#include "pch.h"

#include "Cast.h"
#include "Tree.h"

std::unordered_map<std::pair<Type*, Type*>, Cast> Cast::s_AllowedCasts;

Cast* Cast::Add(Type* from, Type* to, Function fn, bool allowedImplicitly)
{
	return &(s_AllowedCasts[{from, to}] = Cast(to, from, fn, allowedImplicitly));
}

Cast* Cast::IsValid(Type* from, Type* to)
{
	auto it = s_AllowedCasts.find({ from, to });
	if (it == s_AllowedCasts.end())
		return nullptr;

	return &it->second;
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
