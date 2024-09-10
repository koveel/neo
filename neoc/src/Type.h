#pragma once

enum class TypeTag
{
	Unresolved = 0,

	Void, Pointer,
	// Integral
	Int8, Int16, Int32, Int64,
	// Floatin' .
	Float32, Float64,
	Bool,
	// Other!!!
	String,
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
	bool IsPointer() const { return name.size() > 0 && name[0] == '*'; }

	// De-pointerfy
	Type* GetBaseType() const;
	// Pointerfy
	Type* GetPointerTo() const;
public:
	static Type* FindOrAdd(const std::string& name);
	static Type* FindOrAdd(TypeTag tag);
	static const char* TagToString(TypeTag tag);
public:
	std::string name;
	TypeTag tag = TypeTag::Unresolved;
	llvm::Type* raw = nullptr;

	struct StructType
	{
		struct Member
		{
			std::string name;
			Type* type;
		};

		std::vector<Member> members;
	} Struct;
public:
	static std::unordered_map<std::string, Type> RegisteredTypes;
};