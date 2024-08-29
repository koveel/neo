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

static Scope* sCurrentScope = nullptr;
static llvm::Function* sCurrentFunction = nullptr;

template<typename... Args>
static void Warn(int line, const std::string& message, Args&&... args)
{
	SetConsoleColor(14);
	fprintf(stdout, "[line %d] warning: %s\n", line, FormatString(message.c_str(), std::forward<Args>(args)...).c_str());
	ResetConsoleColor();
}

static bool IsAlloca(llvm::Value* v)
{
	return llvm::isa<llvm::AllocaInst>(v);
}

static llvm::Value* LoadIfVariable(llvm::Value* generated, std::unique_ptr<Expression>& expr)
{
	if (expr->nodeType != NodeType::VariableAccess || !IsAlloca(generated))
		return generated;

	return builder->CreateLoad(generated, false, "loadtmp");
}

template<typename Expr>
static Expr* To(std::unique_ptr<Expression>& expr)
{
	return static_cast<Expr*>(expr.get());
}

static llvm::Value* LoadIfPointer(llvm::Value* value, std::unique_ptr<Expression>& expr)
{
	llvm::Type* type = value->getType();
	if (auto unary = To<UnaryExpression>(expr))
	{
		if (unary->unaryType == UnaryType::AddressOf) // Don't load it if we're trying to get it's address
			return value;
	}

	bool isPointer = type->isPointerTy();
	bool isString = type->getNumContainedTypes() == 1 && type->getContainedType(0)->isIntegerTy(8);

	if (!isPointer || isString)
		return value;

	return builder->CreateLoad(value);
}

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

llvm::Value* UnaryExpression::Generate()
{
	PROFILE_FUNCTION();

	llvm::Value* value = operand->Generate();

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
		return value; // Industry trade secret - we don't actually take the address of it
	}
	case UnaryType::Deref:
	{
		llvm::Value* loaded = builder->CreateLoad(value, false, "loadtmp");
		return loaded;
	}
	}

	throw CompileError(sourceLine, "invalid unary operator");
}

llvm::Value* VariableDefinitionStatement::Generate()
{
	llvm::Value* initialVal = nullptr;
	llvm::Type* ty = nullptr;
	if (initializer)
	{
		initialVal = LoadIfVariable(initializer->Generate(), initializer);
		initialVal = LoadIfPointer(initialVal, initializer);

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

	//if (loadValue)
	//	return builder->CreateLoad(value.raw);

	return value.raw;
}

llvm::Value* BinaryExpression::Generate()
{
	using namespace llvm;

	llvm::Value* lhs = left->Generate();
	llvm::Value* rhs = right->Generate();

	// num = 5;     *i32  = i32
	// pnum = @num; **i32 = *i32
	// *pnum = 10;  *i32  = i32
	// num2 = *pnum; // *i32 = i32
	// num2 = num;

	// Unless assiging, treat variables as the underlying values
	if (binaryType != BinaryType::Assign)
	{
		lhs = LoadIfVariable(lhs, left);
		rhs = LoadIfVariable(rhs, right);
	}

	llvm::Type* lhsType = lhs->getType();
	llvm::Type* rhsType = rhs->getType();

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
		builder->CreateStore(rhs, lhs);
		return rhs;
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

llvm::Value* FunctionDefinitionExpression::Generate()
{
	PROFILE_FUNCTION();

	type = Type::FindOrAdd(prototype.ReturnType->name);
	bool hasBody = body.size();

	// Full definition for function
	llvm::Function* function = module->getFunction(prototype.Name.c_str());
	llvm::BasicBlock* previousBlock = builder->GetInsertBlock();
	llvm::BasicBlock* bodyBlock = nullptr;

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
		if (!function->empty())
			throw CompileError(sourceLine, "function cannot be redefined");

		sCurrentFunction = function;

		if (hasBody)
		{
			sCurrentScope = sCurrentScope->Deepen(); // No point in scoping args if there's no body

			bodyBlock = llvm::BasicBlock::Create(*context, "entry", sCurrentFunction);
			builder->SetInsertPoint(bodyBlock);
		}

		// Set names for function args
		for (auto& arg : function->args())
		{
			auto nameView = prototype.Parameters[i++]->Name;
			std::string name = std::string(nameView.start, nameView.length);
			arg.setName(name);

			if (hasBody)
			{
				llvm::Value* alloc = builder->CreateAlloca(arg.getType());
				builder->CreateStore(&arg, alloc);
				sCurrentScope->AddValue(name, { alloc });
			}
		}
	}

	if (hasBody)
	{
		PROFILE_SCOPE("Generate function body");

		for (auto& node : body)
			node->Generate();

		if (!bodyBlock->getTerminator())
		{
			if (type->tag != TypeTag::Void)
				throw CompileError(sourceLine, "expected return statement in function '%s' (%s)", prototype.Name.c_str(), type->name.c_str());

			builder->CreateRet(nullptr);
		}

		sCurrentScope = sCurrentScope->Increase();
		builder->SetInsertPoint(previousBlock);
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
	uint32_t i = 0;
	for (auto& expr : arguments)
	{
		llvm::Value* value = expr->Generate();
		value = LoadIfVariable(value, expr);

		if (function->getArg(i++)->getType() != value->getType())
		{
			//throw CompileError(sourceLine, "invalid type for argument %d passed to '%s' - expected %s but received %s", i, name.c_str());
			throw CompileError(sourceLine, "invalid type for argument %d passed to '%s'", i - 1, name.c_str());
		}

		argValues.push_back(value);
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

static void ResolveType(const std::string& name, Type& type)
{
	// Already resolved?
	if (type.raw)
		return;

	bool isPointer = name[0] == '*';
	bool isStruct = type.tag == TypeTag::Struct;

	if (isPointer)
	{
		// *i32 -> i32
		std::string containedName = std::string(name).erase(0, 1);
		Type* contained = Type::FindOrAdd(containedName);
		ResolveType(containedName, *contained);
		type.raw = llvm::PointerType::get(contained->raw, 0u); // Magic address space of 0???

		return;
	}
	else if (!isPointer)
	{
		switch (type.tag)
		{
			case TypeTag::Int8:
			{
				type.raw = llvm::Type::getInt8Ty(*context);
				break;
			}
			case TypeTag::Int16:
			{
				type.raw = llvm::Type::getInt16Ty(*context);
				break;
			}
			case TypeTag::Int32:
			{
				type.raw = llvm::Type::getInt32Ty(*context);
				break;
			}
			case TypeTag::Int64:
			{
				type.raw = llvm::Type::getInt64Ty(*context);
				break;
			}
			case TypeTag::Float32:
			{
				type.raw = llvm::Type::getFloatTy(*context);
				break;
			}
			case TypeTag::Float64:
			{
				type.raw = llvm::Type::getDoubleTy(*context);
				break;
			}
			case TypeTag::Bool:
			{
				type.raw = llvm::Type::getInt1Ty(*context);
				break;
			}
			case TypeTag::String:
			{
				type.raw = llvm::Type::getInt8PtrTy(*context);
				break;
			}
			case TypeTag::Void:
			{
				type.raw = llvm::Type::getVoidTy(*context);
				break;
			}
		}
	}
}

static void ResolveParsedTypes(ParseResult& result)
{
	PROFILE_FUNCTION();
	
	// Resolve the primitive types
	//{
	//	Type::FindOrAdd(TypeTag::Int8)->raw    = llvm::Type::getInt8Ty(*context);
	//	Type::FindOrAdd(TypeTag::Int32)->raw   = llvm::Type::getInt32Ty(*context);
	//	Type::FindOrAdd(TypeTag::Int64)->raw   = llvm::Type::getInt64Ty(*context);
	//	Type::FindOrAdd(TypeTag::Float32)->raw = llvm::Type::getFloatTy(*context);
	//	Type::FindOrAdd(TypeTag::Float64)->raw = llvm::Type::getDoubleTy(*context);
	//	Type::FindOrAdd(TypeTag::Bool)->raw    = llvm::Type::getInt1Ty(*context);
	//	Type::FindOrAdd(TypeTag::String)->raw  = llvm::Type::getInt8PtrTy(*context);
	//	Type::FindOrAdd(TypeTag::Void)->raw    = llvm::Type::getVoidTy(*context);
	//}

	for (auto& pair : Type::RegisteredTypes)
	{
		const auto& name = pair.first;
		Type& type = pair.second;

		ResolveType(name, type);
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