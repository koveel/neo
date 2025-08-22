#include "pch.h"

#include "Tree.h"
#include "Cast.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>

#include "Value.h"

#include "Generator.h"
#include "PlatformUtils.h"

#include "CodegenUtils.h"
#include "Enum.h"
#include "GeneratorContext.h"

#define SETLINE generator.current_expression_source_line = sourceLine

static Generator* s_Generator = nullptr;

// TODO: fix error shit
// make these errors like parser errors
template<typename... Args>
static void Assert(bool condition, const char* fmt, Args&&... args)
{
	if (condition)
		return;

	throw CompileError(s_Generator->current_expression_source_line, fmt, std::forward<Args>(args)...);
}

RValue Generator::CastRValueIfNecessary(const RValue& rv, Type* to, bool isExplicit, Expression* source)
{
	Type* from = rv.type;
	if (from == to)
		return rv;

	bool isAnAlias = from->IsAliasFor(to) || to->IsAliasFor(from);
	if (isAnAlias)
		return rv;

	Cast* cast = Cast::IsValid(from, to);
	Assert(cast, "cannot cast from '{}' to '{}'", from->GetName(), to->GetName());

	if (isExplicit || cast->implicit) {
		return RValue{ cast->Invoke(rv.value), to };
	}

	Assert(false, "cannot implicitly cast from '{}' to '{}'", from->GetName(), to->GetName());
	return {};
}

RValue Generator::MaterializeToRValue(const Value& val)
{
	if (val.is_rvalue)
		return val.rvalue;
	//Assert(!val.is_rvalue, "");

	llvm::Value* ptr = val.lvalue.address.ptr;
	Type* resultType = val.type;
	llvm::Value* loaded = EmitLoad(resultType, ptr);

	return RValue{ loaded, resultType };
}

template<typename... Args>
static void Warn(int line, const std::string& message, Args&&... args)
{
	SetConsoleColor(14);
	std::cout << "[line " << line << "]: warning " << FormatString(message, std::forward<Args>(args)...) << "\n";
	ResetConsoleColor();
}

llvm::Value* Generator::EmitStructGEP(StructType* structType, llvm::Value* structPtr, uint32_t memberIndex)
{
	return llvm_context->builder->CreateStructGEP(structType->raw, structPtr, memberIndex);
}

llvm::Value* Generator::EmitInBoundsGEP(Type* type, llvm::Value* ptr, std::initializer_list<llvm::Value*> indices, const char* debug_name)
{
	return llvm_context->builder->CreateInBoundsGEP(type->raw, ptr, indices, debug_name);
}

static uint32_t GetBitWidthOfIntegralType(TypeTag tag)
{
	switch (tag)
	{
		case TypeTag::UInt8:
		case TypeTag::Int8:
			return 8;
		case TypeTag::UInt16:
		case TypeTag::Int16:
			return 16;
		case TypeTag::UInt32:
		case TypeTag::Int32:
			return 32;
		case TypeTag::UInt64:
		case TypeTag::Int64:
			return 64;
		case TypeTag::Bool:
			return 1;
	}

	return 0;
}

llvm::Value* Generator::GetNumericConstant(TypeTag tag, int64_t value)
{
	auto& context = llvm_context->context;

	switch (tag)
	{
	case TypeTag::UInt8:
	case TypeTag::UInt16:
	case TypeTag::UInt32:
	case TypeTag::UInt64:
		return llvm::ConstantInt::get(*context, llvm::APInt(GetBitWidthOfIntegralType(tag), value, false));
	case TypeTag::Int8:
	case TypeTag::Int16:
	case TypeTag::Int32:
	case TypeTag::Int64:
		return llvm::ConstantInt::get(*context, llvm::APInt(GetBitWidthOfIntegralType(tag), value, true));
	case TypeTag::Float32:
		return llvm::ConstantFP::get(*context, llvm::APFloat((float)value));
	case TypeTag::Float64:
		return llvm::ConstantFP::get(*context, llvm::APFloat((double)value));
	}

	ASSERT(false);
	return nullptr;
}

Value NullExpression::Generate(Generator& generator)
{
	SETLINE;
	return RValue{ llvm::ConstantPointerNull::get(llvm::PointerType::get(*generator.llvm_context->context, 0u)), Type::Get(TypeTag::Pointer) };
}

Value PrimaryExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	constexpr auto s = std::numeric_limits<uint32_t>::max();
	auto& context = generator.llvm_context->context;

	switch (type->tag)
	{
	case TypeTag::UInt8:
	case TypeTag::UInt16:
	case TypeTag::UInt32:
	case TypeTag::UInt64:
		return RValue{ llvm::ConstantInt::get(*context, llvm::APInt(GetBitWidthOfIntegralType(type->tag), value.u64, true)), type };
	case TypeTag::Int8:
	case TypeTag::Int16:
	case TypeTag::Int32:
	case TypeTag::Int64:
		return RValue{ llvm::ConstantInt::get(*context, llvm::APInt(GetBitWidthOfIntegralType(type->tag), value.i64, true)), type };
	case TypeTag::Float32:
	case TypeTag::Float64:
		return RValue{ llvm::ConstantFP::get(*context, llvm::APFloat((float)value.f64)), type };
	case TypeTag::Bool:
		return RValue{ llvm::ConstantInt::get(*context, llvm::APInt(8, value.b32)), type };
	}

	Assert(false, "invalid type for primary expression");
	return {};
}

Value StringExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	std::string stringExpr;
	stringExpr.reserve(value.length);
	
	// Scuffed
	for (uint32_t i = 0; i < value.length; i++)
	{
		char c = value.start[i];
		if (c == '\\')
		{
			c = value.start[++i];
			switch (c)
			{
			case 'n':
				stringExpr += 0x0A;
				continue;
			default:
				break;
			}
		}

		stringExpr += c;
	}

	auto& builder = generator.llvm_context->builder;

	Type* stringTy = Type::Get(TypeTag::Pointer, Type::Get(TypeTag::Int8));
	return RValue{ builder->CreateGlobalStringPtr(stringExpr, "gstr", 0U, generator.module.llvm_module), stringTy };
}

static bool IsUnaryArithmeticOperation(UnaryType type)
{
	switch (type)
	{
	case UnaryType::PrefixIncrement:
	case UnaryType::PrefixDecrement:
	case UnaryType::PostfixIncrement:
	case UnaryType::PostfixDecrement:
		return true;
	}

	return false;
}

Value UnaryExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	auto& builder = generator.llvm_context->builder;

	Value value = operand->Generate(generator);
	type = value.type;

	if (type->IsPointer() && IsUnaryArithmeticOperation(unaryType))
	{
		bool postfix = false;
		llvm::Value* offset = nullptr;
		switch (unaryType)
		{
		case UnaryType::PrefixIncrement:
		case UnaryType::PostfixIncrement:
			offset = generator.GetNumericConstant(TypeTag::Int32, 1);
			postfix = unaryType == UnaryType::PostfixIncrement;
			break;
		case UnaryType::PrefixDecrement:
		case UnaryType::PostfixDecrement:
			offset = generator.GetNumericConstant(TypeTag::Int32, -1);
			postfix = unaryType == UnaryType::PostfixDecrement;
			break;
		}

		Assert(!value.is_rvalue, "expected lvalue for increment/decrement");
		LValue lv = value.lvalue;
		Type* ptrTy = lv.type;              // T*
		Type* elemTy = ptrTy->contained;

		llvm::Value* oldPtr = generator.EmitLoad(ptrTy, lv.address.ptr); // load T*
		llvm::Value* newPtr = generator.EmitInBoundsGEP(elemTy, oldPtr, { offset });

		generator.EmitStore(newPtr, lv.address.ptr); // store T* back to T**

		if (postfix) {
			return RValue{ oldPtr, ptrTy }; // result is rvalue T*
		}
		else {
			return RValue{ newPtr, ptrTy }; // result is rvalue T*
		}
	}

	switch (unaryType)
	{
	case UnaryType::Not: // !value
	{
		RValue rv = generator.MaterializeToRValue(value);
		return RValue{ builder->CreateNot(rv.value), rv.type };
	}
	case UnaryType::Negate: // -value
	{
		RValue rv = generator.MaterializeToRValue(value);

		Type* result = value.type;
		if (result->IsInteger())
			return RValue{ builder->CreateNeg(rv.value), rv.type };
		if (result->IsFloatingPoint())
			return RValue{ builder->CreateFNeg(rv.value), rv.type };

		Assert(false, "invalid operand for unary negation (-), operand must be numeric");
		break;
	}
	case UnaryType::PrefixIncrement:
	{
		// Increment
		LValue& lv = value.lvalue;
		llvm::Value* loaded = generator.EmitLoad(lv.type->contained, lv.address.ptr);
		generator.EmitStore(builder->CreateAdd(loaded, generator.GetNumericConstant(type->tag, 1)), lv.address.ptr);

		// Return newly incremented value
		Type* resultTy = lv.type->contained;
		return LValue{ generator.EmitLoad(lv.type->contained, lv.address.ptr), resultTy };
	}
	case UnaryType::PostfixIncrement:
	{
		// Increment
		LValue& lv = value.lvalue;
		RValue rv = generator.MaterializeToRValue(lv);
		generator.EmitStore(builder->CreateAdd(rv.value, generator.GetNumericConstant(type->tag, 1)), lv.address.ptr);

		// Return value before increment
		return rv;
	}
	case UnaryType::PrefixDecrement:
	{
		RValue rv = generator.MaterializeToRValue(value);
		LValue& lv = value.lvalue;
		generator.EmitStore(builder->CreateSub(rv.value, generator.GetNumericConstant(type->tag, 1)), lv.address.ptr);

		Type* resultTy = lv.type->contained;
		return LValue{ generator.EmitLoad(lv.type->contained, lv.address.ptr), resultTy };
	}
	case UnaryType::PostfixDecrement:
	{
		LValue& lv = value.lvalue;
		RValue rv = generator.MaterializeToRValue(value);
		generator.EmitStore(builder->CreateSub(rv.value, generator.GetNumericConstant(type->tag, 1)), lv.address.ptr);

		return rv;
	}
	case UnaryType::AddressOf:
	{
		LValue& lv = value.lvalue;
		type = lv.type;
		return RValue{ lv.address.ptr, lv.type }; // Industry trade secret - we don't actually take the address of it
	}
	case UnaryType::Deref:
	{
		bool load = true;
		if (CastExpression* cast = ToExpr<CastExpression>(operand))
		{
			if (cast->type->IsPointer())
				load = false;
		}

		Type* ptrTy = value.type;
		Type* elemTy = value.type->contained;
		if (value.is_rvalue) {
			llvm::Value* basePtr = value.rvalue.value;
			return LValue{ basePtr, elemTy };
		}
		else {
			llvm::Value* basePtr = generator.EmitLoad(ptrTy, value.lvalue.address.ptr);
			return LValue{ basePtr, elemTy };
		}
	}
	}

	return {};
}

LValue Generator::EmitSubscript(BinaryExpression* binary)
{
	PROFILE_FUNCTION();
	 
	llvm::Value* zeroIndex = GetNumericConstant(TypeTag::Int32, 0);
	Value indexv = binary->right->Generate(*this);
	RValue indexrv = MaterializeToRValue(indexv);

	bool isIndexInteger = indexrv.type->IsInteger();
	Assert(isIndexInteger, "expected integer value for array index");

	Value arrayv = binary->left->Generate(*this);
	Assert(!arrayv.is_rvalue, "expected lvalue for lhs of subscript expression");
	const LValue& arraylv = arrayv.lvalue;
	Assert(binary->left->type->IsArray(), "expected subscript target to be of array type");

	binary->type = binary->left->type->contained;
	ArrayType* arrayType = binary->left->type->IsArray();
	Assert(arrayType, "dwadd");

	llvm::Value* ptr = EmitInBoundsGEP(arrayType, arraylv.address.ptr, { zeroIndex, indexrv.value }, "arr.idx");
	return LValue{ ptr, arrayType->contained->GetPointerTo() };
}

Value EnumDefinitionExpression::Generate(Generator& generator)
{
	SETLINE;
	return {};
}

Value VariableDefinitionExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	Value initialVal;
	RValue initrv;

	bool aggregateInitialization = false;
	CompoundExpression* aggregateInitializer = nullptr;

	if (initializer)
	{
		switch (initializer->nodeType)
		{
			case NodeType::Primary:
			{
				auto primary = ToExpr<PrimaryExpression>(initializer);
				primary->type = type;
				break;
			}
			case NodeType::Compound:
			{
				// aggregate init
				CompoundExpression* compound = ToExpr<CompoundExpression>(initializer);
				aggregateInitializer = compound;
				aggregateInitialization = true;

				Assert(compound->type, "expected aggregate initializer to be typed");

				if (ArrayType* compoundArrayType = compound->type->IsArray()) // [...]
				{
					llvm::Value* initializer = generator.CreateArrayAlloca(compoundArrayType, compound->children, name.c_str());
					generator.currentScope->AddValue(name, { type = compound->type, initializer });
					return {};
					//return { initializer, compoundArrayType, compound };
				}

				break;
			}
		}

		if (!aggregateInitialization)
		{
			initialVal = initializer->Generate(generator);
			initrv = generator.MaterializeToRValue(initialVal);
			type = initializer->type;
		}
	}

	// Alloc
	llvm::Value* alloc = generator.EmitAlloca(type, nullptr, name.c_str());
	generator.currentScope->AddValue(name, { type, alloc });

	// Initialize members if struct
	if (StructType* structType = type->IsStruct())
	{
		if (aggregateInitialization) {
			generator.InitializeStructMembersAggregate(alloc, structType, aggregateInitializer);
			return {};
			//return { alloc, structType, this };
		}

		if (!initializer) {
			generator.InitializeStructMembersToDefault(alloc, structType);
			return {};
			// return { alloc, structType, this };
		}

		//initialVal = generator.LoadValueIfVariable(initializer->Generate(generator), initializer);
		initialVal = initializer->Generate(generator);
		initrv = generator.MaterializeToRValue(initialVal);
		generator.EmitStore(initrv.value, alloc);

		return {};
		//return { alloc, structType, this };
	}

	if (!initialVal.type)
	{
		if (type->IsPointer())
			initrv = RValue{ llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type->raw)), type };
		if (type->IsNumeric())
			initrv = RValue{ generator.GetNumericConstant(type->tag, 0), type };
		//if (ArrayType* arrTy = type->IsArray())
		//	return { alloc, arrTy, this };
	}
	else {
		initrv = generator.MaterializeToRValue(initialVal);
	}

	generator.EmitStore(initrv.value, alloc);

	return {};
}

static llvm::Value* AccessEnumMember(Enumeration& target, Expression* rhs)
{
	ASSERT(rhs->nodeType == NodeType::VariableAccess);
	auto* access = ToExpr<VariableAccessExpression>(rhs);

	const std::string& memberName = access->name;
	Assert(target.members.count(memberName), "enum '{}' does not have member '{}'", target.name, memberName);

	return target.members[memberName];
}

LValue Generator::EmitMemberAccessExpression(BinaryExpression* binary)
{
	PROFILE_FUNCTION();

	// ENUM ?
	if (auto* leftVariable = ToExpr<VariableAccessExpression>(binary->left))
	{
		const std::string& lhsID = leftVariable->name;
		if (module.DefinedEnums.count(lhsID))
		{
			Enumeration& enume = module.DefinedEnums[lhsID];
			binary->type = enume.integralType;
			return LValue{ AccessEnumMember(enume, binary->right.get()), enume.integralType }; // not an lvalue idiot
		}
	}
	// STRUCTURE

	Value structurev = binary->left->Generate(*this);
	llvm::Value* basePtr = nullptr; // will be ptr to struct
	StructType* structType = nullptr;
	Type* T = structurev.type;

	if (T->IsStruct()) {
		structType = T->IsStruct();
		if (structurev.is_rvalue) {
			LValue tmp = { EmitAlloca(T), T }; // S*
			EmitStore(structurev.rvalue.value, tmp.address.ptr);
			basePtr = tmp.address.ptr;
		}
		else
			basePtr = structurev.lvalue.address.ptr;
	}
	else if (T->IsPointer() && T->contained->IsStruct()) {
		structType = T->contained->IsStruct();
		if (structurev.is_rvalue)
			basePtr = structurev.rvalue.value;
		else
			basePtr = EmitLoad(T, structurev.lvalue.address.ptr);
	}
	else {
		Assert(false, "member access on non-struct or non-pointer-to-struct type");
	}

	// 2) Resolve member index and type
	VariableAccessExpression* memberExpr = ToExpr<VariableAccessExpression>(binary->right);
	Assert(memberExpr, "expected variable access expression for rhs of member access");

	const auto& members = structType->definition->members;
	int idx = -1;
	for (uint32_t i = 0; i < members.size(); ++i) {
		if (members[i]->name == memberExpr->name) { idx = (int)i; break; }
	}
	Assert(idx >= 0, "not a member of struct");

	Type* fieldTy = members[idx]->type;

	// 3) GEP to the field and return an LValue to the field
	llvm::Value* fieldPtr = EmitStructGEP(structType, basePtr, (uint32_t)idx); // element type is S
	binary->type = fieldTy;  // if you thread types through nodes

	return LValue{ fieldPtr, fieldTy };  // NOTE: LValue.type is the field's frontend type, not pointer
}

Value VariableAccessExpression::Generate(Generator& generator)
{
	SETLINE;

	ScopedValue variable;
	Assert(generator.currentScope->HasValue(name, &variable), "identifier '{}' not declared in scope", name);

	type = variable.type;
	return LValue{ variable.raw, variable.type };
}

llvm::Value* Generator::EmitStore(llvm::Value* value, llvm::Value* ptr)
{
	return llvm_context->builder->CreateStore(value, ptr);
}

llvm::Value* Generator::EmitLoad(Type* result, llvm::Value* ptr)
{
	return llvm_context->builder->CreateLoad(result->raw, ptr);
}

llvm::Value* Generator::EmitBinaryOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs, const char* debug_name)
{
	return llvm_context->builder->CreateBinOp((llvm::Instruction::BinaryOps)op, lhs, rhs, debug_name);
}

llvm::Value* Generator::EmitComparisonOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs)
{
	return llvm_context->builder->CreateCmp((llvm::CmpInst::Predicate)op, lhs, rhs);
}

llvm::Value* Generator::EmitAlloca(Type* type, llvm::Value* arraySize, const char* debug_name)
{
	return llvm_context->builder->CreateAlloca(type->raw, arraySize, debug_name);
}

Value ConstantDefinitionExpression::Generate(Generator& generator)
{
	SETLINE;

	return {};
}

Value CompoundExpression::Generate(Generator& generator)
{
	SETLINE;

	if (type) {
		if (StructType* structType = type->IsStruct()) {
			//if (!structType->raw) {
			//	// The type wasn't resolved
			//}

			llvm::Value* structPtr = generator.EmitAlloca(structType);
			generator.InitializeStructMembersAggregate(structPtr, structType, this);
			return LValue{ structPtr, structType };
		}
	}

	auto& builder = generator.llvm_context->builder;
	auto& context = generator.llvm_context->context;

	generator.currentScope = generator.currentScope->Deepen();

	llvm::Function* current_function = generator.llvm_context->current_function.function;

	llvm::BasicBlock* previousBlock = builder->GetInsertBlock();
	llvm::BasicBlock* block = llvm::BasicBlock::Create(*context, "entry", current_function);
	builder->CreateBr(block);
	builder->SetInsertPoint(block);

	for (auto& expr : children)
		expr->Generate(generator);

	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "end", current_function);
	builder->CreateBr(endBlock);
	builder->SetInsertPoint(endBlock);

	generator.currentScope = generator.currentScope->Increase();

	return {};
}

Value BranchExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	// TODO: else if

	auto& builder = generator.llvm_context->builder;
	auto& context = generator.llvm_context->context;
	auto& current_function = generator.llvm_context->current_function;

	llvm::BasicBlock* parentBlock = builder->GetInsertBlock();
	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "brend", current_function.function, current_function.return_block);

	auto generateBranch = [&](Branch& branch, const char* blockName)
	{
		PROFILE_FUNCTION();

		generator.currentScope = generator.currentScope->Deepen();
			
		llvm::BasicBlock* block = llvm::BasicBlock::Create(*context, blockName, current_function.function, endBlock);
		builder->SetInsertPoint(block);

		for (auto& expr : branch.body)
			expr->Generate(generator);

		if (!block->getTerminator())
			builder->CreateBr(endBlock);
		builder->SetInsertPoint(parentBlock);

		generator.currentScope = generator.currentScope->Increase();

		return block;
	};

	Branch& ifBranch = branches[0];
	llvm::BasicBlock* trueBlock = generateBranch(ifBranch, "btrue"), *falseBlock = nullptr;

	if (branches.size() > 1)
		falseBlock = generateBranch(branches[branches.size() - 1], "bfalse");

	Value condition = ifBranch.condition->Generate(generator);
	RValue condrv = generator.MaterializeToRValue(condition);
	llvm::BranchInst* branchInst = builder->CreateCondBr(condrv.value, trueBlock, endBlock);
	builder->SetInsertPoint(endBlock);

	return {};
}

Value FunctionDefinitionExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	auto& builder = generator.llvm_context->builder;
	auto& context = generator.llvm_context->context;

	type = prototype.ReturnType;
	bool hasBody = body.size();
	bool isVoid = type->tag == TypeTag::Void;

	auto& current_function = generator.llvm_context->current_function;

	// Full definition for function
	current_function.function = generator.module.llvm_module->getFunction(prototype.Name);

	llvm::BasicBlock* previousBlock = builder->GetInsertBlock();
	llvm::BasicBlock* bodyBlock = nullptr;
	
	// Create block, deepen scope
	if (hasBody)
	{
		generator.currentScope = generator.currentScope->Deepen(); // No point in scoping args if there's no body

		bodyBlock = llvm::BasicBlock::Create(*context, "entry", current_function.function);
		builder->SetInsertPoint(bodyBlock);

		// Allocate return value
		if (!isVoid)
			current_function.return_value_alloca = generator.EmitAlloca(type, nullptr, "return");

		current_function.return_block = llvm::BasicBlock::Create(*context, "exit", current_function.function);
	}

	// Set names for function args
	uint32_t i = 0;
	for (auto& arg : current_function.function->args())
	{
		auto& parameter = prototype.Parameters[i++];
		arg.setName(parameter->name);

		if (!hasBody)
			continue;

		// Alloc arg
		llvm::Value* alloc = generator.EmitAlloca(parameter->type);
		generator.EmitStore(&arg, alloc);
		generator.currentScope->AddValue(parameter->name, { parameter->type, alloc });
	}

	// Gen body
	if (hasBody)
	{
		PROFILE_SCOPE("Generate function body");

		for (auto& node : body)
			node->Generate(generator);

		// Insert terminator
		if (!generator.currentScope->contains_terminator)
			builder->CreateBr(current_function.return_block);

		generator.currentScope = generator.currentScope->Increase();
		builder->SetInsertPoint(current_function.return_block);

		// Terminator
		if (isVoid) {
			builder->CreateRetVoid();
		}
		else {
			//Type* returnType = 
			builder->CreateRet(generator.EmitLoad(type, current_function.return_value_alloca));
		}
	}

	{
		PROFILE_SCOPE("Verify function");

		// Handle any errors in the function
		if (llvm::verifyFunction(*current_function.function, &llvm::errs()))
		{
			generator.module.llvm_module->print(llvm::errs(), nullptr);
			current_function.function->eraseFromParent();
			Assert(false, "function verification failed");
		}
	}

	return {};
}

Value CastExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	Value value = from->Generate(generator);
	RValue rv = generator.MaterializeToRValue(value);

	bool useTag = false; // tf is this?
	if (type->IsPointer() || from->type->IsPointer())
		useTag = true;

	Cast* cast = useTag ? Cast::IsValid(from->type->tag, type->tag) : Cast::IsValid(from->type, type);
	Assert(cast, "cannot cast from '{}' to '{}'", from->type->GetName(), type->GetName());

	return RValue{ cast->Invoke(rv.value, this), type };
}

Value ReturnStatement::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	auto& builder = generator.llvm_context->builder;
	auto& current_function = generator.llvm_context->current_function;

	auto ty = current_function.function->getReturnType();
	type = Type::FromLLVM(ty);

	generator.currentScope->contains_terminator = true;

	if (type->tag == TypeTag::Void)
	{
		builder->CreateBr(current_function.return_block);
		return {};
	}

	Assert(value.get(), "expected return statement to express a value of type '{}'", type->GetName());
	RValue returnValue = {};

	// In-place aggregate initialization
	if (CompoundExpression* compound = ToExpr<CompoundExpression>(value))
	{
		if (StructType* structType = type->IsStruct())
		{
			llvm::Value* structPtr = generator.EmitAlloca(type);
			generator.InitializeStructMembersAggregate(structPtr, structType, compound);

			returnValue = RValue{ generator.EmitLoad(type, structPtr), structType };
		}
		if (ArrayType* arrayType = type->IsArray())
		{
			llvm::Value* arrayPtr = generator.CreateArrayAlloca(arrayType, compound->children);
			returnValue = RValue{ generator.EmitLoad(arrayType, arrayPtr), arrayType };
		}

		ASSERT(returnValue.value);
	}
	else
	{
		// Everything else
		Value returnv = value->Generate(generator);
		returnValue = generator.MaterializeToRValue(returnv);
		//returnValue = generator.CastRValueIfNecessary(returnValue, type, false, this);
	}

	generator.EmitStore(returnValue.value, current_function.return_value_alloca);
	builder->CreateBr(current_function.return_block);
	
	return {};
}

Value FunctionCallExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	SETLINE;

	Module& module = generator.module;
	Assert(module.DefinedFunctions.count(name), "undeclared function '{}'", name);

	FunctionSignature& signature = module.DefinedFunctions[name];
	bool argCountMatch = signature.Parameters.size() == arguments.size();
	Assert(argCountMatch, "expected {} arguments for call to '{}' but got {}", signature.Parameters.size(), name, arguments.size());
	type = signature.Return;

	llvm::Function* function = module.llvm_module->getFunction(name);

	std::vector<llvm::Value*> argValues;
	uint32_t i = 0;
	for (auto& expr : arguments)
	{
		// Generate arg
		Value value = expr->Generate(generator);
		RValue rv = generator.MaterializeToRValue(value);
		Type* argumentType = rv.type;

		// Type check
		Type* expectedTy = signature.Parameters[i];
		//rv = generator.CastRValueIfNecessary(rv, expectedTy, false, expr.get());
			
		argValues.push_back(rv.value);
		i++;
	}

	return RValue{ generator.llvm_context->builder->CreateCall(function, argValues), type };
}

void Generator::ResolvePrimitiveType(Type& type, int possibleSourceLine)
{
	PROFILE_FUNCTION();

	// Already resolved?
	if (type.raw)
		return;

	auto& context = llvm_context->context;

	if (type.IsPointer())
	{
		// *i32 -> i32
		Type* contained = type.contained;
		ResolveType(*contained);
		type.raw = llvm::PointerType::get(contained->raw, 0u); // Magic address space of 0???

		return;
	}

	switch (type.tag)
	{
	case TypeTag::UInt8:
	case TypeTag::Int8:
	{
		type.raw = llvm::Type::getInt8Ty(*context);
		return;
	}
	case TypeTag::UInt16:
	case TypeTag::Int16:
	{
		type.raw = llvm::Type::getInt16Ty(*context);
		return;
	}
	case TypeTag::UInt32:
	case TypeTag::Int32:
	{
		type.raw = llvm::Type::getInt32Ty(*context);
		return;
	}
	case TypeTag::UInt64:
	case TypeTag::Int64:
	{
		type.raw = llvm::Type::getInt64Ty(*context);
		return;
	}
	case TypeTag::Float32:
	{
		type.raw = llvm::Type::getFloatTy(*context);
		return;
	}
	case TypeTag::Float64:
	{
		type.raw = llvm::Type::getDoubleTy(*context);
		return;
	}
	case TypeTag::Bool:
	{
		type.raw = llvm::Type::getInt1Ty(*context);
		return;
	}
	case TypeTag::Void:
	{
		type.raw = llvm::Type::getVoidTy(*context);
		return;
	}
	}
}

void Generator::ResolveStructType(StructType& type, int possibleSourceLine)
{
	PROFILE_FUNCTION();

	// Already resolved?
	if (type.raw)
		return;	

	if (!type.definition) {
		return;
	}
	//throw CompileError(possibleSourceLine, "type '{}' not defined", type.name);

	auto& members = type.definition->members;

	std::vector<llvm::Type*> memberTypes;
	memberTypes.reserve(members.size());

	for (auto& member : members)
	{
		ResolveType(*member->type, (int)type.definition->sourceLine);
		memberTypes.push_back(member->type->raw);
	}

	type.raw = llvm::StructType::create(*llvm_context->context, memberTypes, type.name);
}

void Generator::ResolveArrayType(ArrayType& type, int possibleSourceLine)
{
	PROFILE_FUNCTION();

	// Already resolved?
	if (type.raw)
		return;

	// []f32 -> f32
	Type* elementType = type.contained;
	ResolveType(*elementType);
	type.raw = llvm::ArrayType::get(elementType->raw, type.count);
}

static AliasType* IsTypeNameAnAlias(const std::string& name);

void Generator::ResolveType(Type& type, int line)
{
	bool isNamedType = type.tag == TypeTag::Struct || type.tag == TypeTag::Alias;
	AliasType* alias = nullptr;
	if (isNamedType && (alias = type.IsAlias()))
	{
		ASSERT(alias->aliasedType);
		ResolveType(*alias->aliasedType);
		type.raw = alias->aliasedType->raw;
		return;
	}

	switch (type.tag)
	{
	case TypeTag::Array:
	{
		ResolveArrayType(*type.IsArray(), line);
		break;
	}
	case TypeTag::Struct:
	{
		StructType* structTy = type.IsStruct();
		if (AliasType* alias = IsTypeNameAnAlias(structTy->name))
		{
			ResolveType(*alias);
			type = *alias;
			break;
		}

		ResolveStructType(*structTy, line);
		break;
	}
	default:
	{
		ResolvePrimitiveType(type, line);
		break;
	}
	}

	// LLVM's opaque pointer types dont do us much favors
	if (!type.IsPointer())
		Type::LLVMToNeoTypes[type.raw] = &type;
}

static llvm::Type* FindReturnTypeFromBlock(std::vector<std::unique_ptr<Expression>>& block)
{
	for (auto& expr : block)
	{
		if (ReturnStatement* ret = ToExpr<ReturnStatement>(expr))
		{
			return ret->type->raw;
		}

		if (CompoundExpression* compound = ToExpr<CompoundExpression>(expr))
		{
			if (llvm::Type* possible = FindReturnTypeFromBlock(compound->children))
				return possible;
		}
	}

	return nullptr;
}

namespace CastFunctions
{
	// Int / int
	static llvm::Value* SInteger_To_SInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateZExtOrTrunc(integer, to);
	}
	static llvm::Value* UInteger_To_UInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateZExtOrTrunc(integer, to);
	}

	static llvm::Value* SInteger_To_UInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateZExtOrTrunc(integer, to);
	}
	static llvm::Value* UInteger_To_SInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateZExtOrTrunc(integer, to);
		//return integer;
	}

	// Float / float
	static llvm::Value* F32_To_F64(Cast&, llvm::Value* f32, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateFPExt(f32, to);
	}

	static llvm::Value* F64_To_F32(Cast&, llvm::Value* f64, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateFPTrunc(f64, to);
	}

	// Integer / floating point
	static llvm::Value* SInteger_To_FP(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateSIToFP(integer, to);
	}
	static llvm::Value* UInteger_To_FP(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateUIToFP(integer, to);
	}
	static llvm::Value* FP_To_SInteger(Cast&, llvm::Value* f32, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateFPToSI(f32, to);
	}
	static llvm::Value* FP_To_UInteger(Cast&, llvm::Value* f32, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateFPToUI(f32, to);
	}

	// Ptr / int
	static llvm::Value* Pointer_To_Integer(Cast&, llvm::Value* ptr, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreatePtrToInt(ptr, to);
	}
	static llvm::Value* Integer_To_Pointer(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreateIntToPtr(integer, to);
	}

	static llvm::Value* Pointer_To_Pointer(Cast&, llvm::Value* ptr, llvm::Type* to) {
		return s_Generator->llvm_context->builder->CreatePointerCast(ptr, to);
	}
}

static void InitPrimitiveCasts()
{
	Type* i8  = Type::Get(TypeTag::Int8);
	Type* i16 = Type::Get(TypeTag::Int16);
	Type* i32 = Type::Get(TypeTag::Int32);
	Type* i64 = Type::Get(TypeTag::Int64);
	Type* u8  = Type::Get(TypeTag::UInt8);
	Type* u16 = Type::Get(TypeTag::UInt16);
	Type* u32 = Type::Get(TypeTag::UInt32);
	Type* u64 = Type::Get(TypeTag::UInt64);	
	Type* f32 = Type::Get(TypeTag::Float32);
	Type* f64 = Type::Get(TypeTag::Float64);
	Type* b1  = Type::Get(TypeTag::Bool);
	Type* ptr = Type::Get(TypeTag::Pointer);

	Type* signedIntTypes[] = { i8, i16, i32, i64 };
	Type* unsignedIntTypes[] = { u8, u16, u32, u64 };
	Type* fpTypes[] = { f32, f64 };

	const bool Allow_Int_X_FP_Implicitly   = true;
	const bool Allow_Int_X_UInt_Implicitly = true;

	Cast::Add(TypeTag::Pointer, TypeTag::Int64,   CastFunctions::Pointer_To_Integer, false);
	Cast::Add(TypeTag::Int64,   TypeTag::Pointer, CastFunctions::Integer_To_Pointer, false);
	Cast::Add(TypeTag::Bool,    TypeTag::Pointer, CastFunctions::Integer_To_Pointer, true);
	Cast::Add(TypeTag::Pointer, TypeTag::Bool,    CastFunctions::Pointer_To_Integer, true);

	Cast::Add(TypeTag::Pointer, TypeTag::Pointer, CastFunctions::Pointer_To_Pointer, false);

	for (Type* sint : signedIntTypes)
	{
		// Int / Int
		for (Type* sint2 : signedIntTypes)
		{
			if (sint == sint2)
				continue;

			Cast::Add(sint, sint2, CastFunctions::SInteger_To_SInteger, true);
			Cast::Add(sint2, sint, CastFunctions::SInteger_To_SInteger, true);
		}

		// Int / bool
		Cast::Add(sint, b1, CastFunctions::SInteger_To_SInteger, true);
		Cast::Add(b1, sint, CastFunctions::SInteger_To_SInteger, true);

		// Int / FP
		for (Type* fp : fpTypes)
		{
			Cast::Add(sint, fp, CastFunctions::SInteger_To_FP, Allow_Int_X_FP_Implicitly);
			Cast::Add(fp, sint, CastFunctions::FP_To_SInteger, Allow_Int_X_FP_Implicitly);
		}

		// Int / UInt
		for (Type* uint : unsignedIntTypes)
		{
			Cast::Add(sint, uint, CastFunctions::SInteger_To_UInteger, Allow_Int_X_UInt_Implicitly);
			Cast::Add(uint, sint, CastFunctions::UInteger_To_SInteger, Allow_Int_X_UInt_Implicitly);
		}
	}
	for (Type* uint : unsignedIntTypes)
	{
		// UInt / UInt
		for (Type* uint2 : unsignedIntTypes)
		{
			if (uint == uint2)
				continue;

			Cast::Add(uint, uint2, CastFunctions::UInteger_To_UInteger, true);
			Cast::Add(uint2, uint, CastFunctions::UInteger_To_UInteger, true);
		}

		// UInt / bool
		Cast::Add(uint, b1, CastFunctions::UInteger_To_SInteger, true);
		Cast::Add(b1, uint, CastFunctions::SInteger_To_UInteger, true);

		// UInt / FP
		for (Type* fp : fpTypes)
		{
			Cast::Add(uint, fp, CastFunctions::UInteger_To_FP, Allow_Int_X_FP_Implicitly);
			Cast::Add(fp, uint, CastFunctions::FP_To_UInteger, Allow_Int_X_FP_Implicitly);
		}
	}
}

void Generator::ResolveParsedTypes()
{
	PROFILE_FUNCTION();

	for (auto& expr : module.SyntaxTree->children)
		expr->ResolveType();

	for (auto& pair : Type::RegisteredTypes)
	{
		Type* type = pair.second;
		ResolveType(*type);
	}

	for (auto& pair : StructType::RegisteredTypes)
	{
		Type* type = pair.second;
		ResolveType(*type);
	}

	for (auto& pair : ArrayType::RegisteredTypes)
	{
		Type* type = pair.second;
		ResolveType(*type);
	}
}

AliasType* IsTypeNameAnAlias(const std::string& name)
{
	if (AliasType::RegisteredTypes.count(name))
		return AliasType::RegisteredTypes[name];

	return nullptr;
}

AliasType* Type::IsAlias()
{
	if (tag != TypeTag::Alias)
		return nullptr;

	std::string name = GetName();
	if (AliasType::RegisteredTypes.count(name))
	{
		AliasType* alias = AliasType::RegisteredTypes[name];
		s_Generator->ResolveType(*alias->aliasedType);
		return alias;
	}
}

AliasType* Type::IsAliasFor(Type* other)
{
	if (tag != TypeTag::Alias)
		return nullptr;

	std::string name = GetName();
	if (AliasType::RegisteredTypes.count(name))
	{
		AliasType* alias = AliasType::RegisteredTypes[name];
		s_Generator->ResolveType(*alias->aliasedType);
		
		if (alias->aliasedType == other)
			return alias;
	}

	return nullptr;
}

StructType* Type::IsStruct()
{
	if (AliasType* alias = IsAlias()) {
		return alias->aliasedType->IsStruct();
	}

	if (tag == TypeTag::Struct)
		return static_cast<StructType*>(this);

	return nullptr;
}

Type* Type::GetContainedType() const
{
	ASSERT(contained);
	if (s_Generator->llvm_context->context)
		s_Generator->ResolveType(*contained);

	return contained;
}

Type* Type::GetPointerTo()
{
	Type* pointerTy = Type::Get(TypeTag::Pointer, this);
	if (s_Generator->llvm_context->context)
		s_Generator->ResolveType(*pointerTy);

	return pointerTy;
}

ArrayType* Type::GetArrayTypeOf(uint64_t count)
{
	ArrayType* arrayTy = ArrayType::Get(this, count);
	if (s_Generator->llvm_context->context)
		s_Generator->ResolveArrayType(*arrayTy);

	return arrayTy;
}

//static void DoOptimizationPasses(const CommandLineArguments& compilerArgs)
//{
//	PROFILE_FUNCTION();
//
//	using namespace llvm;
//
//	PassBuilder passBuilder;
//
//	FunctionPassManager fpm;
//	LoopAnalysisManager loopAnalysisManager;
//	FunctionAnalysisManager functionAnalysisManager;
//	CGSCCAnalysisManager cgsccAnalysisManager;
//	ModuleAnalysisManager moduleAnalysisManager;
//
//	// Register all the basic analyses with the managers
//	passBuilder.registerModuleAnalyses(moduleAnalysisManager);
//	passBuilder.registerCGSCCAnalyses(cgsccAnalysisManager);
//	passBuilder.registerFunctionAnalyses(functionAnalysisManager);
//	passBuilder.registerLoopAnalyses(loopAnalysisManager);
//	passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager, cgsccAnalysisManager, moduleAnalysisManager);
//
//	PassBuilder::OptimizationLevel optLevel;
//	switch (compilerArgs.optimizationLevel)
//	{
//		case 0: optLevel = llvm::PassBuilder::OptimizationLevel::O0; break;
//		case 1: optLevel = llvm::PassBuilder::OptimizationLevel::O1; break;
//		case 2: optLevel = llvm::PassBuilder::OptimizationLevel::O2; break;
//		case 3: optLevel = llvm::PassBuilder::OptimizationLevel::O3; break;
//	}
//
//	ModulePassManager modulePassManager = passBuilder.buildPerModuleDefaultPipeline(optLevel);
//
//	// Hells ya
//	modulePassManager.run(*module.llvm_module, moduleAnalysisManager);
//}

Generator::Generator(Module& module)
	: module(module)
{
	s_Generator = this;

	llvm_context = new LLVM();
	llvm_context->context = std::make_unique<llvm::LLVMContext>();
	llvm_context->builder = std::make_unique<llvm::IRBuilder<>>(*llvm_context->context);

	module.llvm_module = new llvm::Module(llvm::StringRef(), *llvm_context->context);
}

Generator::~Generator()
{
	delete module.llvm_module;
}

CompileResult Generator::Generate(ParseResult& parseResult, const CommandLineArguments& compilerArgs)
{
	PROFILE_FUNCTION();
	
	CompileResult result;

	try
	{
		ResolveParsedTypes();
		InitPrimitiveCasts();
		VisitTopLevelDefinitions();

		currentScope = new Scope();
		// Codegen module
		for (auto& node : module.SyntaxTree->children)
		{
			node->Generate(*this);
		}

		// Optimizations
		if (compilerArgs.optimizationLevel > 0)
		{
			// optimization level 9000%
			//DoOptimizationPasses(compilerArgs);
		}
		
		// Collect IR to string
		llvm::raw_string_ostream stream(result.ir);
		module.llvm_module->print(stream, nullptr);
		result.ir = stream.str();

		result.Succeeded = true;
	}
	catch (const CompileError& err)
	{
		result.Succeeded = false;
		SetConsoleColor(12);
		if (err.line == -1) {
			std::cout << "error: " << err.message << "\n";
		}
		else {
			std::cout << "[line " << err.line << "] error: " << err.message << "\n";
		}
		ResetConsoleColor();
	}

	return result;
}

void Scope::AddValue(const std::string& name, const ScopedValue& value)
{
	values[name] = value;
}

bool Scope::HasValue(const std::string& name, ScopedValue* out, bool checkParents) const
{
	auto it = values.find(name);
	bool existsInThisScope = it != values.end();

	if (existsInThisScope)
	{
		*out = it->second;
		return true;
	}
	if (!checkParents)
		return false;

	bool existsInParentScopes = parentScope && parentScope->HasValue(name, out);
	return existsInParentScopes;
}

bool Generator::ScopeValue(const std::string& name, const ScopedValue& value)
{
	currentScope->AddValue(name, value);

	return true;
}