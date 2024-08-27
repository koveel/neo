#include "pch.h"

#include "Tree.h"
#include "Generator.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils.h"

#include "Scope.h"
#include "PlatformUtils.h"


static std::unique_ptr<llvm::LLVMContext> context;
static std::unique_ptr<llvm::IRBuilder<>> builder;
static std::unique_ptr<llvm::Module> module;

#define Assert(cond, line, msg, ...) { if (!(cond)) { throw CompileError(line, msg, __VA_ARGS__); } }
#define SoftAssert(cond, line, msg, ...) { if (!(cond)) { Warn(line, msg, __VA_ARGS__); } }

template<typename... Args>
static void Warn(int line, const std::string& message, Args&&... args)
{
	SetConsoleColor(14);
	fprintf(stdout, "warning (line %d): %s\n", line, FormatString(message.c_str(), std::forward<Args>(args)...).c_str());
	ResetConsoleColor();
}

#if 0

#pragma region Casts

static llvm::Value* Int32ToInt64(llvm::Value* int32Value)
{
	return builder->CreateSExt(int32Value, Type::int64Type->raw, "sexttmp");
}
static llvm::Value* Int64ToInt32(llvm::Value* int64Value)
{
	return builder->CreateTrunc(int64Value, Type::int32Type->raw, "trunctmp");
}

static llvm::Value* F32ToF64(llvm::Value* f32Value)
{
	return builder->CreateFPExt(f32Value, Type::float64Type->raw, "fpexttmp");
}
static llvm::Value* F64ToF32(llvm::Value* f64Value)
{
	return builder->CreateFPTrunc(f64Value, Type::float32Type->raw, "fptrunctmp");
}

#pragma endregion

std::vector<CastInst> Caster::allowedImplicitCasts;

struct NamedValue
{
	llvm::Value* raw = nullptr;
	llvm::Type* type = nullptr;
	VariableDefinition::Modifiers modifiers;
};

static std::unordered_map<std::string, NamedValue> namedValues;
static std::unordered_map<llvm::StructType*, std::vector<std::string>> structureTypeMemberNames;

// Removes everything from namedValues that is not a global variable
static void ResetStackValues()
{
	PROFILE_FUNCTION();

	for (auto it = namedValues.begin(); it != namedValues.end(); )
	{
		if (!it->second.modifiers.isGlobal)
		{
			namedValues.erase(it++);
			continue;
		}

		++it;
	}
}

static bool IsUnaryDereference(const std::unique_ptr<Expression>& expr)
{
	bool isUnary = expr->statementType == StatementType::UnaryExpr;
	if (!isUnary)
		return false;

	return static_cast<Unary*>(expr.get())->unaryType == UnaryType::Deref;
}

static bool IsPointer(const llvm::Type* const type) { return type->isPointerTy(); }
static bool IsSinglePointer(const llvm::Type* const type) { return type->getNumContainedTypes() == 1; }

static bool IsNumeric(llvm::Type* type)
{
	llvm::Type* actualType = type;
	if (actualType == llvm::Type::getInt1Ty(*context))
		return false; // Bool isn't numeric

	// Return value types
	if (actualType->isFunctionTy())
		actualType = ((llvm::FunctionType*)type)->getReturnType();

	return actualType->isIntegerTy() ||
		actualType->isFloatingPointTy();
}

llvm::Value* PrimaryValue::Generate()
{
	PROFILE_FUNCTION();

	if (type->isInt32())
		return llvm::ConstantInt::get(*context, llvm::APInt(32, (int32_t)value.i64));
	if (type->isInt64())
		return llvm::ConstantInt::get(*context, llvm::APInt(64, value.i64));
	if (type->isFloat32())
		return llvm::ConstantFP::get(*context, llvm::APFloat((float)value.f64));
	if (type->isFloat64())
		return llvm::ConstantFP::get(*context, llvm::APFloat(value.f64));
	if (type->isBool())
		return llvm::ConstantInt::getBool(*context, value.b32);

	throw CompileError(line, "invalid type for primary value");
}

llvm::Value* StringValue::Generate()
{
	return builder->CreateGlobalStringPtr(value, "gstrtmp", 0U, module.get());
}

llvm::Value* VariableDefinition::Generate()
{
	using namespace llvm;
	
	PROFILE_FUNCTION();

	Assert(!namedValues.count(name), line, "variable '%s' already defined", name.c_str());
	Assert(type->raw, line, "unresolved type for variable '%s'", name.c_str());

	bool isPointer = type->raw->isPointerTy();

	if (scope == 0)
	{
		// Global varable
		module->getOrInsertGlobal(name, type->raw);
		GlobalVariable* gVar = module->getNamedGlobal(name);
		gVar->setLinkage(GlobalValue::CommonLinkage);

		if (initializer)
			gVar->setInitializer(cast<Constant>(initializer->Generate()));

		namedValues[name] = { gVar, type->raw, modifiers };

		return gVar;
	}

	// Allocate on the stack
	AllocaInst* allocaInst = builder->CreateAlloca(type->raw, nullptr, name);
	namedValues[name] = { allocaInst, type->raw, modifiers };

	// Store initializer value into this variable if we have one
	if (initializer)
	{
		if (isPointer)
		{
			// stay safe kids
			if (initializer->type->raw != type->raw)
				throw CompileError(line, "address types do not match");

			Value* value = initializer->Generate();

			builder->CreateStore(value, allocaInst);
		}
		else
		{
			Value* value = initializer->Generate();

			builder->CreateStore(value, allocaInst);
		}
	}

	return allocaInst;
}

llvm::Value* Load::Generate()
{
	using namespace llvm;
	
	PROFILE_FUNCTION();

	Assert(namedValues.count(name), line, "unknown variable '%s'", name.c_str());

	NamedValue& value = namedValues[name];

	return emitInstruction ? builder->CreateLoad(value.type, value.raw, "loadtmp") : value.raw;
}

llvm::Value* Store::Generate()
{
	using namespace llvm;
	
	PROFILE_FUNCTION();

	// TODO: enforce shadowing rules

	Assert(namedValues.count(name), line, "unknown variable '%s'", name.c_str());

	NamedValue& namedValue = namedValues[name];
	Assert(!namedValue.modifiers.isConst, line, "cannot assign to an immutable variable");
	
	Value* value = right->Generate();
	if (value->getType() != namedValue.type)
	{
		// Try implicit cast
		llvm::Value* castAttempt = Caster::TryIfValid(value->getType(), namedValue.type, value);
		Assert(castAttempt, line, "assignment: illegal implicit cast to '%s'", type->name.c_str());
		value = castAttempt;
	}

	//Assert(!IsPointer(namedValue.type), line,
	//	"operands for an assignment must be of the same type. got: %s = %s",
	//	type->name.c_str(), right->type->name.c_str());

	if (storeIntoLoad)
	{
		LoadInst* load = builder->CreateLoad(namedValue.raw, "loadtmp");
		return builder->CreateStore(value, load);
	}

	return builder->CreateStore(value, namedValue.raw);
}

llvm::Value* Cast::Generate()
{
	using namespace llvm;
	
	PROFILE_FUNCTION();

	Value* operandValue = operand->Generate();
	llvm::Type* operandType = operandValue->getType();

	if (type->raw == operandType)
	{
		// Redundant
		Warn(line, "redundant explicit cast to '%s'", type->name.c_str());
		return operandValue;
	}

	// Try implicit cast
	llvm::Value* castAttempt = Caster::TryIfValid(operandType, type->raw, operandValue);
	if (castAttempt)
	{
		// Implicit cast worked
		return castAttempt;
	}

	// int -> float is simple, for example
	// float -> struct is kind of absurd
	constexpr int numSimpleCasts = 8;
	static const std::pair<::Type*, ::Type*> simpleCasts[numSimpleCasts] =
	{
		{ ::Type::int32Type, ::Type::float32Type }, // int32 to float32
		{ ::Type::int32Type, ::Type::float64Type }, // int32 to float64
		{ ::Type::int64Type, ::Type::float64Type }, // int64 to float64
		{ ::Type::int64Type, ::Type::float32Type }, // int64 to float32

		{ ::Type::float32Type, ::Type::int32Type }, // float32 to int32
		{ ::Type::float32Type, ::Type::int64Type }, // float64 to int32
		{ ::Type::float64Type, ::Type::int64Type }, // float64 to int64
		{ ::Type::float64Type, ::Type::int32Type }, // float32 to int64
	};
	bool isSimpleCast = false;
	for (int i = 0; i < numSimpleCasts; i++)
	{
		auto& pair = simpleCasts[i];
		if (pair.second->raw == operandType && pair.first->raw == type->raw)
		{
			isSimpleCast = true;
			break;
		}
	}

	Assert(isSimpleCast, line, "explicit cast to '%s' from value is unsafe", type->name.c_str());

/*
	auto getBitWidth = [](llvm::Type* t) -> int
	{
		if (t->isFloatingPointTy())
			return t->getFPMantissaWidth();
		if (t->isIntegerTy())
			return t->getIntegerBitWidth();
	};

	int bitWidthFrom = getBitWidth(operandType), bitWidthTo = getBitWidth(type->raw);
	if (type->isFloat())
	{
		if (bitWidthTo == 24)
			bitWidthTo = 32;
		if (bitWidthTo == 53)
			bitWidthTo = 64;
	}
	if (bitWidthFrom != bitWidthTo)
	{
		llvm::Type* dest = nullptr;
		// Trunc or ext
		if (bitWidthTo > bitWidthFrom)
		{
			// Extend
			if (operandType->isFloatTy())
			{
				dest = ::Type::float32Type->raw;
				operandValue = builder->CreateFPExt(operandValue, dest);
			}
			else if (operandType->isDoubleTy())
			{
				dest = ::Type::float64Type->raw;
				operandValue = builder->CreateFPExt(operandValue, dest);
			}
			else if (operandType->isIntegerTy())
			{
				dest = llvm::IntegerType::get(*context, bitWidthTo);
				operandValue = builder->CreateSExt(operandValue, dest);
			}
		}
		if (bitWidthTo < bitWidthFrom)
		{
			// Trunc
			if (operandType->isFloatTy())
			{
				dest = ::Type::float32Type->raw;
				operandValue = builder->CreateFPTrunc(operandValue, dest);
			}
			else if (operandType->isDoubleTy())
			{
				dest = ::Type::float64Type->raw;
				operandValue = builder->CreateFPTrunc(operandValue, dest);
			}
			else if (operandType->isIntegerTy())
			{
				dest = llvm::IntegerType::get(*context, bitWidthTo);
				operandValue = builder->CreateTrunc(operandValue, dest);
			}
		}
	}*/
	
	bool floatToInt = operandType->isFloatingPointTy() && type->isInt();
	bool intToFloat = !floatToInt && operandType->isIntegerTy() && type->isFloat();

	if (floatToInt)
		return builder->CreateFPToSI(operandValue, type->raw, "fptositmp");
	if (intToFloat)
		return builder->CreateSIToFP(operandValue, type->raw, "sitofptmp");

	Assert(false, line, "invalid explicit cast");
}

llvm::Value* Unary::Generate()
{
	using namespace llvm;

	PROFILE_FUNCTION();

	Value* value = operand->Generate();
	Value* loadedValue = nullptr;
	if (value->getType()->isPointerTy())
		loadedValue = builder->CreateLoad(value, "loadtmp");

	// TODO: generate the other unary operators
	// TODO: pointer arithmetic

	switch (unaryType)
	{
	case UnaryType::Not: // !value
	{
		if (!operand->type->isBool() && !operand->type->isInt())
			throw CompileError(line, "invalid operand for unary not (!). operand must be integral.");

		return builder->CreateNot(loadedValue ? loadedValue : value, "nottmp");
	}
	case UnaryType::Negate: // -value
	{
		if (!IsNumeric(operand->type->raw))
			throw CompileError(line, "invalid operand for unary negate (-). operand must be numerical.");

		if (operand->type->isFloat())   return builder->CreateFNeg(loadedValue ? loadedValue : value, "negtmp");
		if (operand->type->isInt())     return builder->CreateNeg(loadedValue ? loadedValue : value, "negtmp");

		break;
	}
	case UnaryType::PrefixIncrement:
	{
		if (!IsNumeric(operand->type->raw))
			throw CompileError(line, "invalid operand for unary increment (++). operand must be numerical.");

		builder->CreateStore(builder->CreateAdd(loadedValue, GetOneNumericConstant(operand->type), "inctmp"), value);
		return builder->CreateLoad(value, "loadtmp");
	}
	case UnaryType::PostfixIncrement:
	{
		if (!IsNumeric(operand->type->raw))
			throw CompileError(line, "invalid operand for unary increment (++). operand must be numerical.");

		llvm::Value* previousValue = builder->CreateAlloca(operand->type->raw, nullptr, "prevtmp");
		builder->CreateStore(loadedValue, previousValue); // Store previous value
		llvm::Value* result = builder->CreateAdd(loadedValue, GetOneNumericConstant(operand->type), "inctmp");
		builder->CreateStore(result, value);

		return builder->CreateLoad(previousValue, "prevload");
	}
	case UnaryType::PrefixDecrement:
	{
		if (!IsNumeric(operand->type->raw))
			throw CompileError(line, "invalid operand for unary decrement (--). operand must be numerical.");

		return builder->CreateStore(builder->CreateSub(loadedValue, GetOneNumericConstant(operand->type), "dectmp"), value);
	}
	case UnaryType::PostfixDecrement:
	{
		if (!IsNumeric(operand->type->raw))
			throw CompileError(line, "invalid operand for unary decrement (--). operand must be numerical.");
		
		llvm::Value* previousValue = builder->CreateStore(loadedValue, builder->CreateAlloca(operand->type->raw, nullptr, "prevtmp"));
		llvm::Value* result = builder->CreateAdd(loadedValue, GetOneNumericConstant(operand->type), "dectmp");
		builder->CreateStore(result, value);

		return previousValue;
	}
	case UnaryType::AddressOf:
		return value;
	case UnaryType::Deref:
		return loadedValue;
	}

	throw CompileError(line, "invalid unary operator");
}

static llvm::Value* CreateBinOp(llvm::Value* left, llvm::Value* right, BinaryType type, 
	const VariableDefinition::Modifiers* lhsMods, const VariableDefinition::Modifiers* rhsMods, int line, bool typeCheck = true)
{
	using llvm::Instruction;

	PROFILE_FUNCTION();

	llvm::Type* lType = left->getType();
	Instruction::BinaryOps instruction = (Instruction::BinaryOps)-1;

	if (typeCheck && lType != right->getType())
	{
		llvm::Value* castAttempt = Caster::TryIfValid(right->getType(), left->getType(), right);
		if (castAttempt) // Implicit cast worked
			Warn(line, "binary op: implicit cast from right operand type to left operand type");
		Assert(castAttempt, line, "binary op: illegal implicit cast from right operand type to left operand type");
		right = castAttempt;
	}

	// TODO: clean up
	switch (type)
	{
		case BinaryType::CompoundAdd:
			Assert(!lhsMods->isConst, line, "cannot assign to an immutable variable");
		case BinaryType::Add:
		{
			instruction = lType->isIntegerTy() ? Instruction::Add : Instruction::FAdd;
			break;
		}
		case BinaryType::CompoundSub:
			Assert(!lhsMods->isConst, line, "cannot assign to an immutable variable");
		case BinaryType::Subtract:
		{
			instruction = lType->isIntegerTy() ? Instruction::Sub : Instruction::FSub;
			break;
		}
		case BinaryType::CompoundMul:
			Assert(!lhsMods->isConst, line, "cannot assign to an immutable variable");
		case BinaryType::Multiply:
		{
			instruction = lType->isIntegerTy() ? Instruction::Mul : Instruction::FMul;
			break;
		}
		case BinaryType::CompoundDiv:
			Assert(!lhsMods->isConst, line, "cannot assign to an immutable variable");
		case BinaryType::Divide:
		{
			if (lType->isIntegerTy())
				throw CompileError(line, "integer division not supported");
		
			instruction = Instruction::FDiv;
			break;
		}
		case BinaryType::Assign:
		{
			Assert(!lhsMods->isConst, line, "cannot assign to an immutable variable");

			return builder->CreateStore(right, left);
		}
		case BinaryType::Equal:
		{
			if (lType->isIntegerTy())
				return builder->CreateICmpEQ(left, right, "cmptmp");
			else if (lType->isFloatingPointTy())
				return builder->CreateFCmpUEQ(left, right, "cmptmp");
			break;
		}
		case BinaryType::NotEqual:
		{
			if (lType->isIntegerTy())
				return builder->CreateICmpNE(left, right, "cmptmp");
			else if (lType->isFloatingPointTy())
				return builder->CreateFCmpUNE(left, right, "cmptmp");
			break;
		}
		case BinaryType::Less:
		{
			return lType->isIntegerTy() ? builder->CreateICmpULT(left, right, "cmptmp") :
				builder->CreateFCmpULT(left, right, "cmptmp");
		}
		case BinaryType::LessEqual:
		{
			return lType->isIntegerTy() ? builder->CreateICmpULE(left, right, "cmptmp") :
				builder->CreateFCmpULE(left, right, "cmptmp");
		}
		case BinaryType::Greater:
		{
			return lType->isIntegerTy() ? builder->CreateICmpUGT(left, right, "cmptmp") :
				builder->CreateFCmpUGT(left, right, "cmptmp");
		}
		case BinaryType::GreaterEqual:
		{
			return lType->isIntegerTy() ? builder->CreateICmpUGE(left, right, "cmptmp") :
				builder->CreateFCmpUGE(left, right, "cmptmp");
		}
	}

	Assert(instruction != (Instruction::BinaryOps)-1, line, "invalid binary operator");
	return builder->CreateBinOp(instruction, left, right);
}

static VariableDefinition::Modifiers* TryGetVariableModifiers(Statement* statement)
{
	// TODO: ugly as hell
	if (statement->statementType == StatementType::LoadExpr)
		return &namedValues[((Load*)statement)->name].modifiers;
	if (statement->statementType == StatementType::StoreExpr)
		return &namedValues[((Store*)statement)->name].modifiers;
	if (statement->statementType == StatementType::UnaryExpr)
	{
		Unary* unary = (Unary*)statement;
		if (unary->unaryType == UnaryType::Deref)
			return TryGetVariableModifiers(unary->operand.get());
	}

	return nullptr;
}

llvm::Value* Binary::Generate()
{
	PROFILE_FUNCTION();

	llvm::Value* lhs = left->Generate();
	llvm::Value* rhs = right->Generate();

	// Uh what?
	Assert(left && right, line, "invalid binary operator '%.*s'", operatorToken.length, operatorToken.start);

	VariableDefinition::Modifiers* lMods = TryGetVariableModifiers(left.get());
	VariableDefinition::Modifiers* rMods = TryGetVariableModifiers(right.get());

	if (right->type != left->type)
	{
		bool valid = false;

		if (left->statementType == StatementType::UnaryExpr &&
			(left->type->isPointerTo(right->type) ||
				Caster::IsValid(right->type, left->type->getTypePointedTo()))) // Implicit cast to derefed pointer type
		{
			// Assigning a value to a dereference expression

			Unary* unary = static_cast<Unary*>(left.get());
			if (unary->unaryType == UnaryType::Deref && binaryType == BinaryType::Assign)
			{
				// Looks good 2 me
				valid = true;
			}

			// Implicitly cast?
			llvm::Value* castAttempt = Caster::TryIfValid(right->type, left->type->getTypePointedTo(), rhs);
			if (castAttempt) // Implicit cast worked
				Warn(line, "binary op: implicit cast from right operand type to left operand type");
			Assert(castAttempt, line, "binary op: illegal implicit cast from right operand type to left operand type");
			rhs = castAttempt;
		}
		else
		{
			// Implicitly cast?
			if (Caster::IsValid(right->type, left->type))
				valid = true;
		}

		
		Assert(valid, line, "both operands of a binary operation must be of the same type");
	}

	Assert(right->type && left->type, line, "invalid operands for binary operation");
	
	llvm::Value* value = CreateBinOp(lhs, rhs, binaryType, lMods, rMods, line, false);
	Assert(value, line, "invalid binary operator '%.*s'", operatorToken.length, operatorToken.start);

	return value;
}

llvm::Value* Branch::Generate()
{
	using namespace llvm;

	PROFILE_FUNCTION();
	
	BasicBlock* parentBlock = builder->GetInsertBlock();

	BasicBlock* trueBlock = BasicBlock::Create(*context, "btrue", currentFunction);
	BasicBlock* falseBlock = BasicBlock::Create(*context, "bfalse", currentFunction);

	// Create branch thing
	BranchInst* branch = builder->CreateCondBr(expression->Generate(), trueBlock, falseBlock);

	BasicBlock* endBlock = BasicBlock::Create(*context, "end", currentFunction);
	// Generate bodies
	{
		auto createBlock = [endBlock](BasicBlock* block, const std::vector<std::unique_ptr<Statement>>& statements)
		{
			builder->SetInsertPoint(block);

			bool terminated = false;
			for (auto& statement : statements)
			{
				statement->Generate();

				if (statement->statementType == StatementType::ReturnExpr)
				{
					terminated = true;
					break;
				}
			}

			// Add jump to end
			if (!terminated)
				builder->CreateBr(endBlock);
		};

		// Generate body for the true branch
		createBlock(trueBlock, ifBody);

		// Generate body for the true branch
		createBlock(falseBlock, elseBody);
	}

	builder->SetInsertPoint(parentBlock); // Reset to correct insert point

	// Insert end block
	builder->SetInsertPoint(endBlock);

	return branch;
}

llvm::Value* Call::Generate()
{
	PROFILE_FUNCTION();
	
	// Look up function name
	llvm::Function* func = module->getFunction(fnName);
	Assert(func, line, "unknown function referenced");

	// Handle arg mismatch
	bool isVarArg = func->isVarArg();
	if (isVarArg)
	{
		Assert(args.size() >= func->arg_size() - 1, line, "not enough arguments passed to '%s'", fnName.c_str());
	}
	else
	{
		int given = args.size(), required = func->arg_size();
		Assert(given == required, line, given > required ? "too many arguments passed to '%s'" : "not enough arguments passed to '%s'", fnName.c_str());
	}

	// Generate arguments
	int i = 0;
	std::vector<llvm::Value*> argValues;
	for (auto& expression : args)
	{
		llvm::Value* generated = expression->Generate();
		Assert(generated, line, "failed to generate function argument");

		llvm::Type* generatedType = generated->getType(), *paramType = func->getArg(i)->getType();
		if (i < func->arg_size() && generatedType != paramType)
		{
			// Try an implicit cast
			llvm::Value* castAttempt = Caster::TryIfValid(generatedType, paramType, generated);
			if (castAttempt)
				Warn(line, 
				"call: implicit cast from argument %d (type of '%s') to '%s' parameter %d (type of '%s')", 
				i, args[i]->type->name.c_str(), target->name.c_str(), i, 
				target->params[i].variadic ? "..." : target->params[i].type->name.c_str());
			Assert(castAttempt, line,
					"illegal call: implicit cast from argument %d (type of '%s') to '%s' parameter %d (type of '%s') not allowed",
					i, args[i]->type->name.c_str(), target->name.c_str(), i,
					target->params[i].variadic ? "..." : target->params[i].type->name.c_str());
			generated = castAttempt;
			generatedType = generated->getType();
		}

		// Promotions
		if ((i < target->params.size() && target->params[i].variadic) || 
			(i >= target->params.size() && target->params[target->params.size() - 1].variadic))
		{
			if (generatedType->isFloatingPointTy() && !generatedType->isDoubleTy())
			{
				// Promote any other floating point type to double
				generated = builder->CreateFPExt(generated, Type::float64Type->raw);
			}
			if (generatedType->isIntegerTy() && generatedType->getIntegerBitWidth() == 1)
			{
				// Bool to int32
				generated = builder->CreateZExt(generated, Type::int32Type->raw);
			}
		}
		
		++i;
		argValues.push_back(generated);
	}

	llvm::CallInst* callInst = builder->CreateCall(func, argValues);

	return callInst;
}

static void GenerateEntryBlockAllocasAndLoads(llvm::Function* function)
{
	using namespace llvm;

	PROFILE_FUNCTION();
	
	for (Argument* arg = function->arg_begin(); arg != function->arg_end(); ++arg)
	{
		llvm::Type* type = arg->getType();

		std::string name = arg->getName().str();
		name.erase(0, ARG_PREFIX.length()); // Remove arg suffix

		AllocaInst* allocaInst = builder->CreateAlloca(type, nullptr, name);
		builder->CreateStore(arg, allocaInst); // Store argument value into allocated value

		namedValues[name] = { allocaInst, type, { } };
	}
}

llvm::Value* Return::Generate()
{
	PROFILE_FUNCTION();

	// Handle the return value
	if (expression)
	{
		llvm::Value* returnValue = expression->Generate();
		llvm::Value* result = returnValue;
		if (returnValue->getType() != currentFunction->getReturnType())
		{
			result = Caster::TryIfValid(returnValue->getType(), currentFunction->getReturnType(), returnValue);
			if (result)
				Warn(line, "return statement for function '%s': implicit cast to return type from '%s'", currentFunction->getName().data(), expression->type->name.c_str());
			Assert(result, line, "invalid return value for function '%s': illegal implicit cast to return type from '%s'", currentFunction->getName().data(), expression->type->name.c_str());
		}

		return builder->CreateRet(result);
	}
	else
	{
		// No return value
		Assert(currentFunction->getReturnType() == llvm::Type::getVoidTy(*context), line, "invalid return statement in function '%s': expected a return value", currentFunction->getName());
		return builder->CreateRetVoid();
	}
}

llvm::Value* Import::Generate()
{
	PROFILE_FUNCTION();

	return data->Generate();
}

static llvm::Value* GenerateFunctionPrototype(const FunctionDefinition* definition, const FunctionPrototype& prototype)
{
	PROFILE_FUNCTION();

	std::vector<llvm::Type*> paramTypes(prototype.params.size());

	// Fill paramTypes with proper types
	int index = 0;
	for (auto& param : prototype.params)
	{
		if (!param.variadic)
			paramTypes[index++] = param.type->raw;
	}
	index = 0;

	bool hasVarArg = false;
	for (int i = 0; i < prototype.params.size(); i++)
	{
		if (prototype.params[i].variadic)
			hasVarArg = true;
	}
	if (hasVarArg)
		paramTypes.pop_back();

	llvm::Type* returnType = definition->prototype.returnType->raw;

	Assert(returnType, definition->line, "unresolved return type for function prototype '%s'", prototype.name.c_str());

	llvm::FunctionType* functionType = llvm::FunctionType::get(returnType, paramTypes, hasVarArg);
	llvm::Function* function = llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, prototype.name.c_str(), *module);

	return function;
}

llvm::Value* FunctionDefinition::Generate()
{
	PROFILE_FUNCTION();

	if (!HasBody()) // We are just a prototype (probably in an "import" statement or something)
		return GenerateFunctionPrototype(this, prototype);

	// Full definition for function
	llvm::Function* function = module->getFunction(prototype.name.c_str());
	// Create function if it doesn't exist
	if (!function)
	{
		std::vector<llvm::Type*> paramTypes(prototype.params.size());

		// Fill paramTypes with proper types
		int index = 0;
		for (auto& param : prototype.params)
			paramTypes[index++] = param.type->raw;
		index = 0;

		llvm::Type* returnType = prototype.returnType->raw;
		Assert(returnType, line, "unresolved return type for '%s'", prototype.name.c_str());

		llvm::FunctionType* functionType = llvm::FunctionType::get(returnType, paramTypes, false);
		function = llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, prototype.name.c_str(), *module);

		// Set names for function args
		for (auto& arg : function->args())
			arg.setName(ARG_PREFIX + prototype.params[index++].name); // Suffix arguments with '_' to make stuff work
	}

	Assert(function->empty(), line, "function cannot be redefined");

	currentFunction = function;

	// Create block to start insertion into
	llvm::BasicBlock* block = llvm::BasicBlock::Create(*context, "entry", function);
	builder->SetInsertPoint(block);

	// Allocate args on the stack
	ResetStackValues();
	GenerateEntryBlockAllocasAndLoads(function);

	{
		PROFILE_SCOPE("Generate body :: FunctionDefinition::Generate()");
		
		// Generate body
		for (auto& statement : body)
			statement->Generate();
	}

	if (!block->getTerminator())
	{
		Assert(type->isVoid(), line, "return statement not found in function '%s'", prototype.name.c_str());

		builder->CreateRet(nullptr);
	}

	{
		PROFILE_SCOPE("Verify function :: FunctionDefinition::Generate()");

		// Handle any errors in the function
		if (verifyFunction(*function, &llvm::errs()))
		{
			//module->print(llvm::errs(), nullptr);
			function->eraseFromParent();
			throw CompileError(line, "function verification failed");
		}
	}

	return function;
}

llvm::Value* StructureDefinition::Generate()
{
	PROFILE_FUNCTION();
	
	// The heavy lifting was done in ResolveType(), so we don't really do anything here

	return nullptr;
}

llvm::Value* StructureMemberAccess::Generate()
{
	PROFILE_FUNCTION();

	UserDefinedType* structType = (UserDefinedType*)structureType;
	Assert(structType, line, "invalid structure type for a member access");
	
	int memberIndex = -1, i = 0;
	Type* memberType = nullptr;
	for (auto& member : structType->members)
	{
		if (member.first == memberName)
		{
			memberType = member.second;
			memberIndex = i;
			break;
		}
		i++;
	}
	Assert(memberIndex != -1, line, "struct '%s' doesn't contain member '%s'", structType->name.c_str(), memberName.c_str());
	Assert(memberType, line, "member '%s' in struct '%s' has invalid type", memberName.c_str(), structType->name.c_str());

	NamedValue& structureVariable = namedValues[variableName];
	llvm::Value* structure = structureVariable.raw;

	if (IsAssign())
	{
		llvm::Value* gep = builder->CreateStructGEP(structureType->raw, structure, memberIndex, "geptmp");

		llvm::Value* assignmentValue = assignment->Generate();
		if (assignmentValue->getType() != gep->getType()->getContainedType(0))
		{
			assignmentValue = Caster::TryIfValid(assignmentValue->getType(), gep->getType()->getContainedType(0), assignmentValue);
			if (assignmentValue)
				Warn(line, "assignment to '%s' member '%s': implicit cast to member type", structureType->name.c_str(), memberName.c_str());
			Assert(assignmentValue, line, "invalid implicit cast to '%s' member '%s'", structureType->name.c_str(), memberName.c_str());
		}

		return builder->CreateStore(assignmentValue, gep);
	}
	else
	{
		llvm::Value* gep = builder->CreateStructGEP(structureType->raw, structure, memberIndex, "geptmp");

		return builder->CreateLoad(gep, "loadtmp");
	}
}
#endif


static llvm::Value* Get1NumericalConstant(llvm::Type* type)
{
	switch (type->getTypeID())
	{
	case llvm::Type::IntegerTyID:
		return llvm::ConstantInt::get(*context, llvm::APInt(type->getIntegerBitWidth(), 1, true));
	case llvm::Type::FloatTyID:
	case llvm::Type::DoubleTyID:
		return llvm::ConstantFP::get(*context, llvm::APFloat(1.0f));
	}
}

llvm::Value* PrimaryExpression::Generate()
{
	PROFILE_FUNCTION();

	switch (type->tag)
	{
	case TypeTag::Int8:
		return llvm::ConstantInt::get(*context, llvm::APInt(8, (int8_t)value.i64));
	case TypeTag::Int16:
		return llvm::ConstantInt::get(*context, llvm::APInt(16, (int16_t)value.i64));
	case TypeTag::Int32:
		return llvm::ConstantInt::get(*context, llvm::APInt(32, (int32_t)value.i64));
	case TypeTag::Int64:
		return llvm::ConstantInt::get(*context, llvm::APInt(64, value.i64));
	case TypeTag::Float32:
		return llvm::ConstantFP::get(*context, llvm::APFloat((float)value.f64));
	case TypeTag::Float64:
		return llvm::ConstantFP::get(*context, llvm::APFloat(value.f64));
	case TypeTag::Bool:
		return llvm::ConstantInt::getBool(*context, value.b32);
	}

	throw CompileError(sourceLine, "invalid type for primary expression");
	return nullptr;
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

static llvm::Value* LoadIfVariable(llvm::Value* generated, std::unique_ptr<Expression>& expr)
{
	bool isAlloca = llvm::isa<llvm::AllocaInst>(generated);
	if (expr->nodeType != NodeType::VariableAccess || !isAlloca)
		return generated;

	return builder->CreateLoad(generated, false, "loadtmp");
}

llvm::Value* UnaryExpression::Generate()
{
	PROFILE_FUNCTION();

	llvm::Value* value = operand->Generate();
	//if (operand->nodeType == NodeType::VariableAccess)
	//{
	//	value = builder->CreateLoad(value, false, "loadtmp");
	//}

	//bool operandIsPointer = value->getType()->isPointerTy();

	//if (value->getType()->isPointerTy())
	//	loadedValue = builder->CreateLoad(value, "loadtmp");

	switch (unaryType)
	{
	case UnaryType::Not: // !value
	{
		return builder->CreateNot(value, "not_tmp");
	}
	case UnaryType::Negate: // -value
	{
		value = LoadIfVariable(value, operand);

		llvm::Type* valueType = value->getType();
		switch (valueType->getTypeID())
		{
		case llvm::Type::IntegerTyID:
			return builder->CreateNeg(value, "negtmp");
		case llvm::Type::FloatTyID:
		case llvm::Type::DoubleTyID:
			return builder->CreateFNeg(value, "fnegtmp");
		}

		throw CompileError(sourceLine, "invalid operand for unary negation (-), operand must be numeric");

		break;
	}
	case UnaryType::PrefixIncrement:
	{
		// Increment
		llvm::Value* loaded = builder->CreateLoad(value, false, "loadtmp");
		builder->CreateStore(builder->CreateAdd(loaded, Get1NumericalConstant(loaded->getType()), "inctmp"), value);

		// Return newly incremented value
		return builder->CreateLoad(value, "loadtmp");
	}
	case UnaryType::PostfixIncrement:
	{
		// Increment
		llvm::Value* loaded = builder->CreateLoad(value, false, "loadtmp");
		builder->CreateStore(builder->CreateAdd(loaded, Get1NumericalConstant(loaded->getType()), "inctmp"), value);

		// Return value before increment
		return loaded;
	}
	case UnaryType::PrefixDecrement:
	{
		llvm::Value* loaded = builder->CreateLoad(value, false, "loadtmp");
		builder->CreateStore(builder->CreateSub(loaded, Get1NumericalConstant(loaded->getType()), "dectmp"), value);

		return builder->CreateLoad(value, "loadtmp");
	}
	case UnaryType::PostfixDecrement:
	{
		llvm::Value* loaded = builder->CreateLoad(value, false, "loadtmp");
		builder->CreateStore(builder->CreateSub(loaded, Get1NumericalConstant(loaded->getType()), "dectmp"), value);

		return loaded;
	}
	case UnaryType::AddressOf:
	{
		return value;
		llvm::Value* result = builder->CreateAlloca(value->getType(), nullptr, "allocatmp");
		builder->CreateStore(value, result);
		return result;
	}
	case UnaryType::Deref:
	{
		llvm::Value* loaded = builder->CreateLoad(value, false, "loadtmp");
		return loaded;
	}
	}

	throw CompileError(sourceLine, "invalid unary operator");
}

static bool IsVariablePointer(llvm::Type* v, const std::unique_ptr<Expression>& expr)
{
	return v->isPointerTy() && expr->nodeType == NodeType::VariableAccess;
}

llvm::Value* BinaryExpression::Generate()
{
	using namespace llvm;

	llvm::Value* lhs = left->Generate();
	llvm::Value* rhs = right->Generate();

	llvm::Type* lhsType = lhs->getType();
	llvm::Type* rhsType = rhs->getType();

	bool rhsChanged = false;

	// Unless assiging, treat variables as the underlying values
	if (binaryType != BinaryType::Assign)
	{
		if (IsVariablePointer(lhsType, left))
			lhs = builder->CreateLoad(lhs, false, "lhsloadtmp");
		if (IsVariablePointer(rhsType, right))
			rhsChanged = (rhs = builder->CreateLoad(rhs, false, "rhsloadtmp"));

		lhsType = lhs->getType();
		rhsType = rhs->getType();
	}


	// If right is a variable (meaning rhs is an AllocaInst), load it so we can store into lhs
	if (!rhsChanged && IsVariablePointer(rhsType, right))
	{
		// Deref
		rhs = builder->CreateLoad(rhs, false, "loadtmp");
	}

	Instruction::BinaryOps instruction = (Instruction::BinaryOps)-1;
	switch (binaryType)
	{
	case BinaryType::CompoundAdd:
	case BinaryType::Add:
	{
		if (lhsType->isIntegerTy())
			instruction = Instruction::Add;
		else if (lhsType->isFloatingPointTy())
			instruction = Instruction::FAdd;

		break;
	}
	case BinaryType::CompoundSub:
	case BinaryType::Subtract:
	{
		if (lhsType->isIntegerTy())
			instruction = Instruction::Sub;
		else if (lhsType->isFloatingPointTy())
			instruction = Instruction::FSub;

		break;
	}
	case BinaryType::CompoundMul:
	case BinaryType::Multiply:
	{
		if (lhsType->isIntegerTy())
			instruction = Instruction::Mul;
		else if (lhsType->isFloatingPointTy())
			instruction = Instruction::FMul;

		break;
	}
	case BinaryType::CompoundDiv:
	case BinaryType::Divide:
	{
		instruction = Instruction::FDiv;
		break;
	}
	case BinaryType::Assign:
	{
		return builder->CreateStore(rhs, lhs);
	}
	case BinaryType::Equal:
	{
		if (lhsType->isIntegerTy())
			return builder->CreateICmpEQ(lhs, rhs, "cmptmp");
		if (lhsType->isFloatingPointTy())
			return builder->CreateFCmpUEQ(lhs, rhs, "cmptmp");

		break;
	}
	case BinaryType::NotEqual:
	{
		if (lhsType->isIntegerTy())
			return builder->CreateICmpNE(lhs, rhs, "cmptmp");
		if (lhsType->isFloatingPointTy())
			return builder->CreateFCmpUNE(lhs, rhs, "cmptmp");

		break;
	}
	case BinaryType::Less:
	{
		if (lhsType->isIntegerTy())
			return builder->CreateICmpULT(lhs, rhs, "cmptmp");
		if (lhsType->isFloatingPointTy())
			return builder->CreateFCmpULT(lhs, rhs, "cmptmp");

		break;
	}
	case BinaryType::LessEqual:
	{
		if (lhsType->isIntegerTy())
			return builder->CreateICmpULE(lhs, rhs, "cmptmp");
		if (lhsType->isFloatingPointTy())
			return builder->CreateFCmpULE(lhs, rhs, "cmptmp");
		break;
	}
	case BinaryType::Greater:
	{
		if (lhsType->isIntegerTy())
			return builder->CreateICmpUGT(lhs, rhs, "cmptmp");
		if (lhsType->isFloatingPointTy())
			return builder->CreateFCmpUGT(lhs, rhs, "cmptmp");
		break;
	}
	case BinaryType::GreaterEqual:
	{
		if (lhsType->isIntegerTy())
			return builder->CreateICmpUGE(lhs, rhs, "cmptmp");
		if (lhsType->isFloatingPointTy())
			return builder->CreateFCmpUGE(lhs, rhs, "cmptmp");
		break;
	}
	}

	if (instruction == (Instruction::BinaryOps)-1)
		throw CompileError(sourceLine, "invalid binary operator");

	return builder->CreateBinOp(instruction, lhs, rhs);
}

llvm::Value* BranchExpression::Generate()
{
	return nullptr;
}

static Scope* sCurrentScope = nullptr;
static llvm::Function* sCurrentFunction = nullptr;

llvm::Value* CompoundStatement::Generate()
{
	sCurrentScope = sCurrentScope->Deepen();

	llvm::BasicBlock* previousBlock = builder->GetInsertBlock();
	llvm::BasicBlock* block = llvm::BasicBlock::Create(*context, "entry", sCurrentFunction);
	builder->SetInsertPoint(block);

	for (auto& expr : children)
		expr->Generate();

	builder->SetInsertPoint(previousBlock);

	sCurrentScope = sCurrentScope->Increase();

	return block;
}

llvm::Value* VariableDefinitionStatement::Generate()
{
	llvm::Value* initialVal = nullptr;
	llvm::Type* ty = nullptr;
	if (initializer)
	{
		initialVal = initializer->Generate();
		if (!type)
			ty = initialVal->getType();
	}

	llvm::Value* alloc = builder->CreateAlloca(ty ? ty : type->raw);
	sCurrentScope->AddValue(std::string(Name.start, Name.length), { alloc });

	if (!initialVal)
		return alloc;
	
	return builder->CreateStore(initialVal, alloc);
}

llvm::Value* VariableAccessExpression::Generate()
{
	Value value;
	if (!sCurrentScope->HasValue(name, &value))
		throw CompileError(sourceLine, "identifier '%s' not declared in scope", name.c_str());

	return value.raw;
	return builder->CreateLoad(value.raw, false, "loadtmp");
}

llvm::Value* FunctionDefinitionExpression::Generate()
{
	PROFILE_FUNCTION();

	type = Type::FindOrAdd(prototype.ReturnType->name);

	// Full definition for function
	llvm::Function* function = module->getFunction(prototype.Name.c_str());

	// Create function if it doesn't exist
	if (!function)
	{
		std::vector<llvm::Type*> parameterTypes(prototype.Parameters.size());

		// Fill paramTypes with proper types
		uint32_t i = 0;
		for (auto& param : prototype.Parameters)
			parameterTypes[i++] = param->type->raw;
		i = 0;

		llvm::Type* retType = prototype.ReturnType->raw;
		if (!retType)
			throw CompileError(sourceLine, "unresolved return type for function '%s'", prototype.Name.c_str());

		llvm::FunctionType* functionType = llvm::FunctionType::get(retType, parameterTypes, false);
		function = llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, prototype.Name.c_str(), *module);

		if (body)
			sCurrentScope = sCurrentScope->Deepen(); // No point in scoping args if there's no body

		// Set names for function args
		for (auto& arg : function->args())
		{
			auto name = prototype.Parameters[i++]->Name;
			arg.setName(std::string(name.start, name.length));

			sCurrentScope->AddValue(arg.getName().str(), { &arg });
		}
	}

	if (!function->empty())
		throw CompileError(sourceLine, "function cannot be redefined");

	sCurrentFunction = function;

	if (body)
	{
		PROFILE_SCOPE("Generate function body");

		llvm::BasicBlock* previousBlock = builder->GetInsertBlock();
		llvm::BasicBlock* block = nullptr;
		block = llvm::cast<llvm::BasicBlock>(body->Generate());

		if (!block->getTerminator())
		{
			if (type->tag != TypeTag::Void)
				throw CompileError(sourceLine, "expected return statement in function '%s' (%s)", prototype.Name.c_str(), type->name.c_str());

			builder->SetInsertPoint(block);
			builder->CreateRet(nullptr);
			builder->SetInsertPoint(previousBlock);
		}

		sCurrentScope = sCurrentScope->Increase();
	}

	{
		PROFILE_SCOPE("Verify function");

		// Handle any errors in the function
		if (verifyFunction(*function, &llvm::errs()))
		{
			//module->print(llvm::errs(), nullptr);
			function->eraseFromParent();
			throw CompileError(sourceLine, "function verification failed");
		}
	}

	return function;
}

llvm::Value* ReturnStatement::Generate()
{
	llvm::Value* generated = value->Generate();
	generated = LoadIfVariable(generated, value);

	return builder->CreateRet(generated);
}

llvm::Value* FunctionCallExpression::Generate()
{
	llvm::Function* function = module->getFunction(name.c_str());
	if (!function)
		throw CompileError(sourceLine, "undeclared function '%s'", name.c_str());

	if (function->arg_size() != arguments.size())
		throw CompileError(sourceLine, "expected %d arguments for call to '%s' but got %d", function->arg_size(), name.c_str(), arguments.size());
	
	std::vector<llvm::Value*> argValues;
	for (auto& expr : arguments)
	{
		llvm::Value* value = expr->Generate();
		argValues.push_back(LoadIfVariable(value, expr));
	}

	return builder->CreateCall(function, argValues);
}

llvm::Value* StructDefinitionExpression::Generate()
{
	return nullptr;
}

Generator::Generator()
{
	PROFILE_FUNCTION();
	
	context = std::make_unique<llvm::LLVMContext>();
	module = std::make_unique<llvm::Module>(llvm::StringRef(), *context);
	builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

static void ResolveParsedTypes(ParseResult& result)
{
	PROFILE_FUNCTION();
	
	// Resolve the primitive types
	{
		Type::FindOrAdd(TypeTag::Int8)->raw    = llvm::Type::getInt8Ty(*context);
		Type::FindOrAdd(TypeTag::Int32)->raw   = llvm::Type::getInt32Ty(*context);
		Type::FindOrAdd(TypeTag::Int64)->raw   = llvm::Type::getInt64Ty(*context);
		Type::FindOrAdd(TypeTag::Float32)->raw = llvm::Type::getFloatTy(*context);
		Type::FindOrAdd(TypeTag::Float64)->raw = llvm::Type::getDoubleTy(*context);
		Type::FindOrAdd(TypeTag::Bool)->raw    = llvm::Type::getInt1Ty(*context);
		Type::FindOrAdd(TypeTag::String)->raw  = llvm::Type::getInt8PtrTy(*context);
		Type::FindOrAdd(TypeTag::Void)->raw    = llvm::Type::getVoidTy(*context);
	}

	//std::vector<llvm::Type*> llvm_members;
	//for (auto& member : struct_members)
	//	members.push_back(member->type->raw);
	//
	//struct_type = llvm::StructType::create(*context, members, type->name);
}

//static llvm::PassBuilder::OptimizationLevel GetLLVMOptimizationLevel(const CommandLineArguments& compilerArgs)
//{
//	switch (compilerArgs.optimizationLevel)
//	{
//		case 0: return llvm::PassBuilder::OptimizationLevel::O0;
//		case 1: return llvm::PassBuilder::OptimizationLevel::O1;
//		case 2: return llvm::PassBuilder::OptimizationLevel::O2;
//		case 3: return llvm::PassBuilder::OptimizationLevel::O3;
//	}
//}

//static void DoOptimizationPasses(const CommandLineArguments& compilerArgs)
//{
//	using namespace llvm;
//
//	PROFILE_FUNCTION();
//
//	LoopAnalysisManager loopAnalysisManager;
//	FunctionAnalysisManager functionAnalysisManager;
//	CGSCCAnalysisManager cgsccAnalysisManager;
//	ModuleAnalysisManager moduleAnalysisManager;
//
//	PassBuilder passBuilder;
//
//	// Register all the basic analyses with the managers
//	passBuilder.registerModuleAnalyses(moduleAnalysisManager);
//	passBuilder.registerCGSCCAnalyses(cgsccAnalysisManager);
//	passBuilder.registerFunctionAnalyses(functionAnalysisManager);
//	passBuilder.registerLoopAnalyses(loopAnalysisManager);
//	passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager, cgsccAnalysisManager, moduleAnalysisManager);
//
//	ModulePassManager modulePassManager = passBuilder.buildPerModuleDefaultPipeline(GetLLVMOptimizationLevel(compilerArgs));
//
//	// Hells ya
//	modulePassManager.run(*module, moduleAnalysisManager);
//}

CompileResult Generator::Generate(ParseResult& parseResult, const CommandLineArguments& compilerArgs)
{
	PROFILE_FUNCTION();
	
	CompileResult result;

	try
	{
		ResolveParsedTypes(parseResult);

		sCurrentScope = new Scope();
		// Codegen module
		for (auto& node : parseResult.Module->children)
		{
			node->Generate();
		}

		// Optimizations
		//if (compilerArgs.optimizationLevel > 0)
		//{
		//	// optimization level 9000%
		//	DoOptimizationPasses(compilerArgs);
		//}
		
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