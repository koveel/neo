#include "pch.h"

#include "Tree.h"

void UnaryExpression::ResolveType()
{
}

void BinaryExpression::ResolveType()
{
	left->ResolveType();
	right->ResolveType();
	type = left->type;

	if (binaryType == BinaryType::Subscript && left->type)
		type = left->type->GetContainedType();
}

void CompoundExpression::ResolveType()
{
	for (auto& expr : children)
		expr->ResolveType();
}

void LoopExpression::ResolveType()
{
	for (auto& expr : body)
		expr->ResolveType();
}

void ConstantDefinitionExpression::ResolveType()
{
}

void VariableDefinitionExpression::ResolveType()
{
	if (initializer)
	{
		initializer->ResolveType();

		//if (!type)
		//	type = initializer->type;
	}
}

void CastExpression::ResolveType()
{

}

void VariableAccessExpression::ResolveType()
{
}

void FunctionDefinitionExpression::ResolveType()
{
	for (auto& expr : body)
		expr->ResolveType();

	for (auto& expr : prototype.Parameters)
		expr->ResolveType();
}

void FunctionCallExpression::ResolveType()
{
}

void ReturnStatement::ResolveType()
{
	if (!value)
		return;
}
