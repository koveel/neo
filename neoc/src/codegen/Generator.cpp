#include "pch.h"

#include "Tree.h"
#include "Cast.h"
#include "Generator.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>

#include "PlatformUtils.h"

#include "CodegenUtils.h"

static std::unique_ptr<llvm::LLVMContext> s_Context;
static std::unique_ptr<llvm::IRBuilder<>> s_Builder;
static std::unique_ptr<llvm::Module> s_Module;

static Scope* s_CurrentScope = nullptr;

static struct
{
	llvm::Function* llvmFunction = nullptr;
	llvm::Value* returnValueAlloca = nullptr;
	llvm::BasicBlock* returnBlock = nullptr;
} s_CurrentFunction;

template<typename... Args>
static void Warn(int line, const std::string& message, Args&&... args)
{
	SetConsoleColor(14);
	std::cout << "[line " << line << "]: warning " << FormatString(message, std::forward<Args>(args)...) << "\n";
	ResetConsoleColor();
}

static bool IsRange(std::unique_ptr<Expression>& expr, BinaryExpression** outBinary)
{
	if (expr->nodeType != NodeType::Binary)
		return false;

	BinaryExpression* binary = ToExpr<BinaryExpression>(expr);
	if (binary->binaryType != BinaryType::Range)
		return false;

	*outBinary = binary;
	return true;
}

llvm::Value* Generator::Generator::LoadValueIfVariable(llvm::Value* generated, std::unique_ptr<Expression>& expr)
{
	PROFILE_FUNCTION();

	if (auto unary = ToExpr<UnaryExpression>(expr))
	{
		if (unary->unaryType == UnaryType::AddressOf) // Don't load it if we're trying to get it's address
			return generated;
	}
	if (auto cast = ToExpr<CastExpression>(expr))
	{
		if (cast->type->IsPointer()) // Casting to pointer
			return generated;
	}

	if (!llvm::isa<llvm::AllocaInst>(generated) && !llvm::isa<llvm::LoadInst>(generated) && !llvm::isa<llvm::GetElementPtrInst>(generated))
		return generated;

	return s_Builder->CreateLoad(generated);
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

	return s_Builder->CreateLoad(value);
}

llvm::Value* Generator::CastValueIfNecessary(llvm::Value* v, Type* from, Type* to, bool isExplicit, Expression* source)
{
	if (from == to)
		return v;

	Cast* cast = Cast::IsValid(from, to);
	if (!cast)
		throw CompileError(source->sourceLine, "cannot cast from '{}' to '{}'", from->GetName().c_str(), to->GetName().c_str());

	if (isExplicit || cast->implicit)
		return cast->Invoke(v);

	throw CompileError(source->sourceLine, "cannot implicitly cast from '{}' to '{}'", from->GetName().c_str(), to->GetName().c_str());
}

llvm::Value* Generator::EmitStructGEP(llvm::Value* ptr, uint32_t memberIndex)
{
	return s_Builder->CreateStructGEP(ptr, memberIndex);
}

llvm::Value* Generator::EmitInBoundsGEP(llvm::Type* type, llvm::Value* ptr, std::initializer_list<llvm::Value*> indices)
{
	return s_Builder->CreateInBoundsGEP(type, ptr, indices);
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
	switch (tag)
	{
	case TypeTag::UInt8:
	case TypeTag::UInt16:
	case TypeTag::UInt32:
	case TypeTag::UInt64:
		return llvm::ConstantInt::get(*s_Context, llvm::APInt(GetBitWidthOfIntegralType(tag), value, false));
	case TypeTag::Int8:
	case TypeTag::Int16:
	case TypeTag::Int32:
	case TypeTag::Int64:
		return llvm::ConstantInt::get(*s_Context, llvm::APInt(GetBitWidthOfIntegralType(tag), value, true));
	case TypeTag::Float32:
		return llvm::ConstantFP::get(*s_Context, llvm::APFloat((float)value));
	case TypeTag::Float64:
		return llvm::ConstantFP::get(*s_Context, llvm::APFloat((double)value));
	}
}

llvm::Value* PrimaryExpression::Generate()
{
	PROFILE_FUNCTION();

	constexpr auto s = std::numeric_limits<uint32_t>::max();

	switch (type->tag)
	{
	case TypeTag::Pointer:
		ASSERT(!value.ip64); // ???
		ASSERT(false); // kys
		break;

		// TODO: handle unsigned values properly (wrapping etc)

	case TypeTag::UInt8:
	case TypeTag::UInt16:
	case TypeTag::UInt32:
	case TypeTag::UInt64:
		return llvm::ConstantInt::get(*s_Context, llvm::APInt(GetBitWidthOfIntegralType(type->tag), value.u64, false));
	case TypeTag::Int8:
	case TypeTag::Int16:
	case TypeTag::Int32:
	case TypeTag::Int64:
		return llvm::ConstantInt::get(*s_Context, llvm::APInt(GetBitWidthOfIntegralType(type->tag), value.i64, true));
	case TypeTag::Float32:
		return llvm::ConstantFP::get(*s_Context, llvm::APFloat((float)value.f64));
	case TypeTag::Float64:
		return llvm::ConstantFP::get(*s_Context, llvm::APFloat(value.f64));
	case TypeTag::Bool:
		return llvm::ConstantInt::getBool(*s_Context, value.b32);
	}

	throw CompileError(sourceLine, "invalid type for primary expression");
}

llvm::Value* StringExpression::Generate()
{
	PROFILE_FUNCTION();

	std::string stringExpr;
	stringExpr.reserve(value.length);
	
	// Scuffed
	// TODO: unretard the string lexing
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

	return s_Builder->CreateGlobalStringPtr(stringExpr, "gstr", 0U, s_Module.get());
}

llvm::Value* UnaryExpression::Generate()
{
	PROFILE_FUNCTION();

	llvm::Value* value = operand->Generate();
	type = operand->type;

	switch (unaryType)
	{
	case UnaryType::Not: // !value
	{
		value = Generator::LoadValueIfVariable(value, operand);
		return s_Builder->CreateNot(value);
	}
	case UnaryType::Negate: // -value
	{
		value = Generator::LoadValueIfVariable(value, operand);

		llvm::Type* valueType = value->getType();
		switch (valueType->getTypeID())
		{
		case llvm::Type::IntegerTyID:
			return s_Builder->CreateNeg(value);
		case llvm::Type::FloatTyID:
		case llvm::Type::DoubleTyID:
			return s_Builder->CreateFNeg(value);
		}

		throw CompileError(sourceLine, "invalid operand for unary negation (-), operand must be numeric");
	}
	case UnaryType::PrefixIncrement:
	{
		// Increment
		llvm::Value* loaded = s_Builder->CreateLoad(value);
		s_Builder->CreateStore(s_Builder->CreateAdd(loaded, Generator::GetNumericConstant(operand->type->tag, 1)), value);

		// Return newly incremented value
		return s_Builder->CreateLoad(value);
	}
	case UnaryType::PostfixIncrement:
	{
		// Increment
		llvm::Value* loaded = s_Builder->CreateLoad(value);
		s_Builder->CreateStore(s_Builder->CreateAdd(loaded, Generator::GetNumericConstant(operand->type->tag, 1)), value);

		// Return value before increment
		return loaded;
	}
	case UnaryType::PrefixDecrement:
	{
		llvm::Value* loaded = s_Builder->CreateLoad(value);
		s_Builder->CreateStore(s_Builder->CreateSub(loaded, Generator::GetNumericConstant(operand->type->tag, 1)), value);

		return s_Builder->CreateLoad(value);
	}
	case UnaryType::PostfixDecrement:
	{
		llvm::Value* loaded = s_Builder->CreateLoad(value);
		s_Builder->CreateStore(s_Builder->CreateSub(loaded, Generator::GetNumericConstant(operand->type->tag, 1)), value);

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

		return load ? s_Builder->CreateLoad(value) : value;
	}
	}

	ASSERT(false);
}

llvm::Value* Generator::EmitSubscript(BinaryExpression* binary)
{
	PROFILE_FUNCTION();

	llvm::Value* zeroIndex = llvm::ConstantInt::get(*s_Context, llvm::APInt(32, 0, false));

	llvm::Value* indexVal = LoadIfPointer(binary->right->Generate(), binary->right);
	if (!indexVal->getType()->isIntegerTy())
		throw CompileError(binary->sourceLine, "expected integer value for array index");

	llvm::Value* arrayPtr = binary->left->Generate(); // todo: check this shit
	binary->type = binary->left->type->GetContainedType();

	// Should be doing this at the start of this function
	if (!binary->left->type->IsArray())
		throw CompileError(binary->sourceLine, "expected subscript target to be of array type");

	return s_Builder->CreateInBoundsGEP(binary->left->type->raw, arrayPtr, { zeroIndex, indexVal });
}

llvm::Value* VariableDefinitionExpression::Generate()
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
				return array->Generate();
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

				if (compound->type && compound->type->IsArray()) // [...]
				{
					//llvm::Value* initializer = CreateArrayAlloca(compound->type->raw, compound->children);
					//s_CurrentScope->AddValue(name, { type = compound->type, initializer });
					//return initializer;
					return nullptr;
				}

				break;
			}
		}

		if (!aggregateInitialization)
		{
			initialVal = initializer->Generate();
			initialVal = LoadIfPointer(initialVal, initializer.get());

			if (type)
				initialVal = Generator::CastValueIfNecessary(initialVal, initializer->type, type, false, this);
			else
				type = initializer->type;
		}
	}

	// Alloc
	llvm::Value* alloc = s_Builder->CreateAlloca(type->raw);
	s_CurrentScope->AddValue(name, { type, alloc });

	// Initialize members if struct
	if (StructType* structType = type->IsStruct())
	{
		if (aggregateInitialization)
			Generator::InitializeStructMembersAggregate(alloc, structType, aggregateInitializer);
		
		if (!initializer)
			Generator::InitializeStructMembersToDefault(alloc, structType);

		return alloc;
	}

	if (!initialVal)
	{
		if (type->IsPointer())
			initialVal = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type->raw));
		if (type->IsNumeric())
			initialVal = Generator::GetNumericConstant(type->tag, 0);
		if (type->IsArray())
			return alloc;
	}

	s_Builder->CreateStore(initialVal, alloc);
	return alloc;
}

static Type* FindNeoTypeFromLLVMType(llvm::Type* type)
{
	PROFILE_FUNCTION();

	switch (type->getTypeID())
	{
	case llvm::Type::FloatTyID:
		return Type::Get(TypeTag::Float32);
	case llvm::Type::DoubleTyID:
		return Type::Get(TypeTag::Float64);
	case llvm::Type::IntegerTyID:
	{
		llvm::IntegerType* iType = llvm::cast<llvm::IntegerType>(type);
		uint32_t bitWidth = iType->getBitWidth();

		if (bitWidth == 1) return Type::Get(TypeTag::Bool);

		TypeTag tags[] {
			TypeTag::Int8, TypeTag::Int16, TypeTag::Int32, TypeTag::Int64,
		};

		return Type::Get(tags[(uint32_t)log2(bitWidth) - 3]);
	}
	case llvm::Type::PointerTyID:
	{
		Type* contained = FindNeoTypeFromLLVMType(type->getContainedType(0));
		return Type::Get(TypeTag::Pointer, contained);
	}
	case llvm::Type::ArrayTyID:
	{
		Type* contained = FindNeoTypeFromLLVMType(type->getArrayElementType());
		return ArrayType::Get(contained, type->getArrayNumElements());
	}
	case llvm::Type::StructTyID:
	{
		for (auto& pair : StructType::RegisteredTypes)
		{
			if (pair.second->raw == type)
				return pair.second;
		}
		break;
	}
	case llvm::Type::VoidTyID:
		return Type::Get(TypeTag::Void);
	}

	ASSERT(false);
}

static uint32_t GetTypePointerDepth(llvm::Type* type)
{
	uint32_t depth = 0;
	llvm::Type* subtype = type->getContainedType(depth++);
	while (true)
	{
		if (!subtype->isPointerTy())
			break;

		subtype = subtype->getContainedType(0);
		depth++;
	}

	return depth;
}

llvm::Value* Generator::EmitStructureMemberAccess(BinaryExpression* binary)
{
	PROFILE_FUNCTION();
	
	// If left is VariableAccessExpr: objectValue = AllocaInst (ptr)
	// If left is MemberAccess: objectValue = gepinst (ptr)
	llvm::Value* objectValue = binary->left->Generate();
	Type* objectType = FindNeoTypeFromLLVMType(objectValue->getType()->getContainedType(0));
	ASSERT(objectType);

	if (!objectType->IsStruct() && !objectType->IsArray())
	{
		// This is where u would have used -> instead of . (if I wanted that stupid feature)
		if (objectType->IsPointer())
		{
			objectValue = s_Builder->CreateLoad(objectValue);
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
	if (objectType->IsArray() && targetMemberName == "count")
	{
		uint64_t size = objectType->raw->getArrayNumElements();
		binary->type = Type::Get(TypeTag::UInt32);
		return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*s_Context), llvm::APInt(32, size, false));
	}

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
		throw CompileError(binary->sourceLine, "'{}' not a member of struct '{}'", memberExpr->name.c_str(), objectType->GetName().c_str());

	llvm::Value* memberPtr = s_Builder->CreateStructGEP(objectValue, (uint32_t)memberIndex);
	return memberPtr;
}

llvm::Value* VariableAccessExpression::Generate()
{
	Value variable;
	if (!s_CurrentScope->HasValue(name, &variable))
		throw CompileError(sourceLine, "identifier '{}' not declared in scope", name.c_str());

	type = variable.type;
	return variable.raw;
}

llvm::Value* Generator::EmitStore(llvm::Value* value, llvm::Value* ptr)
{
	return s_Builder->CreateStore(value, ptr);
}

llvm::Value* Generator::EmitBinaryOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs)
{
	return s_Builder->CreateBinOp((llvm::Instruction::BinaryOps)op, lhs, rhs);
}

llvm::Value* Generator::EmitComparisonOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs)
{
	return s_Builder->CreateCmp((llvm::CmpInst::Predicate)op, lhs, rhs);
}

llvm::Value* Generator::EmitAlloca(llvm::Type* type, llvm::Value* arraySize)
{
	return s_Builder->CreateAlloca(type, arraySize);
}

llvm::Value* CompoundExpression::Generate()
{
	if (type && type->IsArray()) // array create [...]
		return nullptr;
		//return CreateArrayAlloca(type->raw, children);

	s_CurrentScope = s_CurrentScope->Deepen();

	llvm::BasicBlock* previousBlock = s_Builder->GetInsertBlock();
	llvm::BasicBlock* block = llvm::BasicBlock::Create(*s_Context, "entry", s_CurrentFunction.llvmFunction);
	s_Builder->CreateBr(block);
	s_Builder->SetInsertPoint(block);

	for (auto& expr : children)
		expr->Generate();

	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*s_Context, "end", s_CurrentFunction.llvmFunction);
	s_Builder->CreateBr(endBlock);
	s_Builder->SetInsertPoint(endBlock);

	s_CurrentScope = s_CurrentScope->Increase();

	return block;
}

llvm::Value* BranchExpression::Generate()
{
	PROFILE_FUNCTION();

	// TODO: else if

	llvm::BasicBlock* parentBlock = s_Builder->GetInsertBlock();
	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*s_Context, "brend", s_CurrentFunction.llvmFunction, s_CurrentFunction.returnBlock);

	auto generateBranch = [endBlock, parentBlock](Branch& branch, const char* blockName)
	{
		PROFILE_FUNCTION();

		llvm::BasicBlock* block = llvm::BasicBlock::Create(*s_Context, blockName, s_CurrentFunction.llvmFunction, endBlock);
		s_Builder->SetInsertPoint(block);

		for (auto& expr : branch.body)
			expr->Generate();

		if (!block->getTerminator())
			s_Builder->CreateBr(endBlock);
		s_Builder->SetInsertPoint(parentBlock);

		return block;
	};

	Branch& ifBranch = branches[0];
	llvm::BasicBlock* trueBlock = generateBranch(ifBranch, "btrue"), *falseBlock = nullptr;

	if (branches.size() > 1)
		falseBlock = generateBranch(branches[branches.size() - 1], "bfalse");

	llvm::Value* condition = ifBranch.condition->Generate();
	llvm::BranchInst* branchInst = s_Builder->CreateCondBr(condition, trueBlock, endBlock);
	s_Builder->SetInsertPoint(endBlock);

	return branchInst;
}

llvm::Value* LoopControlFlowExpression::Generate()
{
	// TODO

	return nullptr;
}

llvm::Value* LoopExpression::Generate()
{
	PROFILE_FUNCTION();

	llvm::Type* indexType = llvm::Type::getInt32Ty(*s_Context);
	llvm::Type* arrayType = nullptr;
	llvm::Value* arrayPtr = nullptr;

	Type* iteratorType = range->type;
	llvm::Value* indexValuePtr = s_Builder->CreateAlloca(indexType);
	llvm::Value* maximumIndex = nullptr;
	llvm::Value* iteratorValuePtr = nullptr; // For arrays, the value in the array

	s_CurrentScope = s_CurrentScope->Deepen();

	bool iteratingArray = false;
	BinaryExpression* rangeOperand = nullptr;
	if (!IsRange(range, &rangeOperand))
	{
		arrayPtr = range->Generate();
		arrayType = arrayPtr->getType()->getContainedType(0);

		if (!arrayType->isArrayTy())
			throw CompileError(range->sourceLine, "expected an object of array type to iterate");
		maximumIndex = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*s_Context), llvm::APInt(32, arrayType->getArrayNumElements(), false));

		iteratorType = range->type->GetContainedType();

		// Init iterator
		iteratorValuePtr = s_Builder->CreateAlloca(arrayType->getArrayElementType());

		// gep
		llvm::Value* zeroIndex = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*s_Context), llvm::APInt(32, 0, false));
		llvm::Value* initialValue = s_Builder->CreateInBoundsGEP(arrayType, arrayPtr, { zeroIndex, zeroIndex });
		initialValue = s_Builder->CreateLoad(initialValue);
		s_Builder->CreateStore(initialValue, iteratorValuePtr);

		llvm::Value* zero = llvm::ConstantInt::get(indexType, llvm::APInt(32, 0, false));
		s_Builder->CreateStore(zero, indexValuePtr);

		iteratingArray = true;

		s_CurrentScope->AddValue(iteratorVariableName, { iteratorType, iteratorValuePtr });
	}
	else
	{
		auto minimum = rangeOperand->left->Generate();
		auto maximum = rangeOperand->right->Generate();

		// If reading variable, treat it as underlying value so the compiler does compiler stuff.
		minimum = Generator::LoadValueIfVariable(minimum, rangeOperand->left);
		maximum = Generator::LoadValueIfVariable(maximum, rangeOperand->right);
		maximumIndex = maximum;

		iteratorType = rangeOperand->left->type;
		s_Builder->CreateStore(minimum, indexValuePtr);

		s_CurrentScope->AddValue(iteratorVariableName, { iteratorType, indexValuePtr });
	}

	// Blocks
	llvm::BasicBlock* parentBlock = s_Builder->GetInsertBlock();
	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*s_Context, "for_end", s_CurrentFunction.llvmFunction, s_CurrentFunction.returnBlock);

	llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(*s_Context, "for_cond", s_CurrentFunction.llvmFunction, endBlock);
	llvm::BasicBlock* incrementBlock = llvm::BasicBlock::Create(*s_Context, "for_inc", s_CurrentFunction.llvmFunction, endBlock);
	llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*s_Context, "for_body", s_CurrentFunction.llvmFunction, endBlock);

	// Condition block
	//	If iterator < rangeMax: jump to body block, otherwise jump to end block
	s_Builder->SetInsertPoint(conditionBlock);
	{
		llvm::Value* bShouldContinue = nullptr;

		llvm::Value* iteratorVal = s_Builder->CreateLoad(indexValuePtr);
		llvm::Type* iteratorType = iteratorVal->getType();

		bShouldContinue = s_Builder->CreateICmpSLT(iteratorVal, maximumIndex);

		// TODO: abstract
		//switch (iteratorType->getTypeID())
		//{
		//	case llvm::Type::IntegerTyID:
		//	{
		//		bShouldContinue = builder->CreateICmpSLT(iteratorVal, maximum);
		//		break;
		//	}
		//	case llvm::Type::FloatTyID:
		//	{
		//		bShouldContinue = builder->CreateFCmpULE(iteratorVal, maximum);
		//		break;
		//	}
		//}

		s_Builder->CreateCondBr(bShouldContinue, bodyBlock, endBlock);
	}

	// Increment block:
	//	Increment iterator
	//	Jump to condition block
	s_Builder->SetInsertPoint(incrementBlock);
	{
		llvm::Value* iteratorVal = s_Builder->CreateLoad(indexValuePtr);
		s_Builder->CreateStore(s_Builder->CreateAdd(iteratorVal, Generator::GetNumericConstant(TypeTag::Int32, 1), "inc"), indexValuePtr);
		s_Builder->CreateBr(conditionBlock);
	}

	// Body block:
	//	Body
	//	Jump to increment block
	s_Builder->SetInsertPoint(bodyBlock);
	{
		if (iteratingArray)
		{
			llvm::Value* zeroIndex = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*s_Context), llvm::APInt(32, 0, false));
			llvm::Value* currentElementPtr = s_Builder->CreateInBoundsGEP(arrayType, arrayPtr, { zeroIndex, s_Builder->CreateLoad(indexValuePtr) });
			s_Builder->CreateStore(s_Builder->CreateLoad(currentElementPtr), iteratorValuePtr);
		}

		for (auto& expr : body)
			expr->Generate();

		s_Builder->CreateBr(incrementBlock);
	}
	
	s_Builder->SetInsertPoint(parentBlock);
	s_Builder->CreateBr(conditionBlock);
	s_Builder->SetInsertPoint(endBlock);

	s_CurrentScope = s_CurrentScope->Increase();

	return endBlock;
}

llvm::Value* FunctionDefinitionExpression::Generate()
{
	PROFILE_FUNCTION();

	type = prototype.ReturnType;
	bool hasBody = body.size();
	bool isVoid = type->tag == TypeTag::Void;

	// Full definition for function
	s_CurrentFunction.llvmFunction = s_Module->getFunction(prototype.Name);

	llvm::BasicBlock* previousBlock = s_Builder->GetInsertBlock();
	llvm::BasicBlock* bodyBlock = nullptr;
	
	// Create block, deepen scope
	if (hasBody)
	{
		s_CurrentScope = s_CurrentScope->Deepen(); // No point in scoping args if there's no body

		bodyBlock = llvm::BasicBlock::Create(*s_Context, "entry", s_CurrentFunction.llvmFunction);
		s_Builder->SetInsertPoint(bodyBlock);

		// Allocate return value
		if (!isVoid)
			s_CurrentFunction.returnValueAlloca = s_Builder->CreateAlloca(s_CurrentFunction.llvmFunction->getReturnType());
	}

	s_CurrentFunction.returnBlock = llvm::BasicBlock::Create(*s_Context, "exit", s_CurrentFunction.llvmFunction);

	// Set names for function args
	uint32_t i = 0;
	for (auto& arg : s_CurrentFunction.llvmFunction->args())
	{
		auto& parameter = prototype.Parameters[i++];
		arg.setName(parameter->name);

		if (!hasBody)
			continue;

		// Alloc arg
		llvm::Value* alloc = s_Builder->CreateAlloca(arg.getType());
		s_Builder->CreateStore(&arg, alloc);
		s_CurrentScope->AddValue(parameter->name, { parameter->type, alloc });
	}

	// Gen body
	if (hasBody)
	{
		PROFILE_SCOPE("Generate function body");

		for (auto& node : body)
			node->Generate();

		s_CurrentScope = s_CurrentScope->Increase();

		// Insert terminator
		//s_Builder->CreateBr(s_CurrentFunction.returnBlock);
		s_Builder->SetInsertPoint(s_CurrentFunction.returnBlock);

		if (isVoid)
			s_Builder->CreateRetVoid();
		else
			s_Builder->CreateRet(s_Builder->CreateLoad(s_CurrentFunction.returnValueAlloca));
	}

	{
		PROFILE_SCOPE("Verify function");

		// Handle any errors in the function
		if (llvm::verifyFunction(*s_CurrentFunction.llvmFunction, &llvm::errs()))
		{
			//module->print(llvm::errs(), nullptr);
			s_CurrentFunction.llvmFunction->eraseFromParent();
			throw CompileError(sourceLine, "function verification failed");
		}
	}

	return s_CurrentFunction.llvmFunction;
}

llvm::Value* CastExpression::Generate()
{
	PROFILE_FUNCTION();

	llvm::Value* value = LoadIfPointer(from->Generate(), from);

	bool useTag = false;
	if (type->IsPointer() || from->type->IsPointer())
		useTag = true;

	Cast* cast = useTag ? Cast::IsValid(from->type->tag, type->tag) : Cast::IsValid(from->type, type);
	if (!cast)
		throw CompileError(sourceLine, "cannot cast from '{}' to '{}'", from->type->GetName().c_str(), type->GetName().c_str());

	return cast->Invoke(value, this);
}

llvm::Value* ReturnStatement::Generate()
{
	PROFILE_FUNCTION();

	type = FindNeoTypeFromLLVMType(s_CurrentFunction.llvmFunction->getReturnType());

	if (type->tag == TypeTag::Void)
	{
		s_Builder->CreateBr(s_CurrentFunction.returnBlock);
		return nullptr;
	}

	llvm::Value* returnValue = nullptr;

	// In-place aggregate initialization
	if (CompoundExpression* compound = ToExpr<CompoundExpression>(value))
	{
		if (StructType* structType = type->IsStruct())
		{
			llvm::Value* structPtr = s_Builder->CreateAlloca(type->raw);
			Generator::InitializeStructMembersAggregate(structPtr, structType, compound);

			returnValue = s_Builder->CreateLoad(structPtr);
		}
		if (ArrayType* arrayType = type->IsArray())
		{
			llvm::Value* arrayPtr = Generator::CreateArrayAlloca(arrayType->raw, compound->children);
			returnValue = s_Builder->CreateLoad(arrayPtr);
		}

		ASSERT(returnValue);
	}
	else
	{
		// Everything else
		returnValue = value->Generate();
		returnValue = Generator::CastValueIfNecessary(Generator::LoadValueIfVariable(returnValue, value), value->type, type, false, this);
	}

	s_Builder->CreateStore(returnValue, s_CurrentFunction.returnValueAlloca);
	s_Builder->CreateBr(s_CurrentFunction.returnBlock);

	return returnValue;
}

llvm::Value* FunctionCallExpression::Generate()
{
	PROFILE_FUNCTION();

	llvm::Function* function = s_Module->getFunction(name.c_str());
	if (!function)
		throw CompileError(sourceLine, "undeclared function '{}'", name.c_str());

	if (function->arg_size() != arguments.size())
		throw CompileError(sourceLine, "expected {} arguments for call to '{}' but got {}", function->arg_size(), name.c_str(), arguments.size());
	
	type = FindNeoTypeFromLLVMType(function->getReturnType());

	std::vector<llvm::Value*> argValues;
	uint32_t i = 0;
	for (auto& expr : arguments)
	{
		llvm::Value* value = expr->Generate();
		value = Generator::LoadValueIfVariable(value, expr);

		llvm::Type* expectedTy = function->getArg(i++)->getType();
		value = Generator::CastValueIfNecessary(value, expr->type, FindNeoTypeFromLLVMType(expectedTy), false, expr.get());

		if (expectedTy != value->getType())
			throw CompileError(sourceLine, "invalid type for argument {} passed to '{}'", i - 1, name.c_str());
			
		argValues.push_back(value);
	}

	return s_Builder->CreateCall(function, argValues);
}

static void ResolveType(Type&, int line = -1);

static void ResolvePrimitiveType(Type& type, int possibleSourceLine = -1)
{
	PROFILE_FUNCTION();

	// Already resolved?
	if (type.raw)
		return;

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
		type.raw = llvm::Type::getInt8Ty(*s_Context);
		return;
	}
	case TypeTag::UInt16:
	case TypeTag::Int16:
	{
		type.raw = llvm::Type::getInt16Ty(*s_Context);
		return;
	}
	case TypeTag::UInt32:
	case TypeTag::Int32:
	{
		type.raw = llvm::Type::getInt32Ty(*s_Context);
		return;
	}
	case TypeTag::UInt64:
	case TypeTag::Int64:
	{
		type.raw = llvm::Type::getInt64Ty(*s_Context);
		return;
	}
	case TypeTag::Float32:
	{
		type.raw = llvm::Type::getFloatTy(*s_Context);
		return;
	}
	case TypeTag::Float64:
	{
		type.raw = llvm::Type::getDoubleTy(*s_Context);
		return;
	}
	case TypeTag::Bool:
	{
		type.raw = llvm::Type::getInt1Ty(*s_Context);
		return;
	}
	case TypeTag::String:
	{
		type.raw = llvm::Type::getInt8PtrTy(*s_Context);
		return;
	}
	case TypeTag::Void:
	{
		type.raw = llvm::Type::getVoidTy(*s_Context);
		return;
	}
	}
}

static void ResolveStructType(StructType& type, int possibleSourceLine = -1)
{
	PROFILE_FUNCTION();

	// Already resolved?
	if (type.raw)
		return;

	if (!type.definition)
		throw CompileError(possibleSourceLine, "type '{}' not defined", type.name.c_str());

	auto& members = type.definition->members;

	std::vector<llvm::Type*> memberTypes;
	memberTypes.reserve(members.size());

	for (auto& member : members)
	{
		ResolveType(*member->type, (int)type.definition->sourceLine);
		memberTypes.push_back(member->type->raw);
	}

	type.raw = llvm::StructType::create(*s_Context, memberTypes, type.name);
}

static void ResolveArrayType(ArrayType& type, int possibleSourceLine = -1)
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

static void ResolveType(Type& type, int line)
{
	switch (type.tag)
	{
	case TypeTag::Array:
	{
		ResolveArrayType(*type.IsArray(), line);
		return;
	}
	case TypeTag::Struct:
	{
		ResolveStructType(*type.IsStruct(), line);
		return;
	}
	default:
	{
		ResolvePrimitiveType(type, line);
		return;
	}
	}
}

// todo: store ref to returns in the ast node?
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

// todo: abstract?
static void VisitFunctionDefinitions(ParseResult& result)
{
	PROFILE_FUNCTION();

	// only works for top level functions rn
	for (auto& node : result.module.SyntaxTree->children)
	{
		FunctionDefinitionExpression* definition = nullptr;
		if (!(definition = ToExpr<FunctionDefinitionExpression>(node)))
			continue;

		FunctionPrototype& prototype = definition->prototype;
		if (s_Module->getFunction(prototype.Name))
			throw CompileError(node->sourceLine, "redefinition of function '{}'", prototype.Name.c_str());

		// Param types
		std::vector<llvm::Type*> parameterTypes(prototype.Parameters.size());
		uint32_t i = 0;
		for (auto& param : prototype.Parameters)
			parameterTypes[i++] = param->type->raw;
		i = 0;

		llvm::Type* retType = prototype.ReturnType->raw;
		//llvm::Type* returnTypeFromBody = FindReturnTypeFromBlock(definition->body);
		//retType = returnTypeFromBody;

		llvm::FunctionType* functionType = llvm::FunctionType::get(retType, parameterTypes, false);
		llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, prototype.Name.c_str(), *s_Module);
	}
}

namespace CastFunctions
{
	// Int / int
	static llvm::Value* SInteger_To_SInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Builder->CreateSExtOrTrunc(integer, to);
	}
	static llvm::Value* UInteger_To_UInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Builder->CreateZExtOrTrunc(integer, to);
	}

	static llvm::Value* SInteger_To_UInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return integer;
	}
	static llvm::Value* UInteger_To_SInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return integer;
	}

	// Float / float
	static llvm::Value* F32_To_F64(Cast&, llvm::Value* f32, llvm::Type* to) {
		return s_Builder->CreateFPExt(f32, to);
	}

	static llvm::Value* F64_To_F32(Cast&, llvm::Value* f64, llvm::Type* to) {
		return s_Builder->CreateFPTrunc(f64, to);
	}

	// Integer / floating point
	static llvm::Value* SInteger_To_FP(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Builder->CreateSIToFP(integer, to);
	}
	static llvm::Value* UInteger_To_FP(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Builder->CreateUIToFP(integer, to);
	}
	static llvm::Value* FP_To_SInteger(Cast&, llvm::Value* f32, llvm::Type* to) {
		return s_Builder->CreateFPToSI(f32, to);
	}
	static llvm::Value* FP_To_UInteger(Cast&, llvm::Value* f32, llvm::Type* to) {
		return s_Builder->CreateFPToUI(f32, to);
	}

	// Ptr / int
	static llvm::Value* Pointer_To_Integer(Cast&, llvm::Value* ptr, llvm::Type* to) {
		return s_Builder->CreatePtrToInt(ptr, to);
	}
	static llvm::Value* Integer_To_Pointer(Cast&, llvm::Value* integer, llvm::Type* to) {
		return s_Builder->CreateIntToPtr(integer, to);
	}

	static llvm::Value* Pointer_To_Pointer(Cast&, llvm::Value* ptr, llvm::Type* to) {
		return s_Builder->CreatePointerCast(ptr, to);
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
		Cast::Add(uint, b1, CastFunctions::SInteger_To_SInteger, true);
		Cast::Add(b1, uint, CastFunctions::SInteger_To_SInteger, true);

		// UInt / FP
		for (Type* fp : fpTypes)
		{
			Cast::Add(uint, fp, CastFunctions::UInteger_To_FP, Allow_Int_X_FP_Implicitly);
			Cast::Add(fp, uint, CastFunctions::FP_To_UInteger, Allow_Int_X_FP_Implicitly);
		}
	}
}

static void ResolveParsedTypes(ParseResult& result)
{
	PROFILE_FUNCTION();

	for (auto& expr : result.module.SyntaxTree->children)
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

	VisitFunctionDefinitions(result);
}

Type* Type::GetContainedType() const
{
	ASSERT(contained);
	if (s_Context)
		ResolveType(*contained);

	return contained;
}

Type* Type::GetPointerTo()
{
	Type* pointerTy = Type::Get(TypeTag::Pointer, this);
	if (s_Context)
		ResolveType(*pointerTy);

	return pointerTy;
}

ArrayType* Type::GetArrayTypeOf(uint64_t count)
{
	ArrayType* arrayTy = ArrayType::Get(this, count);
	if (s_Context)
		ResolveArrayType(*arrayTy);

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
//	modulePassManager.run(*s_Module, moduleAnalysisManager);
//}

Generator::Generator()
{
	s_Context = std::make_unique<llvm::LLVMContext>();
	s_Module = std::make_unique<llvm::Module>(llvm::StringRef(), *s_Context);
	s_Builder = std::make_unique<llvm::IRBuilder<>>(*s_Context);
}

CompileResult Generator::Generate(ParseResult& parseResult, const CommandLineArguments& compilerArgs)
{
	PROFILE_FUNCTION();
	
	CompileResult result;

	try
	{
		ResolveParsedTypes(parseResult);
		InitPrimitiveCasts();

		s_CurrentScope = new Scope();
		// Codegen module
		for (auto& node : parseResult.module.SyntaxTree->children)
		{
			node->Generate();
		}

		// Optimizations
		if (compilerArgs.optimizationLevel > 0)
		{
			// optimization level 9000%
			//DoOptimizationPasses(compilerArgs);
		}
		
		// Collect IR to string
		llvm::raw_string_ostream stream(result.ir);
		s_Module->print(stream, nullptr);
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
	s_CurrentScope->AddValue(name, value);

	return true;
}