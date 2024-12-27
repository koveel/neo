#include "pch.h"

#include "Type.h"
#include "Cast.h"

std::unordered_map<std::pair<Type*, Type*>, Cast> Cast::sValidCasts;

Cast* Cast::Add(Type* from, Type* to, Function fn, bool allowedImplicitly)
{
	return &(sValidCasts[{from, to}] = Cast(to, from, fn, allowedImplicitly));
}

Cast* Cast::IsValid(Type* from, Type* to)
{
	if (!sValidCasts.count({ from, to }))
		return nullptr;

	return &sValidCasts[{from, to}];
}

Cast* Cast::IsImplicit(Type* from, Type* to)
{
	Cast* result = IsValid(from, to);
	if (!result || !result->implicit)
		return nullptr;

	return result;
}

llvm::Value* Cast::Invoke(llvm::Value* from)
{
	return function(*this, from, to->raw);
}
