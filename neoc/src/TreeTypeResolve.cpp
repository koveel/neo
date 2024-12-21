#include "pch.h"

#include "Tree.h"
 
void UnaryExpression::ResolveType()
{
	operand->ResolveType();
	type = operand->type;

	switch (unaryType)
	{
	case UnaryType::AddressOf:
	{
		type = type->GetPointerTo();
		break;
	}
	case UnaryType::Deref:
	{
		type = type->GetContainedType();
		break;
	}
	default:
		break;
	}
}

void BinaryExpression::ResolveType()
{
	left->ResolveType();
	right->ResolveType();
	type = left->type;

	//if (binaryType == BinaryType::Subscript)
	//	type = left->type->GetContainedType();
}

void CompoundStatement::ResolveType()
{
	for (auto& expr : children)
		expr->ResolveType();
}

void LoopExpression::ResolveType()
{
	for (auto& expr : body)
		expr->ResolveType();
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

void ArrayDefinitionExpression::ResolveType()
{
}

void VariableAccessExpression::ResolveType()
{
}

//void LoopExp

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
	value->ResolveType();
	type = value->type;
}
