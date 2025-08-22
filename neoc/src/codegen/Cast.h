#pragma once

#include "Type.h"

namespace std
{
	template<> struct hash<pair<Type*, Type*>>
	{
		std::size_t operator()(const std::pair<Type*, Type*>& k) const
		{
			size_t seed = 0;
			hash_combine(seed, (uintptr_t)k.first);
			hash_combine(seed, (uintptr_t)k.second);
			return seed;
		}
	};
	template<> struct hash<pair<TypeTag, TypeTag>>
	{
		std::size_t operator()(const std::pair<TypeTag, TypeTag>& k) const
		{
			size_t seed = 0;
			hash_combine(seed, (uint32_t)k.first);
			hash_combine(seed, (uint32_t)k.second);
			return seed;
		}
	};
}

class Cast
{
public:
	using Function = llvm::Value*(*)(Cast&, llvm::Value*, llvm::Type*);

	Cast() = default;
private:
	Cast(Type* to, Type* from, Function fn, bool allowedImplicitly = false)
		: to(to), from(from), function(fn), implicit(allowedImplicitly)
	{}
public:
	static Cast* Add(TypeTag from, TypeTag to, Function func, bool allowedImplicitly = false);
	static Cast* Add(Type* from, Type* to, Function func, bool allowedImplicitly = false);

	static Cast* IsValid(TypeTag from, TypeTag to);
	static Cast* IsValid(Type* from, Type* to);

	static Cast* IsImplicit(TypeTag from, TypeTag to);
	static Cast* IsImplicit(Type* from, Type* to);

	llvm::Value* Invoke(llvm::Value* from, struct CastExpression* expression = nullptr);
public:
	Type* to = nullptr, *from = nullptr;
	Function function = nullptr;
	bool implicit = false;
private:
	static std::unordered_map<std::pair<TypeTag, TypeTag>, Cast> s_PrimitiveCasts;
	static std::unordered_map<std::pair<Type*, Type*>, Cast> s_TypeCasts;
};