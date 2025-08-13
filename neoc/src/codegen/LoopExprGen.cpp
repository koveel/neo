#include "pch.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>

#include "Tree.h"
#include "CodegenUtils.h"

#include "Cast.h"
#include "Generator.h"
#include "GeneratorContext.h"

//static struct
//{
//
//} s_CurrentLoop;

llvm::Value* LoopControlFlowExpression::Generate(Generator& generator)
{
	// TODO

	return nullptr;
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

llvm::Value* LoopExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

	Type* indexType = Type::Get(TypeTag::Int32);

	ArrayType* arrayType = nullptr;
	llvm::Value* arrayPtr = nullptr;

	Type* iteratorType = range->type;
	llvm::Value* indexValuePtr = generator.EmitAlloca(indexType);
	llvm::Value* maximumIndex = nullptr;
	llvm::Value* iteratorValuePtr = nullptr; // For arrays, the value in the array

	generator.currentScope = generator.currentScope->Deepen();

	bool iteratingArray = false;
	BinaryExpression* rangeOperand = nullptr;
	if (!IsRange(range, &rangeOperand))
	{
		arrayPtr = range->Generate(generator);
		bool isAlloca = llvm::isa<llvm::AllocaInst>(arrayPtr); // Not sure if this will always be true
		ASSERT(isAlloca);

		llvm::AllocaInst* alloc = llvm::cast<llvm::AllocaInst>(arrayPtr);
		llvm::Type* llvmArrayType = alloc->getAllocatedType();
		ASSERT(llvmArrayType->isArrayTy());
		arrayType = Type::FromLLVM(llvmArrayType)->IsArray();
		ASSERT(arrayType);

		//if (!arrayType->isArrayTy())
		//	throw CompileError(range->sourceLine, "expected an object of array type to iterate");
		maximumIndex = generator.GetNumericConstant(TypeTag::Int32, arrayType->count);

		// Init iterator
		iteratorType = arrayType->contained;
		iteratorValuePtr = generator.EmitAlloca(iteratorType);

		// gep
		llvm::Value* zero = generator.GetNumericConstant(TypeTag::Int32, 0);
		llvm::Value* initialValue = generator.EmitInBoundsGEP(arrayType, arrayPtr, { zero, zero });

		initialValue = generator.EmitLoad(arrayType->contained, initialValue);
		generator.EmitStore(initialValue, iteratorValuePtr);
		generator.EmitStore(zero, indexValuePtr);

		iteratingArray = true;

		generator.currentScope->AddValue(iteratorVariableName, { iteratorType, iteratorValuePtr });
	}
	else
	{
		auto minimum = rangeOperand->left->Generate(generator);
		auto maximum = rangeOperand->right->Generate(generator);

		// If reading variable, treat it as underlying value so the compiler does compiler stuff.
		minimum = generator.LoadValueIfVariable(minimum, rangeOperand->left);
		maximum = generator.LoadValueIfVariable(maximum, rangeOperand->right);
		maximumIndex = maximum;

		iteratorType = rangeOperand->left->type;
		generator.EmitStore(minimum, indexValuePtr);

		generator.currentScope->AddValue(iteratorVariableName, { iteratorType, indexValuePtr });
	}

	auto& context = generator.llvm_context->context;
	auto& builder = generator.llvm_context->builder;

	auto& current_function = generator.llvm_context->current_function;
	auto& llvm_current_function = current_function.function;

	// Blocks
	llvm::BasicBlock* parentBlock = builder->GetInsertBlock();
	llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "for_end", llvm_current_function, current_function.return_block);

	llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(*context, "for_cond", llvm_current_function, endBlock);
	llvm::BasicBlock* incrementBlock = llvm::BasicBlock::Create(*context, "for_inc", llvm_current_function, endBlock);
	llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "for_body", llvm_current_function, endBlock);

	// Condition block
	//	If iterator < rangeMax: jump to body block, otherwise jump to end block
	builder->SetInsertPoint(conditionBlock);
	{
		llvm::Value* bShouldContinue = nullptr;

		llvm::Value* iteratorVal = generator.EmitLoad(indexType, indexValuePtr);
		llvm::Type* iteratorType = iteratorVal->getType();

		bShouldContinue = builder->CreateICmpSLT(iteratorVal, maximumIndex);

		builder->CreateCondBr(bShouldContinue, bodyBlock, endBlock);
	}

	// Increment block:
	//	Increment iterator
	//	Jump to condition block
	builder->SetInsertPoint(incrementBlock);
	{
		llvm::Value* iteratorVal = generator.EmitLoad(indexType, indexValuePtr);
		generator.EmitStore(builder->CreateAdd(iteratorVal, generator.GetNumericConstant(TypeTag::Int32, 1), "inc"), indexValuePtr);
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
			llvm::Value* currentElementPtr = generator.EmitInBoundsGEP(arrayType, arrayPtr,
				{ zeroIndex, generator.EmitLoad(indexType, indexValuePtr) });
			generator.EmitStore(generator.EmitLoad(arrayType->contained, currentElementPtr), iteratorValuePtr);
		}

		for (auto& expr : body)
			expr->Generate(generator);

		builder->CreateBr(incrementBlock);
	}

	builder->SetInsertPoint(parentBlock);
	builder->CreateBr(conditionBlock);
	builder->SetInsertPoint(endBlock);

	generator.currentScope = generator.currentScope->Increase();

	return endBlock;
}