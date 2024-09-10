#include "pch.h"

#include "Tree.h"
#include "Generator.h"

#include "llvm/Passes/PassBuilder.h"

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

template<typename Expr>
static Expr* To(std::unique_ptr<Expression>& expr)
{
	return dynamic_cast<Expr*>(expr.get());
}

static bool IsAlloca(llvm::Value* v)
{
	return llvm::isa<llvm::AllocaInst>(v);
}

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

static llvm::Value* LoadIfVariable(llvm::Value* generated, std::unique_ptr<Expression>& expr)
{
	if (auto unary = To<UnaryExpression>(expr))
	{
		if (unary->unaryType == UnaryType::AddressOf) // Don't load it if we're trying to get it's address
			return generated;
	}

	if (!IsAlloca(generated) && !llvm::isa<llvm::LoadInst>(generated) && !llvm::isa<llvm::GetElementPtrInst>(generated))
		return generated;

	return builder->CreateLoad(generated);
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
	case TypeTag::Pointer:
		ASSERT(!value.ip64); // ???
		ASSERT(false); // kys
		break;
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
	type = operand->type;

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
		llvm::Value* loaded = builder->CreateLoad(value);
		builder->CreateStore(builder->CreateAdd(loaded, Get1NumericalConstant(loaded->getType())), value);

		// Return newly incremented value
		return builder->CreateLoad(value);
	}
	case UnaryType::PostfixIncrement:
	{
		// Increment
		llvm::Value* loaded = builder->CreateLoad(value);
		builder->CreateStore(builder->CreateAdd(loaded, Get1NumericalConstant(loaded->getType())), value);

		// Return value before increment
		return loaded;
	}
	case UnaryType::PrefixDecrement:
	{
		llvm::Value* loaded = builder->CreateLoad(value);
		builder->CreateStore(builder->CreateSub(loaded, Get1NumericalConstant(loaded->getType())), value);

		return builder->CreateLoad(value);
	}
	case UnaryType::PostfixDecrement:
	{
		llvm::Value* loaded = builder->CreateLoad(value);
		builder->CreateStore(builder->CreateSub(loaded, Get1NumericalConstant(loaded->getType())), value);

		return loaded;
	}
	case UnaryType::AddressOf:
	{
		type = type->GetPointerTo();
		return value; // Industry trade secret - we don't actually take the address of it
	}
	case UnaryType::Deref:
	{
		type = type->GetBaseType();
		llvm::Value* loaded = builder->CreateLoad(value);
		return loaded;
	}
	}

	throw CompileError(sourceLine, "invalid unary operator");
}

llvm::Value* VariableDefinitionStatement::Generate()
{
	llvm::Value* initialVal = nullptr;
	llvm::Type* initializerType = nullptr;

	if (initializer)
	{
		initialVal = initializer->Generate();
		initialVal = LoadIfPointer(initialVal, initializer);

		initializerType = initialVal->getType();
		type = initializer->type;
	}

	llvm::Value* alloc = builder->CreateAlloca(type->raw);
	sCurrentScope->AddValue(std::string(Name.start, Name.length), { type, alloc });

	if (!initialVal)
		return alloc;

	return builder->CreateStore(initialVal, alloc);
}

// todo: improve
static Type* FindNeoTypeFromLLVMType(llvm::Type* type)
{
	PROFILE_FUNCTION();

	for (auto& pair : Type::RegisteredTypes)
	{
		if (pair.second.raw == type)
			return &pair.second;
	}

	if (type->isPointerTy())
		return FindNeoTypeFromLLVMType(type->getContainedType(0));
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

	if (!objectType->IsStruct())
	{
		// This is where u would have used -> instead of . (if I wanted that stupid feature)
		if (objectType->IsPointer())
		{
			objectValue = builder->CreateLoad(objectValue);
			objectType = objectType->GetBaseType();
		}
		else
			throw CompileError(binary->sourceLine, "can't access member of non-struct type or pointer to struct type");
	}

	// rhs should always be variable access expr
	VariableAccessExpression* memberExpr = nullptr;
	if (!(memberExpr = To<VariableAccessExpression>(binary->right)))
		throw CompileError(binary->sourceLine, "expected variable access expression for rhs of member access");

	const std::string& targetMemberName = memberExpr->name;

	// Find member index
	const auto& members = objectType->Struct.members;
	int memberIndex = -1;
	for (uint32_t i = 0; i < members.size(); i++)
	{
		auto& member = members[i];
		if (member.name == targetMemberName)
		{
			binary->type = member.type;
			memberIndex = i;
			break;
		}
	}
	if (memberIndex == -1)
		throw CompileError(binary->sourceLine, "'%s' not a member of struct '%s'", memberExpr->name.c_str(), objectType->name.c_str());

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

llvm::Value* BinaryExpression::Generate()
{
	PROFILE_FUNCTION();

	using namespace llvm;

	if (binaryType == BinaryType::MemberAccess)
		return GenerateStructureMemberAccessExpression(this);

	llvm::Value* lhs = left->Generate();
	llvm::Value* rhs = right->Generate();

	type = left->type;

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

	llvm::BranchInst* branchInst = builder->CreateCondBr(ifBranch.condition->Generate(), trueBlock, falseBlock);
	builder->SetInsertPoint(endBlock);

	return branchInst;
}

llvm::Value* LoopControlFlowExpression::Generate()
{
	return nullptr;
}

llvm::Value* LoopExpression::Generate()
{
	PROFILE_FUNCTION();

	BinaryExpression* rangeOperand = nullptr;
	if (!IsRange(range, &rangeOperand))
		throw CompileError(sourceLine, "expected range expression for loop");

	auto minimum = rangeOperand->left->Generate();
	auto maximum = rangeOperand->right->Generate();

	// If reading variable, treat it as underlying value so the compiler does compiler stuff.
	minimum = LoadIfVariable(minimum, rangeOperand->left);
	maximum = LoadIfVariable(maximum, rangeOperand->right);

	sCurrentScope = sCurrentScope->Deepen();

	// Create iterator variable
	llvm::Value* iteratorValuePtr = builder->CreateAlloca(minimum->getType());
	{
		sCurrentScope->AddValue(iteratorVariableName, { rangeOperand->left->type, iteratorValuePtr });
		// Starts at the range's minimum
		builder->CreateStore(minimum, iteratorValuePtr);
	}

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

		llvm::Value* iteratorVal = builder->CreateLoad(iteratorValuePtr);
		llvm::Type* iteratorType = iteratorVal->getType();

		// TODO: abstract
		switch (iteratorType->getTypeID())
		{
			case llvm::Type::IntegerTyID:
			{
				bShouldContinue = builder->CreateICmpSLT(iteratorVal, maximum);
				break;
			}
			case llvm::Type::FloatTyID:
			{
				bShouldContinue = builder->CreateFCmpULE(iteratorVal, maximum);
				break;
			}
		}

		builder->CreateCondBr(bShouldContinue, bodyBlock, endBlock);
	}

	// Increment block:
	//	Increment iterator
	//	Jump to condition block
	builder->SetInsertPoint(incrementBlock);
	{
		llvm::Value* iteratorVal = builder->CreateLoad(iteratorValuePtr);
		builder->CreateStore(builder->CreateAdd(iteratorVal, Get1NumericalConstant(iteratorVal->getType()), "inc"), iteratorValuePtr);
		builder->CreateBr(conditionBlock);
	}

	// Body block:
	//	Body
	//	Jump to increment block
	builder->SetInsertPoint(bodyBlock);
	{
		for (auto& expr : body)
			expr->Generate();

		builder->CreateBr(incrementBlock);
	}
	
	builder->SetInsertPoint(parentBlock);
	builder->CreateBr(conditionBlock);
	builder->SetInsertPoint(endBlock);

	sCurrentScope = sCurrentScope->Increase();

	return nullptr;
}

llvm::Value* FunctionDefinitionExpression::Generate()
{
	PROFILE_FUNCTION();

	type = Type::FindOrAdd(prototype.ReturnType->name);
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

		std::string name = std::string(parameter->Name.start, parameter->Name.length);
		arg.setName(name);

		if (!hasBody)
			continue;

		// Alloc arg
		llvm::Value* alloc = builder->CreateAlloca(arg.getType());
		builder->CreateStore(&arg, alloc);
		sCurrentScope->AddValue(name, { parameter->type, alloc });
	}

	// Gen body
	if (hasBody)
	{
		PROFILE_SCOPE("Generate function body");

		for (auto& node : body)
			node->Generate();

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

llvm::Value* ReturnStatement::Generate()
{
	llvm::Value* generated = value->Generate();
	generated = LoadIfVariable(generated, value);

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
	
	std::vector<llvm::Value*> argValues;
	uint32_t i = 0;
	for (auto& expr : arguments)
	{
		llvm::Value* value = expr->Generate();
		value = LoadIfVariable(value, expr);

		llvm::Type* expectedTy = function->getArg(i++)->getType();
		if (expectedTy != value->getType())
		{
			throw CompileError(sourceLine, "invalid type for argument %d passed to '%s'", i - 1, name.c_str());
		}

		argValues.push_back(value);
	}

	return builder->CreateCall(function, argValues);
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
			memberType->name.c_str(), std::string(vardef->Name.start, vardef->Name.length).c_str(), name.c_str());
	}

	return nullptr;
}

Generator::Generator()
{
	context = std::make_unique<llvm::LLVMContext>();
	module = std::make_unique<llvm::Module>(llvm::StringRef(), *context);
	builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

static void ResolveType(const std::string& name, Type& type)
{
	PROFILE_FUNCTION();

	// Already resolved?
	if (type.raw)
		return;

	if (type.IsStruct())
	{
		auto& members = type.Struct.members;

		std::vector<llvm::Type*> memberTypes;
		memberTypes.reserve(members.size());

		for (auto& member : members)
		{
			ResolveType(member.type->name, *member.type);
			memberTypes.push_back(member.type->raw);
		}

		type.raw = llvm::StructType::create(*context, memberTypes, name);
		return;
	}
	else if (type.IsPointer())
	{
		// *i32 -> i32
		std::string containedName = std::string(name).erase(0, 1);
		Type* contained = Type::FindOrAdd(containedName);
		ResolveType(containedName, *contained);
		type.raw = llvm::PointerType::get(contained->raw, 0u); // Magic address space of 0???
		return;
	}

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

// todo: abstract?
static void VisitFunctionDefinitions(ParseResult& result)
{
	PROFILE_FUNCTION();

	// only works for top level functions rn
	for (auto& node : result.Module->children)
	{
		if (node->nodeType != NodeType::FunctionDefinition)
			continue;

		auto definition = dynamic_cast<FunctionDefinitionExpression*>(node.get());
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

		llvm::FunctionType* functionType = llvm::FunctionType::get(retType, parameterTypes, false);
		llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, prototype.Name.c_str(), *module);
	}
}

static void ResolveParsedTypes(ParseResult& result)
{
	PROFILE_FUNCTION();

	for (auto& pair : Type::RegisteredTypes)
	{
		const auto& name = pair.first;
		Type& type = pair.second;

		ResolveType(name, type);
	}

	VisitFunctionDefinitions(result);
}

Type* Type::GetBaseType() const
{
	ASSERT(IsPointer());
	Type* baseType = FindOrAdd(name.substr(1));
	ResolveType(baseType->name, *baseType);

	return baseType;
}

Type* Type::GetPointerTo() const
{
	Type* pointerTy = FindOrAdd('*' + name);
	ResolveType(pointerTy->name, *pointerTy);

	return pointerTy;
}

static void DoOptimizationPasses(const CommandLineArguments& compilerArgs)
{
	using namespace llvm;

	PROFILE_FUNCTION();

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