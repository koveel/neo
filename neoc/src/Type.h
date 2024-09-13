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

class Type
{
public:
	Type() = default;
	Type(TypeTag tag, llvm::Type* raw = nullptr)
		: name(TagToString(tag)), tag(tag), raw(raw) {}

	bool operator==(const Type* other) const { return tag == other->tag; }
	bool operator!=(const Type* other) const { return tag != other->tag; }

	const std::string& GetName() const { return name; }

	bool IsStruct() const { return tag == TypeTag::Struct; }
	bool IsArray() const { return tag == TypeTag::Array; }
	bool IsPointer() const { return tag == TypeTag::Pointer; }

	// De-pointerfy
	Type* GetBaseType() const;
	// Pointerfy
	Type* GetPointerTo() const;
	Type* GetArrayTypeOf() const;
public:
	static Type* FindOrAdd(const std::string& name);
	static Type* FindOrAdd(TypeTag tag);
	static const char* TagToString(TypeTag tag);
public:
	std::string name;
	TypeTag tag = TypeTag::Unresolved;
	llvm::Type* raw = nullptr;

	// Not good!
	struct StructType
	{
		struct Member
		{
			std::string name;
			Type* type;
		};

		std::vector<Member> members;
	} Struct;
	struct ArrayType
	{
		uint64_t size = 0;
		bool dynamic = false;
	} Array;
public:
	static std::unordered_map<std::string, Type> RegisteredTypes;
};