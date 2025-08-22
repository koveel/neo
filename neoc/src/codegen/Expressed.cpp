#include "pch.h"

#include "Generator.h"
#include "CodegenUtils.h"
#include <llvm/IR/IRBuilder.h>

#include "Cast.h"

#if false

Expressed::Expressed(llvm::Value* raw, Expression* expr)
	: raw(raw), type(expr->type), expression(expr)
{
}

Expressed::Expressed(llvm::Value* raw, Type* type, Expression* expr)
	: raw(raw), type(type), expression(expr)
{
}

bool Expressed::IsPointer() const
{
	return type->IsPointer();
}

bool Expressed::IsVariable() const
{
	bool isAlloca = llvm::isa<llvm::AllocaInst>(raw);
	bool isGep = llvm::isa<llvm::GetElementPtrInst>(raw);

	return isAlloca || isGep;
	return isAlloca;
}

bool Expressed::IsSubscript() const
{
	return ToExpr<BinaryExpression>(expression);
}

static bool UnaryExprImpliesLoad(UnaryExpression* unary)
{
	if (unary->unaryType == UnaryType::AddressOf || unary->unaryType == UnaryType::Deref)
		return false;
}

Expressed Expressed::TryImplicitLoadIfNecessary(Generator& generator) const
{
	bool isConstantExpr = expression->nodeType == NodeType::Primary || expression->nodeType == NodeType::String; // todo: combine those nodes wtf
	if (isConstantExpr)
		return *this;

	if (UnaryExpression* unary = ToExpr<UnaryExpression>(expression))
	{
		if (!UnaryExprImpliesLoad(unary))
			return *this;

		return Load(generator);
	}

	bool isVariableAccess = expression->nodeType == NodeType::VariableAccess;
	bool isGep = llvm::isa<llvm::GetElementPtrInst>(raw);

	if (!isVariableAccess && !isGep)
		return *this;

	return Load(generator);
}

Expressed Expressed::Load(Generator& generator) const
{
	Type* result_type = type->contained;
	llvm::Value* result_value = generator.EmitLoad(result_type, raw);
	return { result_value, result_type, expression };
}

//void Expressed::EmitLoadIfVariable(Generator& generator)
//{
//	if (!IsVariable())
//		return;
//
//	EmitLoad(generator);
//}

#endif