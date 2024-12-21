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

	COUNT,
};

class Type;
class StructType;
class ArrayType;

static inline void hash_combine(std::size_t& seed, uint64_t v)
{
	std::hash<uint64_t> hasher;
	seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

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

	bool operator==(const Type* other) const { return tag == other->tag && contained == other->contained; }
	bool operator!=(const Type* other) const { return !operator==(other); }

	virtual std::string GetName() { return TagToString(tag, this); }

	StructType* IsStruct();
	ArrayType* IsArray();
	bool IsPointer() const { return tag == TypeTag::Pointer; }
	bool IsNumeric() const { return tag >= TypeTag::UInt8 && tag <= TypeTag::Bool; }
	bool IsFloatingPoint() const { return tag == TypeTag::Float32 || tag == TypeTag::Float64; }
	bool IsSigned() const { return tag >= TypeTag::Int8 && tag <= TypeTag::Bool; }
	uint8_t GetSign() const { return IsSigned() ? true : false; } // for comparisons idk

	// De-pointerfy
	Type* GetContainedType() const;
	// Pointerfy
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

class StructType : public Type
{
public:
	using Type::Type;

	StructType(const std::string& name, const std::vector<Type*>& members)
		: name(name), members(members)
	{
		tag = TypeTag::Struct;
	}

	virtual std::string GetName() { return name; }

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
public:
	uint64_t count = 0;
	//bool dynamic = false;

	static std::unordered_map<Key, ArrayType*> RegisteredTypes;
};