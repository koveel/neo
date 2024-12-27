#pragma once

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
	static Cast* Add(Type* from, Type* to, Function func, bool allowedImplicitly = false);
	static Cast* IsValid(Type* from, Type* to);
	static Cast* IsImplicit(Type* from, Type* to);

	llvm::Value* Invoke(llvm::Value* from);
public:
	Type* to = nullptr, *from = nullptr;
	Function function = nullptr;
	bool implicit = false;
private:
	static std::unordered_map<std::pair<Type*, Type*>, Cast> sValidCasts;
};