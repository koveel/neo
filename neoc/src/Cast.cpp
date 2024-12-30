#include "pch.h"

#include "Lexer.h"
#include "Tree.h"
#include "Cast.h"

std::unordered_map<std::pair<TypeTag, TypeTag>, Cast> Cast::sPrimitiveCasts;
std::unordered_map<std::pair<Type*, Type*>, Cast> Cast::sTypeCasts;

Cast* Cast::Add(TypeTag from, TypeTag to, Function fn, bool allowedImplicitly)
{
	return &(sPrimitiveCasts[{from, to}] = Cast(Type::Get(to), Type::Get(from), fn, allowedImplicitly));
}

Cast* Cast::Add(Type* from, Type* to, Function fn, bool allowedImplicitly)
{
	return &(sTypeCasts[{from, to}] = Cast(to, from, fn, allowedImplicitly));
}

Cast* Cast::IsValid(TypeTag from, TypeTag to)
{
	if (!sPrimitiveCasts.count({ from, to }))
		return nullptr;

	return &sPrimitiveCasts[{from, to}];
}

Cast* Cast::IsValid(Type* from, Type* to)
{
	if (!sTypeCasts.count({ from, to }))
		return nullptr;

	return &sTypeCasts[{from, to}];
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
