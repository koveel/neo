#include "pch.h"

#include "Lexer.h"
#include "Tree.h"
#include "Cast.h"
#include "Generator.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>

#include "Scope.h"
#include "PlatformUtils.h"

/* TODO:
Array = instance of array struct, no length needed if []x is a function parameter
Loop control flow (break/continue)

Functions:
	Default parameters
	Overloads
	Var args
	Multiple return values
Enums
Polymorphism
Modules
Reflection
*/

static std::unique_ptr<llvm::LLVMContext> context;
static std::unique_ptr<llvm::IRBuilder<>> builder;
static std::unique_ptr<llvm::Module> module;

static Scope* sCurrentScope = nullptr;
static llvm::Function* sCurrentFunction = nullptr;

template<typename... Args>
static void Warn(int line, const std::string& message, Args&&... args)
{
	SetConsoleColor(14);
	fprintf(stdout, "[line %d] warning: %s\n", line, FormatString(message.c_str(), std::forward<Args>(args)...).c_str());
	ResetConsoleColor();
}

template<typename Expr>
static Expr* To(Expression* expr)
{
	if (expr->nodeType != Expr::GetNodeType())
		return nullptr;

	return static_cast<Expr*>(expr);
}
template<typename Expr>
static Expr* To(std::unique_ptr<Expression>& expr) { return To<Expr>(expr.get()); }

template<typename Expr>
static Expr* To(std::shared_ptr<Expression>&expr) { return To<Expr>(expr.get()); }

static bool IsRange(std::unique_ptr<Expression>& expr, BinaryExpression** outBinary)
{
	if (expr->nodeType != NodeType::Binary)
		return false;

	BinaryExpression* binary = To<BinaryExpression>(expr);
	if (binary->binaryType != BinaryType::Range)
		return false;

	*outBinary = binary;
	return true;
}

static bool IsBinaryCompound(BinaryType type)
{
	switch (type)
	{
		case BinaryType::CompoundAdd:
		case BinaryType::CompoundSub:
		case BinaryType::CompoundMul:
		case BinaryType::CompoundDiv:
			return true;
		default:
			return false;
	}
}

static llvm::Value* LoadIfVariable(llvm::Value* generated, std::unique_ptr<Expression>& expr)
{
	if (auto unary = To<UnaryExpression>(expr))
	{
		if (unary->unaryType == UnaryType::AddressOf) // Don't load it if we're trying to get it's address
			return generated;
	}
	if (auto cast = To<CastExpression>(expr))
	{
		if (cast->type->IsPointer()) // Casting to pointer
			return generated;
	}

	if (!llvm::isa<llvm::AllocaInst>(generated) && !llvm::isa<llvm::LoadInst>(generated) && !llvm::isa<llvm::GetElementPtrInst>(generated))
		return generated;

	return builder->CreateLoad(generated);
}

static llvm::Value* LoadIfPointer(llvm::Value* value, Expression* expr)
{
	llvm::Type* type = value->getType();
	if (auto unary = To<UnaryExpression>(expr))
	{
		if (unary->unaryType == UnaryType::AddressOf) // Don't load it if we're trying to get it's address
			return value;
	}
	if (auto cast = To<CastExpression>(expr))
	{
		if (cast->type->IsPointer()) // Casting to pointer
			return value;
	}

	bool isPointer = type->isPointerTy();
	bool isString = (type->getNumContainedTypes() == 1 && type->getContainedType(0)->isIntegerTy(8)) && expr->type->IsString();

	if (!isPointer || isString)
		return value;

	return builder->CreateLoad(value);
}

static llvm::Value* TryCastIfNecessary(llvm::Value* v, Type* from, Type* to, bool isExplicit, Expression* source)
{
	if (from == to)
		return v;

	Cast* cast = Cast::IsValid(from, to);
	if (!cast)
		throw CompileError(source->sourceLine, "cannot cast from '%s' to '%s'", from->GetName().c_str(), to->GetName().c_str());

	if (isExplicit || cast->implicit)
		return cast->Invoke(v);

	throw CompileError(source->sourceLine, "cannot implicitly cast from '%s' to '%s'", from->GetName().c_str(), to->GetName().c_str());
}

static llvm::Value* LoadIfPointer(llvm::Value* value, std::unique_ptr<Expression>& expr)
{
	return LoadIfPointer(value, expr.get());
}

static uint32_t GetBitWidthOfIntegralType(TypeTag tag)
{
	switch (tag)
	{
		case TypeTag::UInt8:  return 8;
		case TypeTag::Int8:   return 8;
		case TypeTag::UInt16: return 16;
		case TypeTag::Int16:  return 16;
		case TypeTag::UInt32: return 32;
		case TypeTag::Int32:  return 32;
		case TypeTag::UInt64: return 64;
		case TypeTag::Int64:  return 64;
		case TypeTag::Bool:   return 1;
	}

	return 0;
}

static bool IsArithmetic(BinaryType type)
{
	uint32_t num = (uint32_t)type;
	return num >= 1 && num <= 8;
}

static bool IsComparison(BinaryType type)
{
	return type >= BinaryType::Equal && type <= BinaryType::GreaterEqual;
}

static llvm::Value* GetNumericalConstant(llvm::Type* type, uint64_t value = 1)
{
	switch (type->getTypeID())
	{
	case llvm::Type::IntegerTyID:
	{
		bool isSigned = (bool)llvm::cast<llvm::IntegerType>(type)->getSignBit();
		return llvm::ConstantInt::get(*context, llvm::APInt(type->getIntegerBitWidth(), value, isSigned));
	}
	case llvm::Type::FloatTyID:
		return llvm::ConstantFP::get(*context, llvm::APFloat((float)value));
	case llvm::Type::DoubleTyID:
		return llvm::ConstantFP::get(*context, llvm::APFloat((double)value));
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

llvm::Value* StringExpression::Generate()
{
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

	return builder->CreateGlobalStringPtr(stringExpr, "gstr", 0U, module.get());
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
		value = LoadIfVariable(value, operand);
		return builder->CreateNot(value);
	}
	case UnaryType::Negate: // -value
	{
		value = LoadIfVariable(value, operand);

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
		llvm::Value* loaded = builder->CreateLoad(value);
		builder->CreateStore(builder->CreateAdd(loaded, GetNumericalConstant(loaded->getType())), value);

		// Return newly incremented value
		return builder->CreateLoad(value);
	}
	case UnaryType::PostfixIncrement:
	{
		// Increment
		llvm::Value* loaded = builder->CreateLoad(value);
		builder->CreateStore(builder->CreateAdd(loaded, GetNumericalConstant(loaded->getType())), value);

		// Return value before increment
		return loaded;
	}
	case UnaryType::PrefixDecrement:
	{
		llvm::Value* loaded = builder->CreateLoad(value);
		builder->CreateStore(builder->CreateSub(loaded, GetNumericalConstant(loaded->getType())), value);

		return builder->CreateLoad(value);
	}
	case UnaryType::PostfixDecrement:
	{
		llvm::Value* loaded = builder->CreateLoad(value);
		builder->CreateStore(builder->CreateSub(loaded, GetNumericalConstant(loaded->getType())), value);

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
		if (CastExpression* cast = To<CastExpression>(operand))
		{
			if (cast->type->IsPointer())
				load = false;
		}

		return load ? builder->CreateLoad(value) : value;
	}
	}

	ASSERT(false);
}

static llvm::Value* CreateArrayAlloca(llvm::Type* arrayType, const std::vector<std::unique_ptr<Expression>>& elements)
{
	std::vector<llvm::Value*> values;
	values.reserve(elements.size());

	for (auto& expr : elements)
	{
		llvm::Value* element = expr->Generate();
		//if (elementType && (elementType != element->getType()))
		//	//throw CompileError(sourceLine, "expected %s as array element but got %s (index = %d)", )
		//	throw CompileError(sourceLine, "array element type mismatch (index = %ld)", i);

		values.push_back(element);
	}

	llvm::Value* alloc = builder->CreateAlloca(arrayType);
	//i = 0;

	// initialize elements (store into gep)
	uint64_t i = 0;
	llvm::Value* zeroIndex = llvm::ConstantInt::get(*context, llvm::APInt(32, 0, false));
	for (llvm::Value* value : values)
	{
		llvm::Value* index = llvm::ConstantInt::get(*context, llvm::APInt(32, i, false));
		llvm::Value* elementPtr = builder->CreateInBoundsGEP(arrayType, alloc, { zeroIndex, index });
		builder->CreateStore(values[i], elementPtr);

		i++;
	}

	return alloc;
}

llvm::Value* ArrayDefinitionExpression::Generate()
{
	llvm::Value* value = nullptr;
	if (initializer)
	{
		value = initializer->Generate();
		type = initializer->type;
	}
	else
	{
		llvm::Type* arrayTy = llvm::ArrayType::get(type->raw, capacity);
		type->raw = arrayTy;
		value = builder->CreateAlloca(arrayTy);
	}

	sCurrentScope->AddValue(definition->name, { type, value });
	return value;
}

// returns ptr
static llvm::Value* GetArrayElement(llvm::Value* arrayPtr, llvm::Value* index)
{
	static llvm::Value* zeroIndex = llvm::ConstantInt::get(*context, llvm::APInt(32, 0, false));

	llvm::Type* arrayType = arrayPtr->getType()->getContainedType(0);
	return builder->CreateInBoundsGEP(arrayType, arrayPtr, { zeroIndex, index });
}
static llvm::Value* GetArrayElement(llvm::Value* arrayPtr, uint64_t index)
{
	return GetArrayElement(arrayPtr, llvm::ConstantInt::get(*context, llvm::APInt(32, index, false)));
}

//llvm::Value* SubscriptExpression::Generate()
static llvm::Value* GenerateSubscript(BinaryExpression* binary)
{
	//// Array creation, not subscript (until multi-dim arrays?)
	//VariableAccessExpression* variable = nullptr;
	//if (!(variable = To<VariableAccessExpression>(operand))) // identifier ig
	//	throw CompileError(sourceLine, "expected identifier for array element type");
	//
	//Type* elementType = Type::Get(variable->name);
	////llvm::Value* ptr = operand->Generate();

	llvm::Value* zeroIndex = llvm::ConstantInt::get(*context, llvm::APInt(32, 0, false));

	llvm::Value* indexVal = LoadIfPointer(binary->right->Generate(), binary->right);
	if (!indexVal->getType()->isIntegerTy())
		throw CompileError(binary->sourceLine, "expected integer value for array index");

	llvm::Value* arrayPtr = binary->left->Generate(); // todo: check this shit
	binary->type = binary->left->type->GetContainedType();

	// Should be doing this at the start of this function
	if (!binary->left->type->IsArray())
		throw CompileError(binary->sourceLine, "expected subscript target to be of array type");

	return builder->CreateInBoundsGEP(binary->left->type->raw, arrayPtr, { zeroIndex, indexVal });
}

static void DefaultInitializeStructMembers(llvm::Value* structPtr, StructType* structTy);
static void AggregateInitializeStructMembers(llvm::Value* structPtr, StructType* structTy, CompoundStatement* initializer);

llvm::Value* VariableDefinitionExpression::Generate()
{
	llvm::Value* initialVal = nullptr;

	bool aggregateInitialization = false;
	CompoundStatement* aggregateInitializer = nullptr;

	if (initializer)
	{
		switch (initializer->nodeType)
		{
			case NodeType::ArrayDefinition:
			{
				auto array = To<ArrayDefinitionExpression>(initializer);
				return array->Generate();
			}
			case NodeType::Primary:
			{
				auto primary = To<PrimaryExpression>(initializer);
				primary->type = type;
				break;
			}
			case NodeType::Compound:
			{
				// aggregate init
				CompoundStatement* compound = To<CompoundStatement>(initializer);
				aggregateInitializer = compound;
				aggregateInitialization = true;

				if (compound->type && compound->type->IsArray()) // [...]
				{
					llvm::Value* initializer = CreateArrayAlloca(compound->type->raw, compound->children);
					sCurrentScope->AddValue(name, { type = compound->type, initializer });
					return initializer;
				}

				break;
			}
		}

		if (!aggregateInitialization)
		{
			initialVal = initializer->Generate();
			initialVal = LoadIfPointer(initialVal, initializer.get());

			if (type)
				initialVal = TryCastIfNecessary(initialVal, initializer->type, type, false, this);
			else
				type = initializer->type;
		}
	}

	// Alloc
	llvm::Value* alloc = builder->CreateAlloca(type->raw);
	sCurrentScope->AddValue(name, { type, alloc });

	// Initialize members if struct
	if (StructType* structType = type->IsStruct())
	{
		if (aggregateInitialization)
		{
			AggregateInitializeStructMembers(alloc, structType, aggregateInitializer);
			return alloc;
		}
		
		if (!initializer)
			DefaultInitializeStructMembers(alloc, structType);
	}

	if (!initialVal)
	{
		if (type->IsPointer())
			initialVal = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type->raw));
		if (type->IsNumeric())
			initialVal = GetNumericalConstant(type->raw, 0);
		if (type->IsArray())
			return alloc;
	}

	builder->CreateStore(initialVal, alloc);
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
	}
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

static llvm::Value* GenerateStructureMemberAccessExpression(BinaryExpression* binary)
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
			objectValue = builder->CreateLoad(objectValue);
			objectType = objectType->GetContainedType();
		}
		else
			throw CompileError(binary->sourceLine, "can't access member of non-struct type or pointer to struct type");
	}

	// rhs should always be variable access expr
	VariableAccessExpression* memberExpr = nullptr;
	if (!(memberExpr = To<VariableAccessExpression>(binary->right)))
		throw CompileError(binary->sourceLine, "expected variable access expression for rhs of member access");

	const std::string& targetMemberName = memberExpr->name;
	
	// epic hardcoded array.size
	if (objectType->IsArray() && targetMemberName == "count")
	{
		uint64_t size = objectType->raw->getArrayNumElements();
		return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), llvm::APInt(32, size, false));
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
		throw CompileError(binary->sourceLine, "'%s' not a member of struct '%s'", memberExpr->name.c_str(), objectType->GetName().c_str());

	llvm::Value* memberPtr = builder->CreateStructGEP(objectValue, (uint32_t)memberIndex);
	return memberPtr;
}

llvm::Value* VariableAccessExpression::Generate()
{
	Value variable;
	if (!sCurrentScope->HasValue(name, &variable))
		throw CompileError(sourceLine, "identifier '%s' not declared in scope", name.c_str());

	type = variable.type;
	return variable.raw;
}

// Kinda wack
static llvm::Instruction::BinaryOps LLVMBinaryInstructionFromBinary(BinaryExpression* binary)
{
	using namespace llvm;

	ASSERT(IsArithmetic(binary->binaryType));
	
	static constexpr std::array<Instruction::BinaryOps, 4> instructions[] =
	{
		{
			Instruction::BinaryOps::FAdd, Instruction::BinaryOps::FSub,
			Instruction::BinaryOps::FMul, Instruction::BinaryOps::FDiv,
		},
		{
			Instruction::BinaryOps::Add, Instruction::BinaryOps::Sub,
			Instruction::BinaryOps::Mul, Instruction::BinaryOps::SDiv,
		},
		{
			Instruction::BinaryOps::Add, Instruction::BinaryOps::Sub,
			Instruction::BinaryOps::Mul, Instruction::BinaryOps::UDiv,
		}
	};

	const auto& lhsType = binary->left->type;
	uint32_t instIdx = lhsType->IsFloatingPoint() ? 0 : (lhsType->IsSigned() ? 1 : 2);

	uint32_t num = (uint32_t)binary->binaryType;
	uint32_t index = (num % 2 == 0 ? (num / 2 - 1) : ((num - 1) / 2)); // Probably the worst line of code ive ever written

	return instructions[instIdx][index];
}

// also wack
static llvm::CmpInst::Predicate LLVMCmpInstructionFromBinary(BinaryExpression* binary)
{
	using namespace llvm;

	ASSERT(IsComparison(binary->binaryType));

	static constexpr std::array<CmpInst::Predicate, 6> predicates[] =
	{
		{
			CmpInst::Predicate::ICMP_EQ,  CmpInst::Predicate::ICMP_NE,  // =, !=
			CmpInst::Predicate::ICMP_ULT, CmpInst::Predicate::ICMP_ULE, // <, <=
			CmpInst::Predicate::ICMP_UGT, CmpInst::Predicate::ICMP_UGE, // >, >=
		},
		{
			CmpInst::Predicate::ICMP_EQ,  CmpInst::Predicate::ICMP_NE,  // =, !=
			CmpInst::Predicate::ICMP_SLT, CmpInst::Predicate::ICMP_SLE, // <, <=
			CmpInst::Predicate::ICMP_SGT, CmpInst::Predicate::ICMP_SGE, // >, >=
		},
		{
			CmpInst::Predicate::FCMP_UEQ, CmpInst::Predicate::FCMP_UNE, // =, !=
			CmpInst::Predicate::FCMP_ULT, CmpInst::Predicate::FCMP_ULE, // <, <=
			CmpInst::Predicate::FCMP_UGT, CmpInst::Predicate::FCMP_UGE, // >, >=
		}
	};

	::Type* lhsType = binary->left->type;
	uint32_t predIdx = lhsType->IsFloatingPoint() ? 0 : (lhsType->IsSigned() ? 1 : 2);

	auto& preds = predicates[predIdx];
	uint32_t index = (uint32_t)binary->binaryType % 10;
	ASSERT(index >= 0 && index < preds.size());

	return preds[index];
}

llvm::Value* BinaryExpression::Generate()
{
	PROFILE_FUNCTION();

	using namespace llvm;

	if (binaryType == BinaryType::MemberAccess)
		return GenerateStructureMemberAccessExpression(this);
	if (binaryType == BinaryType::Subscript)
		return GenerateSubscript(this);

	llvm::Value* lhs = left->Generate();
	llvm::Value* rhs = right->Generate();
	 
	// Type checking

	llvm::CmpInst::Predicate comparePredicate = {};
	if (IsComparison(binaryType))
		comparePredicate = LLVMCmpInstructionFromBinary(this);

	type = left->type;

	llvm::Value* unloadedLhs = lhs;
	// Unless assiging, treat variables as the underlying values
	if (binaryType != BinaryType::Assign)
	{
		lhs = LoadIfVariable(lhs, left);
	}
	rhs = LoadIfVariable(rhs, right);

	if (left->type != right->type)
	{
		rhs = TryCastIfNecessary(rhs, right->type, left->type, false, this);
	}

	llvm::Type* lhsType = lhs->getType();
	llvm::Type* rhsType = rhs->getType();

	Instruction::BinaryOps instruction = (Instruction::BinaryOps)-1;
	switch (binaryType)
	{
	case BinaryType::CompoundAdd:
	case BinaryType::Add:	
	case BinaryType::CompoundSub:
	case BinaryType::Subtract:
	case BinaryType::CompoundMul:
	case BinaryType::Multiply:
	case BinaryType::CompoundDiv:
	case BinaryType::Divide:
	{
		instruction = LLVMBinaryInstructionFromBinary(this);
		break;
	}
	case BinaryType::Assign:
	{
		builder->CreateStore(rhs, lhs);
		return rhs;
	}
	case BinaryType::Equal:
	case BinaryType::NotEqual:
	case BinaryType::Less:
	case BinaryType::LessEqual:
	case BinaryType::Greater:
	case BinaryType::GreaterEqual:
		return builder->CreateCmp(comparePredicate, lhs, rhs, "cmp");
	default:
		ASSERT(false);
	}

	llvm::Value* result = builder->CreateBinOp(instruction, lhs, rhs);

	if (IsBinaryCompound(binaryType))
		builder->CreateStore(result, unloadedLhs);

	return result;
}

llvm::Value* CompoundStatement::Generate()
{
	if (type && type->IsArray()) // array create [...]
		return CreateArrayAlloca(type->raw, children);

	sCurrentScope = sCurrentScope->Deepen();

	llvm::BasicBlock* previousBlock = builder->GetInsertBlock();
	llvm::BasicBlock* block = llvm::BasicBlock::Create(*context, "entry", sCurrentFunction);
	builder->CreateBr(block);
	builder->SetInsertPoint(block);

	for (auto& expr : children)
		expr->Generate();

	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "end", sCurrentFunction);
	builder->CreateBr(endBlock);
	builder->SetInsertPoint(endBlock);

	sCurrentScope = sCurrentScope->Increase();

	return block;
}

llvm::Value* BranchExpression::Generate()
{
	PROFILE_FUNCTION();

	// TODO: else if

	llvm::BasicBlock* parentBlock = builder->GetInsertBlock();
	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "end", sCurrentFunction);

	auto generateBranch = [endBlock, parentBlock](Branch& branch, const char* blockName)
	{
		PROFILE_FUNCTION();

		llvm::BasicBlock* block = llvm::BasicBlock::Create(*context, blockName, sCurrentFunction, endBlock);
		builder->SetInsertPoint(block);

		for (auto& expr : branch.body)
			expr->Generate();

		builder->CreateBr(endBlock);
		builder->SetInsertPoint(parentBlock);

		return block;
	};

	Branch& ifBranch = branches[0];
	llvm::BasicBlock* trueBlock = generateBranch(ifBranch, "btrue"), *falseBlock = nullptr;

	if (branches.size() > 1)
		falseBlock = generateBranch(branches[branches.size() - 1], "bfalse");

	llvm::Value* condition = ifBranch.condition->Generate();
	llvm::BranchInst* branchInst = builder->CreateCondBr(condition, trueBlock, endBlock);
	builder->SetInsertPoint(endBlock);

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

	llvm::Type* indexType = llvm::Type::getInt32Ty(*context);
	llvm::Type* arrayType = nullptr;
	llvm::Value* arrayPtr = nullptr;

	Type* iteratorType = range->type;
	llvm::Value* indexValuePtr = builder->CreateAlloca(indexType);
	llvm::Value* maximumIndex = nullptr;
	llvm::Value* iteratorValuePtr = nullptr; // For arrays, the value in the array

	sCurrentScope = sCurrentScope->Deepen();

	bool iteratingArray = false;
	BinaryExpression* rangeOperand = nullptr;
	if (!IsRange(range, &rangeOperand))
	{
		arrayPtr = range->Generate();
		arrayType = arrayPtr->getType()->getContainedType(0);

		if (!arrayType->isArrayTy())
			throw CompileError(range->sourceLine, "expected an object of array type to iterate");
		maximumIndex = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), llvm::APInt(32, arrayType->getArrayNumElements(), false));

		iteratorType = range->type->GetContainedType();

		// Init iterator
		iteratorValuePtr = builder->CreateAlloca(arrayType->getArrayElementType());

		// gep
		llvm::Value* zeroIndex = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), llvm::APInt(32, 0, false));
		llvm::Value* initialValue = builder->CreateInBoundsGEP(arrayType, arrayPtr, { zeroIndex, zeroIndex });
		initialValue = builder->CreateLoad(initialValue);
		builder->CreateStore(initialValue, iteratorValuePtr);

		llvm::Value* zero = llvm::ConstantInt::get(indexType, llvm::APInt(32, 0, false));
		builder->CreateStore(zero, indexValuePtr);

		iteratingArray = true;

		sCurrentScope->AddValue(iteratorVariableName, { iteratorType, iteratorValuePtr });
	}
	else
	{
		auto minimum = rangeOperand->left->Generate();
		auto maximum = rangeOperand->right->Generate();

		// If reading variable, treat it as underlying value so the compiler does compiler stuff.
		minimum = LoadIfVariable(minimum, rangeOperand->left);
		maximum = LoadIfVariable(maximum, rangeOperand->right);
		maximumIndex = maximum;

		iteratorType = rangeOperand->left->type;
		builder->CreateStore(minimum, indexValuePtr);

		sCurrentScope->AddValue(iteratorVariableName, { iteratorType, indexValuePtr });
	}


	// Blocks
	llvm::BasicBlock* parentBlock = builder->GetInsertBlock();
	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "for_end", sCurrentFunction);

	llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(*context, "for_cond", sCurrentFunction, endBlock);
	llvm::BasicBlock* incrementBlock = llvm::BasicBlock::Create(*context, "for_inc", sCurrentFunction, endBlock);
	llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "for_body", sCurrentFunction, endBlock);

	// Condition block
	//	If iterator < rangeMax: jump to body block, otherwise jump to end block
	builder->SetInsertPoint(conditionBlock);
	{
		llvm::Value* bShouldContinue = nullptr;

		llvm::Value* iteratorVal = builder->CreateLoad(indexValuePtr);
		llvm::Type* iteratorType = iteratorVal->getType();

		bShouldContinue = builder->CreateICmpSLT(iteratorVal, maximumIndex);

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

		builder->CreateCondBr(bShouldContinue, bodyBlock, endBlock);
	}

	// Increment block:
	//	Increment iterator
	//	Jump to condition block
	builder->SetInsertPoint(incrementBlock);
	{
		llvm::Value* iteratorVal = builder->CreateLoad(indexValuePtr);
		builder->CreateStore(builder->CreateAdd(iteratorVal, GetNumericalConstant(iteratorVal->getType()), "inc"), indexValuePtr);
		builder->CreateBr(conditionBlock);
	}

	// Body block:
	//	Body
	//	Jump to increment block
	builder->SetInsertPoint(bodyBlock);
	{
		if (iteratingArray)
		{
			llvm::Value* zeroIndex = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), llvm::APInt(32, 0, false));
			llvm::Value* currentElementPtr = builder->CreateInBoundsGEP(arrayType, arrayPtr, { zeroIndex, builder->CreateLoad(indexValuePtr) });
			builder->CreateStore(builder->CreateLoad(currentElementPtr), iteratorValuePtr);
		}

		for (auto& expr : body)
			expr->Generate();

		builder->CreateBr(incrementBlock);
	}
	
	builder->SetInsertPoint(parentBlock);
	builder->CreateBr(conditionBlock);
	builder->SetInsertPoint(endBlock);

	sCurrentScope = sCurrentScope->Increase();

	return endBlock;
}

llvm::Value* FunctionDefinitionExpression::Generate()
{
	PROFILE_FUNCTION();

	type = prototype.ReturnType;
	bool hasBody = body.size();
	
	// Full definition for function
	sCurrentFunction = module->getFunction(prototype.Name);

	llvm::BasicBlock* previousBlock = builder->GetInsertBlock();
	llvm::BasicBlock* bodyBlock = nullptr;

	// Create block, deepen scope
	if (hasBody)
	{
		sCurrentScope = sCurrentScope->Deepen(); // No point in scoping args if there's no body

		bodyBlock = llvm::BasicBlock::Create(*context, "entry", sCurrentFunction);
		builder->SetInsertPoint(bodyBlock);
	}

	// Set names for function args
	uint32_t i = 0;
	for (auto& arg : sCurrentFunction->args())
	{
		auto& parameter = prototype.Parameters[i++];
		arg.setName(parameter->name);

		if (!hasBody)
			continue;

		// Alloc arg
		llvm::Value* alloc = builder->CreateAlloca(arg.getType());
		builder->CreateStore(&arg, alloc);
		sCurrentScope->AddValue(parameter->name, { parameter->type, alloc });
	}

	// Gen body
	if (hasBody)
	{
		PROFILE_SCOPE("Generate function body");

		for (auto& node : body)
		{
			// todo: fix
			// avoids llvm complaining about return in middle of block if there are multiple returns that would be hit
			// also means an error wont be flagged after the first return so kinda dumb rn
			if (llvm::isa<llvm::ReturnInst>(node->Generate()))
				break;
		}

		//bodyBlock = builder->GetInsertBlock();
		if (!bodyBlock->getTerminator())
		{
			if (type->tag != TypeTag::Void)
				throw CompileError(sourceLine, "expected return statement in function '%s'", prototype.Name.c_str());

			builder->CreateRet(nullptr);
		}

		sCurrentScope = sCurrentScope->Increase();
		builder->SetInsertPoint(previousBlock);
	}

	{
		PROFILE_SCOPE("Verify function");

		// Handle any errors in the function
		if (llvm::verifyFunction(*sCurrentFunction, &llvm::errs()))
		{
			//module->print(llvm::errs(), nullptr);
			sCurrentFunction->eraseFromParent();
			throw CompileError(sourceLine, "function verification failed");
		}
	}

	return sCurrentFunction;
}

llvm::Value* CastExpression::Generate()
{
	llvm::Value* value = LoadIfPointer(from->Generate(), from);

	bool useTag = false;
	if (type->IsPointer() || from->type->IsPointer())
		useTag = true;

	Cast* cast = useTag ? Cast::IsValid(from->type->tag, type->tag) : Cast::IsValid(from->type, type);
	if (!cast)
		throw CompileError(sourceLine, "cannot cast from '%s' to '%s'", from->type->GetName().c_str(), type->GetName().c_str());

	return cast->Invoke(value, this);
}

llvm::Value* ReturnStatement::Generate()
{
	type = FindNeoTypeFromLLVMType(sCurrentFunction->getReturnType());
	// In-place aggregate initialization
	if (CompoundStatement* compound = To<CompoundStatement>(value))
	{
		//type = FindNeoTypeFromLLVMType(sCurrentFunction->getReturnType());

		llvm::Value* result = nullptr;
		if (StructType* structType = type->IsStruct())
		{
			llvm::Value* structPtr = builder->CreateAlloca(type->raw);
			AggregateInitializeStructMembers(structPtr, structType, compound);

			result = builder->CreateLoad(structPtr);
		}
		if (ArrayType* arrayType = type->IsArray())
		{
			llvm::Value* arrayPtr = CreateArrayAlloca(arrayType->raw, compound->children);
			result = builder->CreateLoad(arrayPtr);
		}
		ASSERT(result);

		return builder->CreateRet(result);
	}

	// Everything else
	llvm::Value* generated = value->Generate();
	generated = TryCastIfNecessary(LoadIfVariable(generated, value), value->type, type, false, this);

	return builder->CreateRet(generated);
}

llvm::Value* FunctionCallExpression::Generate()
{
	PROFILE_FUNCTION();

	llvm::Function* function = module->getFunction(name.c_str());
	if (!function)
		throw CompileError(sourceLine, "undeclared function '%s'", name.c_str());

	if (function->arg_size() != arguments.size())
		throw CompileError(sourceLine, "expected %d arguments for call to '%s' but got %d", function->arg_size(), name.c_str(), arguments.size());
	
	type = FindNeoTypeFromLLVMType(function->getReturnType());

	std::vector<llvm::Value*> argValues;
	uint32_t i = 0;
	for (auto& expr : arguments)
	{
		llvm::Value* value = expr->Generate();
		value = LoadIfVariable(value, expr);

		llvm::Type* expectedTy = function->getArg(i++)->getType();
		value = TryCastIfNecessary(value, expr->type, FindNeoTypeFromLLVMType(expectedTy), false, expr.get());

		if (expectedTy != value->getType())
			throw CompileError(sourceLine, "invalid type for argument %d passed to '%s'", i - 1, name.c_str());
			
		argValues.push_back(value);
	}

	return builder->CreateCall(function, argValues);
}

static void DefaultInitializeStructMembers(llvm::Value* structPtr, StructType* type)
{
	PROFILE_FUNCTION();

	StructDefinitionExpression* definition = type->definition;

	uint32_t i = 0;
	for (auto& member : definition->members)
	{
		auto& init = member->initializer;
		if (!init || init->nodeType == NodeType::ArrayDefinition || init->nodeType == NodeType::ArrayInitialize) // TODO: default initialize primitive types
			continue;

		llvm::Value* initialValue = init->Generate();
		llvm::Value* memberPtr = builder->CreateStructGEP(structPtr, i++);
		builder->CreateStore(initialValue, memberPtr);
	}
}

static uint32_t GetIndexOfMemberInStruct(const std::string& targetMember, StructType* type)
{
	StructDefinitionExpression* definition = type->definition;

	uint32_t i = 0;
	for (auto& member : definition->members)
	{
		if (targetMember == member->name)
			return i;

		i++;
	}

	return std::numeric_limits<uint32_t>::max();
}

// TODO: cleanup ?
static void AggregateInitializeStructMembers(llvm::Value* structPtr, StructType* type, CompoundStatement* initializer)
{
	PROFILE_FUNCTION();

	StructDefinitionExpression* definition = type->definition;

	std::vector<uint32_t> initializedMembers;
	initializedMembers.reserve(initializer->children.size());

	bool usedNamedInitialization = false;
	uint32_t i = 0;
	for (auto& expr : initializer->children)
	{
		if (auto binary = To<BinaryExpression>(expr))
		{
			if (binary->binaryType != BinaryType::Assign)
				throw CompileError(expr->sourceLine, "expected binary assignment expression");

			// Why tf is concise binary a binary
			auto concise = To<BinaryExpression>(binary->left);
			ASSERT(concise);

			if (concise->binaryType != BinaryType::ConciseMemberAccess)
				throw CompileError(concise->sourceLine, "expected concise member access \".member\" for lhs of initializer");

			VariableAccessExpression* variable = To<VariableAccessExpression>(concise->right);
			ASSERT(variable);

			uint32_t memberIndex = GetIndexOfMemberInStruct(variable->name, type);
			if (memberIndex == std::numeric_limits<uint32_t>::max())
				throw CompileError(expr->sourceLine, "member '%s' doesn't exist in struct '%s'", variable->name.c_str(), type->GetName().c_str());

			if (std::find(initializedMembers.begin(), initializedMembers.end(), memberIndex) != initializedMembers.end())
				throw CompileError(expr->sourceLine, "member '%s' appears multiple times in aggregate initializer. can only assign to it once", variable->name.c_str());
			initializedMembers.push_back(memberIndex);

			llvm::Value* value = LoadIfVariable(binary->right->Generate(), binary->right);
			llvm::Value* memberPtr = builder->CreateStructGEP(structPtr, memberIndex);

			value = TryCastIfNecessary(value, binary->right->type, type->members[memberIndex], false, binary->right.get());

			builder->CreateStore(value, memberPtr);

			usedNamedInitialization = true;
		}
		else
		{
			if (usedNamedInitialization)
				throw CompileError(expr->sourceLine, "if using named initialization \".member = x\", must use it for all subsequent initializations");

			llvm::Value* value = LoadIfVariable(expr->Generate(), expr);
			uint32_t memberIndex = i++;

			value = TryCastIfNecessary(value, expr->type, type->members[memberIndex], false, expr.get());
			llvm::Value* memberPtr = builder->CreateStructGEP(structPtr, memberIndex);
			builder->CreateStore(value, memberPtr);
		}
	}
}

llvm::Value* StructDefinitionExpression::Generate()
{
	// We already resolved the struct type and its members but here we just flag an error if a member type if unresolved
	
	for (auto& vardef : members)
	{
		Type* memberType = vardef->type;
		if (memberType->tag != TypeTag::Unresolved)
			continue;

		throw CompileError(vardef->sourceLine, "unresolved type '%s' for member '%s' in struct '%s'",
			memberType->GetName().c_str(), vardef->name.c_str(), name.c_str());
	}

	return nullptr;
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
		type.raw = llvm::Type::getInt8PtrTy(*context);
		return;
	}
	case TypeTag::Void:
	{
		type.raw = llvm::Type::getVoidTy(*context);
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
		throw CompileError(possibleSourceLine, "type '%s' not defined", type.name.c_str());

	auto& members = type.definition->members;

	std::vector<llvm::Type*> memberTypes;
	memberTypes.reserve(members.size());

	for (auto& member : members)
	{
		ResolveType(*member->type, (int)type.definition->sourceLine);
		memberTypes.push_back(member->type->raw);
	}

	type.raw = llvm::StructType::create(*context, memberTypes, type.name);
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

	//throw CompileError(possibleSourceLine, "unresolved type %s", type.GetName().c_str());
}

// todo: store ref to returns in the ast node?
static llvm::Type* FindReturnTypeFromBlock(std::vector<std::unique_ptr<Expression>>& block)
{
	for (auto& expr : block)
	{
		if (ReturnStatement* ret = To<ReturnStatement>(expr))
		{
			return ret->type->raw;
		}

		if (CompoundStatement* compound = To<CompoundStatement>(expr))
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
	for (auto& node : result.Module->children)
	{
		FunctionDefinitionExpression* definition = nullptr;
		if (!(definition = To<FunctionDefinitionExpression>(node)))
			continue;

		FunctionPrototype& prototype = definition->prototype;
		if (module->getFunction(prototype.Name))
			throw CompileError(node->sourceLine, "redefinition of function '%s'", prototype.Name.c_str());

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
		llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, prototype.Name.c_str(), *module);
	}
}

namespace CastFunctions
{
	// Int / int
	static llvm::Value* SInteger_To_SInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return builder->CreateSExtOrTrunc(integer, to);
	}
	static llvm::Value* UInteger_To_UInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return builder->CreateZExtOrTrunc(integer, to);
	}

	static llvm::Value* SInteger_To_UInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return integer;
	}
	static llvm::Value* UInteger_To_SInteger(Cast&, llvm::Value* integer, llvm::Type* to) {
		return integer;
	}

	// Float / float
	static llvm::Value* F32_To_F64(Cast&, llvm::Value* f32, llvm::Type* to) {
		return builder->CreateFPExt(f32, to);
	}

	static llvm::Value* F64_To_F32(Cast&, llvm::Value* f64, llvm::Type* to) {
		return builder->CreateFPTrunc(f64, to);
	}

	// Integer / floating point
	static llvm::Value* SInteger_To_FP(Cast&, llvm::Value* integer, llvm::Type* to) {
		return builder->CreateSIToFP(integer, to);
	}
	static llvm::Value* UInteger_To_FP(Cast&, llvm::Value* integer, llvm::Type* to) {
		return builder->CreateUIToFP(integer, to);
	}
	static llvm::Value* FP_To_SInteger(Cast&, llvm::Value* f32, llvm::Type* to) {
		return builder->CreateFPToSI(f32, to);
	}
	static llvm::Value* FP_To_UInteger(Cast&, llvm::Value* f32, llvm::Type* to) {
		return builder->CreateFPToUI(f32, to);
	}

	// Ptr / int
	static llvm::Value* Pointer_To_Integer(Cast&, llvm::Value* ptr, llvm::Type* to) {
		return builder->CreatePtrToInt(ptr, to);
	}
	static llvm::Value* Integer_To_Pointer(Cast&, llvm::Value* integer, llvm::Type* to) {
		return builder->CreateIntToPtr(integer, to);
	}

	static llvm::Value* Pointer_To_Pointer(Cast&, llvm::Value* ptr, llvm::Type* to) {
		return builder->CreatePointerCast(ptr, to);
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

	for (auto& expr : result.Module->children)
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
	if (context)
		ResolveType(*contained);

	return contained;
}

Type* Type::GetPointerTo()
{
	Type* pointerTy = Type::Get(TypeTag::Pointer, this);
	if (context)
		ResolveType(*pointerTy);

	return pointerTy;
}

ArrayType* Type::GetArrayTypeOf(uint64_t count)
{
	ArrayType* arrayTy = ArrayType::Get(this, count);
	if (context)
		ResolveArrayType(*arrayTy);

	return arrayTy;
}

static void DoOptimizationPasses(const CommandLineArguments& compilerArgs)
{
	PROFILE_FUNCTION();

	using namespace llvm;

	PassBuilder passBuilder;

	FunctionPassManager fpm;
	LoopAnalysisManager loopAnalysisManager;
	FunctionAnalysisManager functionAnalysisManager;
	CGSCCAnalysisManager cgsccAnalysisManager;
	ModuleAnalysisManager moduleAnalysisManager;

	// Register all the basic analyses with the managers
	passBuilder.registerModuleAnalyses(moduleAnalysisManager);
	passBuilder.registerCGSCCAnalyses(cgsccAnalysisManager);
	passBuilder.registerFunctionAnalyses(functionAnalysisManager);
	passBuilder.registerLoopAnalyses(loopAnalysisManager);
	passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager, cgsccAnalysisManager, moduleAnalysisManager);

	PassBuilder::OptimizationLevel optLevel;
	switch (compilerArgs.optimizationLevel)
	{
		case 0: optLevel = llvm::PassBuilder::OptimizationLevel::O0; break;
		case 1: optLevel = llvm::PassBuilder::OptimizationLevel::O1; break;
		case 2: optLevel = llvm::PassBuilder::OptimizationLevel::O2; break;
		case 3: optLevel = llvm::PassBuilder::OptimizationLevel::O3; break;
	}

	ModulePassManager modulePassManager = passBuilder.buildPerModuleDefaultPipeline(optLevel);

	// Hells ya
	modulePassManager.run(*module, moduleAnalysisManager);
}

Generator::Generator()
{
	context = std::make_unique<llvm::LLVMContext>();
	module = std::make_unique<llvm::Module>(llvm::StringRef(), *context);
	builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

CompileResult Generator::Generate(ParseResult& parseResult, const CommandLineArguments& compilerArgs)
{
	PROFILE_FUNCTION();
	
	CompileResult result;

	try
	{
		ResolveParsedTypes(parseResult);
		InitPrimitiveCasts();

		sCurrentScope = new Scope();
		// Codegen module
		for (auto& node : parseResult.Module->children)
		{
			node->Generate();
		}

		// Optimizations
		if (compilerArgs.optimizationLevel > 0)
		{
			// optimization level 9000%
			DoOptimizationPasses(compilerArgs);
		}
		
		// Collect IR to string
		llvm::raw_string_ostream stream(result.ir);
		module->print(stream, nullptr);
		result.ir = stream.str();

		result.Succeeded = true;
	}
	catch (const CompileError& err)
	{
		result.Succeeded = false;
		SetConsoleColor(12);
		if (err.line == -1)
			fprintf(stderr, "error: %s\n", err.message.c_str());
		else
			fprintf(stderr, "[line %d] error: %s\n", err.line, err.message.c_str());
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