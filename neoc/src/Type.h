#pragma once

enum class TypeTag
{
	Unresolved = 0,

	Void, Pointer,
	// Integral
	UInt8, UInt16, UInt32, UInt64,
	Int8, Int16, Int32, Int64,
	// Floatin' .
	Float32, Float64,
	Bool,
	// Other!!!
	String,
	Array,
	Struct,
	Alias,

	COUNT,
};

class Type;
class AliasType;
class StructType;
class ArrayType;

namespace std
{
	template<> struct hash<pair<TypeTag, Type*>>
	{
		std::size_t operator()(const std::pair<TypeTag, Type*>& k) const
		{
			size_t seed = 0;
			hash_combine(seed, (uint64_t)k.first);
			hash_combine(seed, (uintptr_t)k.second);
			return seed;
		}
	};

	template<> struct hash<pair<Type*, uint64_t>>
	{
		std::size_t operator()(const std::pair<Type*, uint64_t>& k) const
		{
			size_t seed = 0;
			hash_combine(seed, (uintptr_t)k.first);
			hash_combine(seed, k.second);
			return seed;
		}
	};
}

class Type
{
public:
	using Key = std::pair<TypeTag, Type*>;
public:
	Type() = default;
	Type(Key spec, llvm::Type* raw = nullptr)
		: tag(spec.first), contained(spec.second), raw(raw) {}

	bool operator==(const Type* other) const { return this == other; }
	bool operator!=(const Type* other) const { return this != other; }

	virtual std::string GetName() { return TagToString(tag, this); }

	StructType* IsStruct();
	ArrayType* IsArray();
	AliasType* IsAlias();
	AliasType* IsAliasFor(Type* other);

	bool IsPointer() const { return tag == TypeTag::Pointer; }
	bool IsNumeric() const { return tag >= TypeTag::UInt8 && tag <= TypeTag::Bool; }
	bool IsFloatingPoint() const { return tag == TypeTag::Float32 || tag == TypeTag::Float64; }
	bool IsSigned() const { return tag >= TypeTag::Int8 && tag <= TypeTag::Bool; }
	bool IsString() const { return tag == TypeTag::String; }

	uint8_t GetSign() const { return IsSigned() ? true : false; } // for comparisons idk
	uint32_t GetBitWidth() const
	{
		switch (tag)
		{
		case TypeTag::Int8:
		case TypeTag::UInt8:
			return 8;
		case TypeTag::Int16:
		case TypeTag::UInt16:
			return 16;
		case TypeTag::Int32:
		case TypeTag::UInt32:
			return 32;
		case TypeTag::Int64:
		case TypeTag::UInt64:
			return 64;
		}
		ASSERT(false);
	}

	Type* GetContainedType() const;
	Type* GetPointerTo();
	ArrayType* GetArrayTypeOf(uint64_t count = 0);
public:
	static Type* Get(TypeTag tag, Type* contained = nullptr);
	static Type* Get(const std::string& name, Type* contained = nullptr);

	static std::string TagToString(TypeTag tag, Type* type = nullptr);
public:
	TypeTag tag = TypeTag::Unresolved;
	Type* contained = nullptr;
	llvm::Type* raw = nullptr;
public:
	static std::unordered_map<Key, Type*> RegisteredTypes;
};

class AliasType : public Type
{
public:
	using Type::Type;

	AliasType(Type* aliasFor, const std::string& name)
		: aliasedType(aliasFor), name(name)
	{
		tag = TypeTag::Alias;
	}

	std::string GetName() override { return name; }

	static AliasType* Get(Type* aliasFor, const std::string& name);
public:
	std::string name;
	Type* aliasedType = nullptr;

	static std::unordered_map<std::string, AliasType*> RegisteredTypes;
};

class StructType : public Type
{
public:
	using Type::Type;

	StructType(const std::string& name, const std::vector<Type*>& members)
		: name(name), members(members)
	{
		tag = TypeTag::Struct;
	}

	std::string GetName() override { return name; }

	static StructType* Get(const std::string& name, const std::vector<Type*>& members = {});
public:
	std::vector<Type*> members;
	struct StructDefinitionExpression* definition = nullptr;
public:
	std::string name;

	static std::unordered_map<std::string, StructType*> RegisteredTypes;
};

class ArrayType : public Type
{
public:
	using Key = std::pair<Type*, uint64_t>;
public:
	using Type::Type;
	
	ArrayType(Type* elementType, uint64_t count)
		: count(count)
	{
		tag = TypeTag::Array;
		contained = elementType;
	}

	virtual std::string GetName() { return "[" + std::to_string(count) + "]" + contained->GetName(); }

	static ArrayType* Get(Type* elementType, uint64_t count);
	
	// Used within a variable definition to keep track of the fact that it's an array, without needing the actual element type or count
	// used when inferring type idk
	// arr := [] { ... }
	static ArrayType* Dummy(uint64_t capacity = 0);
public:
	uint64_t count = 0;
	//bool dynamic = false;

	static std::unordered_map<Key, ArrayType*> RegisteredTypes;
};