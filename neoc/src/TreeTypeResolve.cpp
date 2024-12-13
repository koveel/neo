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
}

void CompoundStatement::ResolveType()
{
}

void VariableDefinitionExpression::ResolveType()
{
}

void ArrayInitializationExpression::ResolveType()
{
	//
}

void ArrayDefinitionExpression::ResolveType()
{
}

void VariableAccessExpression::ResolveType()
{
}

void FunctionDefinitionExpression::ResolveType()
{
}

void FunctionCallExpression::ResolveType()
{
}

void ReturnStatement::ResolveType()
{
}
