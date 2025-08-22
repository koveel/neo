#include "pch.h"

#include "Cast.h"
#include "Tree.h"

std::unordered_map<std::pair<TypeTag, TypeTag>, Cast> Cast::s_PrimitiveCasts;
std::unordered_map<std::pair<Type*, Type*>, Cast> Cast::s_TypeCasts;

Cast* Cast::Add(TypeTag from, TypeTag to, Function fn, bool allowedImplicitly)
{
	return &(s_PrimitiveCasts[{from, to}] = Cast(Type::Get(to), Type::Get(from), fn, allowedImplicitly));
}

Cast* Cast::Add(Type* from, Type* to, Function fn, bool allowedImplicitly)
{
	return &(s_TypeCasts[{from, to}] = Cast(to, from, fn, allowedImplicitly));
}

Cast* Cast::IsValid(TypeTag from, TypeTag to)
{
	if (!s_PrimitiveCasts.count({ from, to }))
		return nullptr;

	return &s_PrimitiveCasts[{from, to}];
}

Cast* Cast::IsValid(Type* from, Type* to)
{
	if (!s_TypeCasts.count({ from, to }))
		return nullptr;

	return &s_TypeCasts[{from, to}];
}

Cast* Cast::IsImplicit(TypeTag from, TypeTag to)
{
	Cast* result = IsValid(from, to);
	if (!result || !result->implicit)
		return nullptr;

	return result;
}

Cast* Cast::IsImplicit(Type* from, Type* to)
{
	Cast* result = IsValid(from, to);
	if (!result || !result->implicit)
		return nullptr;

	return result;
}

llvm::Value* Cast::Invoke(llvm::Value* from, CastExpression* expression)
{
	return function(*this, from, expression ? expression->type->raw : to->raw);
}
