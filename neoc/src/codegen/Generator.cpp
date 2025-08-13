#include "pch.h"

#include "Tree.h"
#include "Cast.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>

#include "Generator.h"

#include "PlatformUtils.h"

#include "CodegenUtils.h"
#include "Enum.h"
#include "GeneratorContext.h"

static Generator* s_Generator = nullptr;

template<typename... Args>
static void Warn(int line, const std::string& message, Args&&... args)
{
	SetConsoleColor(14);
	std::cout << "[line " << line << "]: warning " << FormatString(message, std::forward<Args>(args)...) << "\n";
	ResetConsoleColor();
}

llvm::Value* Generator::LoadValueIfVariable(llvm::Value* generated, Expression* expr)
{
	PROFILE_FUNCTION();

	if (auto unary = ToExpr<UnaryExpression>(expr))
	{
		switch (unary->unaryType)
		{
		case UnaryType::AddressOf:
		case UnaryType::PrefixIncrement:
		case UnaryType::PrefixDecrement:
		case UnaryType::PostfixIncrement:
		case UnaryType::PostfixDecrement:
			return generated;
		}
	}
	if (auto cast = ToExpr<CastExpression>(expr))
	{
		if (cast->type->IsPointer()) // Casting to pointer
			return generated;
	}

	if (!llvm::isa<llvm::AllocaInst>(generated) && !llvm::isa<llvm::LoadInst>(generated) && !llvm::isa<llvm::GetElementPtrInst>(generated))
		return generated;
		
	return EmitLoad(expr->type, generated);
}

llvm::Value* Generator::Generator::LoadValueIfVariable(llvm::Value* generated, std::unique_ptr<Expression>& expr)
{
	return LoadValueIfVariable(generated, expr.get());
}

static llvm::Value* LoadIfPointer(llvm::Value* value, Expression* expr)
{
	PROFILE_FUNCTION();

	llvm::Type* type = value->getType();
	if (auto unary = ToExpr<UnaryExpression>(expr))
	{
		if (unary->unaryType == UnaryType::AddressOf) // Don't load it if we're trying to get it's address
			return value;
	}
	if (auto cast = ToExpr<CastExpression>(expr))
	{
		if (cast->type->IsPointer()) // Casting to pointer
			return value;
	}

	bool isPointer = type->isPointerTy();
	bool isString = (type->getNumContainedTypes() == 1 && type->getContainedType(0)->isIntegerTy(8)) && expr->type->IsString();

	if (!isPointer || isString)
		return value;

	return s_Generator->EmitLoad(expr->type, value);
}

llvm::Value* Generator::CastValueIfNecessary(llvm::Value* v, Type* from, Type* to, bool isExplicit, Expression* source)
{
	if (from == to)
		return v;

	bool isAnAlias = from->IsAliasFor(to) || to->IsAliasFor(from);
	if (isAnAlias)
		return v;

	Cast* cast = Cast::IsValid(from, to);
	if (!cast)
		throw CompileError(source->sourceLine, "cannot cast from '{}' to '{}'", from->GetName(), to->GetName());

	if (isExplicit || cast->implicit)
		return cast->Invoke(v);

	throw CompileError(source->sourceLine, "cannot implicitly cast from '{}' to '{}'", from->GetName(), to->GetName());
}

llvm::Value* Generator::EmitStructGEP(StructType* structType, llvm::Value* structPtr, uint32_t memberIndex)
{
	return llvm_context->builder->CreateStructGEP(structType->raw, structPtr, memberIndex);
}

llvm::Value* Generator::EmitInBoundsGEP(Type* type, llvm::Value* ptr, std::initializer_list<llvm::Value*> indices)
{
	return llvm_context->builder->CreateInBoundsGEP(type->raw, ptr, indices);
}

static llvm::Value* LoadIfPointer(llvm::Value* value, std::unique_ptr<Expression>& expr)
{
	return LoadIfPointer(value, expr.get());
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

llvm::Value* NullExpression::Generate(Generator& generator)
{
	return llvm::ConstantPointerNull::get(llvm::PointerType::get(*generator.llvm_context->context, 0u));
}

llvm::Value* PrimaryExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

	constexpr auto s = std::numeric_limits<uint32_t>::max();
	auto& context = generator.llvm_context->context;

	switch (type->tag)
	{
	case TypeTag::UInt8:
	case TypeTag::UInt16:
	case TypeTag::UInt32:
	case TypeTag::UInt64:
		return llvm::ConstantInt::get(*context, llvm::APInt(GetBitWidthOfIntegralType(type->tag), value.u64, false));
	case TypeTag::Int8:
	case TypeTag::Int16:
	case TypeTag::Int32:
	case TypeTag::Int64:
		return llvm::ConstantInt::get(*context, llvm::APInt(GetBitWidthOfIntegralType(type->tag), value.i64, true));
	case TypeTag::Float32:
		return llvm::ConstantFP::get(*context, llvm::APFloat((float)value.f64));
	case TypeTag::Float64:
		return llvm::ConstantFP::get(*context, llvm::APFloat(value.f64));
	case TypeTag::Bool:
		return llvm::ConstantInt::getBool(*context, value.b32);
	}

	throw CompileError(sourceLine, "invalid type for primary expression");
}

llvm::Value* StringExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

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

	return builder->CreateGlobalStringPtr(stringExpr, "gstr", 0U, generator.module.llvm_module);
}

llvm::Value* UnaryExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

	auto& builder = generator.llvm_context->builder;

	llvm::Value* value = operand->Generate(generator);
	type = operand->type;

	switch (unaryType)
	{
	case UnaryType::Not: // !value
	{
		value = generator.LoadValueIfVariable(value, operand);
		return builder->CreateNot(value);
	}
	case UnaryType::Negate: // -value
	{
		value = generator.LoadValueIfVariable(value, operand);

		llvm::Type* valueType = value->getType();
		switch (valueType->getTypeID())
		{
		case llvm::Type::IntegerTyID:
			return builder->CreateNeg(value);
		case llvm::Type::FloatTyID:
		case llvm::Type::DoubleTyID:
			return builder->CreateFNeg(value);
		}

		throw CompileError(sourceLine, "invalid operand for unary negation (-), operand must be numeric");
	}
	case UnaryType::PrefixIncrement:
	{
		// Increment
		llvm::Value* loaded = generator.EmitLoad(type, value);
		generator.EmitStore(builder->CreateAdd(loaded, generator.GetNumericConstant(operand->type->tag, 1)), value);

		// Return newly incremented value
		return generator.EmitLoad(type, value);
	}
	case UnaryType::PostfixIncrement:
	{
		// Increment
		llvm::Value* loaded = generator.EmitLoad(type, value);
		generator.EmitStore(builder->CreateAdd(loaded, generator.GetNumericConstant(operand->type->tag, 1)), value);

		// Return value before increment
		return loaded;
	}
	case UnaryType::PrefixDecrement:
	{
		llvm::Value* loaded = generator.EmitLoad(type, value);
		generator.EmitStore(builder->CreateSub(loaded, generator.GetNumericConstant(operand->type->tag, 1)), value);

		return generator.EmitLoad(type, value);
	}
	case UnaryType::PostfixDecrement:
	{
		llvm::Value* loaded = generator.EmitLoad(type, value);
		generator.EmitStore(builder->CreateSub(loaded, generator.GetNumericConstant(operand->type->tag, 1)), value);

		return loaded;
	}
	case UnaryType::AddressOf:
	{
		type = type->GetPointerTo();
		return value; // Industry trade secret - we don't actually take the address of it
	}
	case UnaryType::Deref:
	{
		type = type->GetContainedType();

		bool load = true;
		if (CastExpression* cast = ToExpr<CastExpression>(operand))
		{
			if (cast->type->IsPointer())
				load = false;
		}

		return load ? generator.EmitLoad(type->contained, value) : value;
	}
	}

	ASSERT(false);
}

llvm::Value* Generator::EmitSubscript(BinaryExpression* binary)
{
	PROFILE_FUNCTION();

	llvm::Value* zeroIndex = llvm::ConstantInt::get(*llvm_context->context, llvm::APInt(32, 0, false));

	llvm::Value* indexVal = LoadIfPointer(binary->right->Generate(*this), binary->right);
	if (!indexVal->getType()->isIntegerTy())
		throw CompileError(binary->sourceLine, "expected integer value for array index");

	llvm::Value* arrayPtr = binary->left->Generate(*this); // todo: check this shit
	binary->type = binary->left->type->GetContainedType();

	// Should be doing this at the start of this function
	if (!binary->left->type->IsArray())
		throw CompileError(binary->sourceLine, "expected subscript target to be of array type");

	return llvm_context->builder->CreateInBoundsGEP(binary->left->type->raw, arrayPtr, { zeroIndex, indexVal });
}

llvm::Value* EnumDefinitionExpression::Generate(Generator& generator)
{
	return nullptr;
}

llvm::Value* VariableDefinitionExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

	llvm::Value* initialVal = nullptr;

	bool aggregateInitialization = false;
	CompoundExpression* aggregateInitializer = nullptr;

	if (initializer)
	{
		switch (initializer->nodeType)
		{
			case NodeType::ArrayDefinition:
			{
				auto array = ToExpr<ArrayDefinitionExpression>(initializer);
				return array->Generate(generator);
			}
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

				if (!compound->type) {
					throw CompileError(compound->sourceLine, "expected aggregate initializer to be typed");
				}

				if (ArrayType* compoundArrayType = compound->type->IsArray()) // [...]
				{
					llvm::Value* initializer = generator.CreateArrayAlloca(compoundArrayType, compound->children);
					generator.currentScope->AddValue(name, { type = compound->type, initializer });
					return initializer;
				}

				break;
			}
		}

		if (!aggregateInitialization)
		{
			initialVal = initializer->Generate(generator);
			initialVal = generator.LoadValueIfVariable(initialVal, initializer.get());

			if (type)
				initialVal = generator.CastValueIfNecessary(initialVal, initializer->type, type, false, this);
			else
				type = initializer->type;
		}
	}

	// Alloc
	llvm::Value* alloc = generator.EmitAlloca(type);
	generator.currentScope->AddValue(name, { type, alloc });

	// Initialize members if struct
	if (StructType* structType = type->IsStruct())
	{
		if (aggregateInitialization) {
			generator.InitializeStructMembersAggregate(alloc, structType, aggregateInitializer);
			return alloc;
		}
		
		if (!initializer) {
			generator.InitializeStructMembersToDefault(alloc, structType);
			return alloc;
		}

		//initialVal = generator.LoadValueIfVariable(initializer->Generate(generator), initializer);
		initialVal = initializer->Generate(generator);
		generator.EmitStore(initialVal, alloc);

		return alloc;
	}

	if (!initialVal)
	{
		if (type->IsPointer())
			initialVal = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type->raw));
		if (type->IsNumeric())
			initialVal = generator.GetNumericConstant(type->tag, 0);
		if (type->IsArray())
			return alloc;
	}

	initialVal = generator.CastValueIfNecessary(initialVal, initializer->type, type, false, initializer.get());
	generator.EmitStore(initialVal, alloc);

	return alloc;
}

llvm::Value* Generator::EmitStructureMemberAccess(BinaryExpression* binary)
{
	PROFILE_FUNCTION();
	
	// If left is VariableAccessExpr: objectValue = AllocaInst (ptr)
	// If left is MemberAccess: objectValue = gepinst (ptr)
	llvm::Value* objectValue = binary->left->Generate(*this);
	Type* objectType = binary->left->type;

	if (!objectType->IsStruct() && !objectType->IsArray())
	{
		// This is where u would have used -> instead of . (if I wanted that stupid feature)
		if (objectType->IsPointer())
		{
			objectValue = EmitLoad(objectType, objectValue);
			objectType = objectType->GetContainedType();
		}
		else
			throw CompileError(binary->sourceLine, "can't access member of non-struct type or pointer to struct type");
	}

	// rhs should always be variable access expr
	VariableAccessExpression* memberExpr = nullptr;
	if (!(memberExpr = ToExpr<VariableAccessExpression>(binary->right)))
		throw CompileError(binary->sourceLine, "expected variable access expression for rhs of member access");

	const std::string& targetMemberName = memberExpr->name;
	
	// epic hardcoded array.size
	//if (objectType->IsArray() && targetMemberName == "count")
	//{
	//	uint64_t size = objectType->raw->getArrayNumElements();
	//	binary->type = Type::Get(TypeTag::UInt32);
	//	return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*llvm_context->context), llvm::APInt(32, size, false));
	//}

	StructType* structType = objectType->IsStruct();

	// Find member index
	const auto& members = structType->definition->members;
	int memberIndex = -1;
	for (uint32_t i = 0; i < members.size(); i++)
	{
		auto& member = members[i];
		if (member->name == targetMemberName)
		{
			binary->type = member->type;
			memberIndex = i;
			break;
		}
	}
	if (memberIndex == -1)
		throw CompileError(binary->sourceLine, "'{}' not a member of struct '{}'", memberExpr->name, objectType->GetName());

	llvm::Value* memberPtr = EmitStructGEP(structType, objectValue, (uint32_t)memberIndex);
	return memberPtr;
}

static llvm::Value* AccessEnumMember(Enumeration& target, Expression* rhs)
{
	ASSERT(rhs->nodeType == NodeType::VariableAccess);
	auto* access = ToExpr<VariableAccessExpression>(rhs);

	const std::string& memberName = access->name;
	if (!target.members.count(memberName)) {
		throw CompileError(rhs->sourceLine, "enum '{}' does not have member '{}'", target.name, memberName);
	}

	return target.members[memberName];
}

llvm::Value* Generator::HandleMemberAccessExpression(BinaryExpression* binary)
{
	// Enum?
	if (auto* leftVariable = ToExpr<VariableAccessExpression>(binary->left))
	{
		const std::string& lhsID = leftVariable->name;
		if (module.DefinedEnums.count(lhsID))
		{
			Enumeration& enume = module.DefinedEnums[lhsID];
			binary->type = enume.integralType;
			return AccessEnumMember(enume, binary->right.get());
		}
	}

	return Generator::EmitStructureMemberAccess(binary);
}

llvm::Value* VariableAccessExpression::Generate(Generator& generator)
{
	Value variable;
	if (!generator.currentScope->HasValue(name, &variable))
		throw CompileError(sourceLine, "identifier '{}' not declared in scope", name);

	type = variable.type;
	return variable.raw;
}

llvm::Value* Generator::EmitStore(llvm::Value* value, llvm::Value* ptr)
{
	return llvm_context->builder->CreateStore(value, ptr);
}

llvm::Value* Generator::EmitLoad(Type* result, llvm::Value* ptr)
{
	return llvm_context->builder->CreateLoad(result->raw, ptr);
}

llvm::Value* Generator::EmitBinaryOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs)
{
	return llvm_context->builder->CreateBinOp((llvm::Instruction::BinaryOps)op, lhs, rhs);
}

llvm::Value* Generator::EmitComparisonOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs)
{
	return llvm_context->builder->CreateCmp((llvm::CmpInst::Predicate)op, lhs, rhs);
}

llvm::Value* Generator::EmitAlloca(Type* type, llvm::Value* arraySize, const std::string& debug_name)
{
	return llvm_context->builder->CreateAlloca(type->raw, arraySize, debug_name);
}

llvm::Value* ConstantDefinitionExpression::Generate(Generator& generator)
{
	return nullptr;
}

llvm::Value* CompoundExpression::Generate(Generator& generator)
{
	if (type) {
		if (StructType* structType = type->IsStruct()) {
			//if (!structType->raw) {
			//	// The type wasn't resolved
			//}

			llvm::Value* structPtr = generator.EmitAlloca(structType);
			generator.InitializeStructMembersAggregate(structPtr, structType, this);
			return structPtr;
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

	return block;
}

llvm::Value* BranchExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

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

	llvm::Value* condition = generator.LoadValueIfVariable(ifBranch.condition->Generate(generator), ifBranch.condition);
	llvm::BranchInst* branchInst = builder->CreateCondBr(condition, trueBlock, endBlock);
	builder->SetInsertPoint(endBlock);

	return branchInst;
}

static inline bool s_GeneratedTerminatorForCurrentFunction = false;

llvm::Value* FunctionDefinitionExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

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
			current_function.return_value_alloca = generator.EmitAlloca(type);

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

		generator.currentScope = generator.currentScope->Increase();

		// Insert terminator
		if (!s_GeneratedTerminatorForCurrentFunction)
			builder->CreateBr(current_function.return_block);

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
			throw CompileError(sourceLine, "function verification failed");
		}
	}

	return current_function.function;
}

llvm::Value* CastExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

	llvm::Value* value = LoadIfPointer(from->Generate(generator), from);

	bool useTag = false;
	if (type->IsPointer() || from->type->IsPointer())
		useTag = true;

	Cast* cast = useTag ? Cast::IsValid(from->type->tag, type->tag) : Cast::IsValid(from->type, type);
	if (!cast)
		throw CompileError(sourceLine, "cannot cast from '{}' to '{}'", from->type->GetName(), type->GetName());

	return cast->Invoke(value, this);
}

llvm::Value* ReturnStatement::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

	auto& builder = generator.llvm_context->builder;
	auto& current_function = generator.llvm_context->current_function;

	type = Type::FromLLVM(current_function.function->getReturnType());

	s_GeneratedTerminatorForCurrentFunction = true;

	if (type->tag == TypeTag::Void)
	{
		builder->CreateBr(current_function.return_block);
		return nullptr;
	}

	llvm::Value* returnValue = nullptr;

	// In-place aggregate initialization
	if (CompoundExpression* compound = ToExpr<CompoundExpression>(value))
	{
		if (StructType* structType = type->IsStruct())
		{
			llvm::Value* structPtr = generator.EmitAlloca(type);
			generator.InitializeStructMembersAggregate(structPtr, structType, compound);

			returnValue = generator.EmitLoad(type, structPtr);
		}
		if (ArrayType* arrayType = type->IsArray())
		{
			llvm::Value* arrayPtr = generator.CreateArrayAlloca(arrayType, compound->children);
			returnValue = generator.EmitLoad(arrayType, arrayPtr);
		}

		ASSERT(returnValue);
	}
	else
	{
		// Everything else
		returnValue = value->Generate(generator);
		returnValue = generator.CastValueIfNecessary(generator.LoadValueIfVariable(returnValue, value), value->type, type, false, this);
	}

	generator.EmitStore(returnValue, current_function.return_value_alloca);
	builder->CreateBr(current_function.return_block);

	return returnValue;
}

llvm::Value* FunctionCallExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

	Module& module = generator.module;

	if (!module.DefinedFunctions.count(name)) {
		throw CompileError(sourceLine, "undeclared function '{}'", name);
	}
	FunctionSignature& signature = module.DefinedFunctions[name];
	if (signature.Parameters.size() != arguments.size()) {
		throw CompileError(sourceLine, "expected {} arguments for call to '{}' but got {}", signature.Parameters.size(), name, arguments.size());
	}
	type = signature.Return;

	llvm::Function* function = module.llvm_module->getFunction(name);

	std::vector<llvm::Value*> argValues;
	uint32_t i = 0;
	for (auto& expr : arguments)
	{
		// Generate arg
		llvm::Value* value = expr->Generate(generator);
		Type* argumentType = expr->type;
		value = generator.LoadValueIfVariable(value, expr);

		// Type check
		Type* expectedTy = signature.Parameters[i++];
		value = generator.CastValueIfNecessary(value, expr->type, expectedTy, false, expr.get());
			
		argValues.push_back(value);
	}

	return generator.llvm_context->builder->CreateCall(function, argValues);
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
	case TypeTag::String:
	{
		type.raw = llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0u);
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

void Scope::AddValue(const std::string& name, const Value& value)
{
	values[name] = value;
}

bool Scope::HasValue(const std::string& name, Value* out, bool checkParents) const
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

bool Generator::ScopeValue(const std::string& name, const Value& value)
{
	currentScope->AddValue(name, value);

	return true;
}