#include "pch.h"

#include "Tree.h"

#include "Generator.h"
#include <llvm/IR/InstrTypes.h>

#include "CodegenUtils.h"

llvm::Value* Generator::CreateArrayAlloca(ArrayType* arrayType, const std::vector<std::unique_ptr<Expression>>& elements, const char* debug_name)
{
	PROFILE_FUNCTION();

	std::vector<llvm::Value*> values;
	values.reserve(elements.size());

	for (auto& expr : elements)
	{
		Value elementv = expr->Generate(*this);
		RValue rv = MaterializeToRValue(elementv);
		//if (elementType && (elementType != element->getType()))
		//	//throw CompileError(sourceLine, "expected {} as array element but got {} (index = {})", )
		//	throw CompileError(sourceLine, "array element type mismatch (index = {})", i);

		values.push_back(rv.value);
	}

	llvm::Value* alloc = EmitAlloca(arrayType, nullptr, debug_name);
	//StructType* containedStructType = arrayType->contained->IsStruct();

	// initialize elements (store into gep)
	uint64_t i = 0;
	llvm::Value* zeroIndex = GetNumericConstant(TypeTag::Int32, 0);
	for (llvm::Value* value : values)
	{
		llvm::Value* index = GetNumericConstant(TypeTag::Int32, i);
		//if (containedStructType)
		//	value = EmitLoad(containedStructType, value);

		EmitStore(value, EmitInBoundsGEP(arrayType, alloc, { zeroIndex, index }));

		i++;
	}

	return alloc;
}

//Value ArrayDefinitionExpression::Generate(Generator& generator)
//{
//	Value value = {};
//	if (initializer)
//	{
//		value = initializer->Generate(generator);
//		type = initializer->type;
//	}
//	else
//	{
//		type = ArrayType::Get(type, capacity);
//		value = { generator.EmitAlloca(type), this };
//	}
//
//	generator.ScopeValue(variableDef->name, { type, value.raw });
//	return value;
//}