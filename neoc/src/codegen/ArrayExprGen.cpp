#include "pch.h"

#include "Tree.h"

#include "Generator.h"
#include <llvm/IR/InstrTypes.h>

#include "CodegenUtils.h"

llvm::Value* Generator::CreateArrayAlloca(llvm::Type* arrayType, const std::vector<std::unique_ptr<Expression>>& elements)
{
	PROFILE_FUNCTION();

	std::vector<llvm::Value*> values;
	values.reserve(elements.size());

	for (auto& expr : elements)
	{
		llvm::Value* element = expr->Generate();
		//if (elementType && (elementType != element->getType()))
		//	//throw CompileError(sourceLine, "expected {} as array element but got {} (index = {})", )
		//	throw CompileError(sourceLine, "array element type mismatch (index = {})", i);

		values.push_back(element);
	}

	llvm::Value* alloc = Generator::EmitAlloca(arrayType);

	// initialize elements (store into gep)
	uint64_t i = 0;
	llvm::Value* zeroIndex = Generator::GetNumericConstant(TypeTag::Int32, 0);
	for (llvm::Value* value : values)
	{
		llvm::Value* index = Generator::GetNumericConstant(TypeTag::Int32, i);
		Generator::EmitStore(values[i], Generator::EmitInBoundsGEP(arrayType, alloc, { zeroIndex, index }));

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
		value = Generator::EmitAlloca(arrayTy);
	}

	Generator::ScopeValue(definition->name, { type, value });
	return value;
}

// returns ptr
static llvm::Value* GetArrayElement(llvm::Value* arrayPtr, llvm::Value* index)
{
	static llvm::Value* zeroIndex = Generator::GetNumericConstant(TypeTag::Int32, 0);

	llvm::Type* arrayType = arrayPtr->getType()->getContainedType(0);
	return Generator::EmitInBoundsGEP(arrayType, arrayPtr, { zeroIndex, index });
}
static llvm::Value* GetArrayElement(llvm::Value* arrayPtr, uint64_t index)
{
	return GetArrayElement(arrayPtr, Generator::GetNumericConstant(TypeTag::Int32, index));
}