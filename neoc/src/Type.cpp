#include "pch.h"

#include "Type.h"

#include <llvm/IR/Type.h>

std::unordered_map<Type::Key, Type*> Type::RegisteredTypes;
std::unordered_map<llvm::Type*, Type*> Type::LLVMToNeoTypes;
std::unordered_map<std::string, AliasType*> AliasType::RegisteredTypes;
std::unordered_map<std::string, StructType*> StructType::RegisteredTypes;
std::unordered_map<ArrayType::Key, ArrayType*> ArrayType::RegisteredTypes;
 
// TODO; WTF DONT HEAP ALLOCATE EVERY TYPE

static const std::pair<TypeTag, const char*> TagToStringMap[] =
{
	{ TypeTag::Void,    "void",   },
	{ TypeTag::UInt8,   "u8",     },
	{ TypeTag::UInt16,  "u16",    },
	{ TypeTag::UInt32,  "u32",    },
	{ TypeTag::UInt64,  "u64",    },
	{ TypeTag::Int8,    "i8",     },
	{ TypeTag::Int16,   "i16",    },
	{ TypeTag::Int32,   "i32",    },
	{ TypeTag::Int64,   "i64",    },
	{ TypeTag::Float32, "f32",    },
	{ TypeTag::Float64, "f64",    },
	{ TypeTag::Bool,    "bool",   },
};

std::string Type::TagToString(TypeTag tag, Type* type)
{
	if (tag == TypeTag::Pointer)
		return "*" + type->contained->GetName();
	if (ArrayType* arrayType = type->IsArray())
		return FormatString("[{}]{}", arrayType->count, arrayType->contained->GetName().c_str());

	for (const auto& pair : TagToStringMap)
	{
		if (pair.first == tag)
			return pair.second;
	}

	AliasType* alias = nullptr;
	if (tag == TypeTag::Struct && (alias = type->IsAlias()))
	{
		return alias->name;
	}

	ASSERT(false);
}

static Type* Add(const Type::Key& key)
{
	// TODO: free
	return Type::RegisteredTypes[key] = new Type(key);
}

static TypeTag TagFromString(const char* str)
{
	if (str[0] == '*')
		return TypeTag::Pointer;
	if (str[0] == '[' && str[1] == ']')
		return TypeTag::Array;

	for (const auto& pair : TagToStringMap)
	{
		if (strcmp(pair.second, str) == 0)
			return pair.first;
	}

	return TypeTag::Unresolved;
}

ArrayType* Type::IsArray()
{
	if (tag == TypeTag::Array)
		return static_cast<ArrayType*>(this);

	return nullptr;
}

Type* Type::Get(TypeTag tag, Type* contained)
{
	Key key = { tag, contained };
	if (!RegisteredTypes.count(key))
		return Add(key);

	return RegisteredTypes[key];
}
Type* Type::Get(const std::string& name, Type* contained)
{
	ASSERT(name.length());

	TypeTag tag = TagFromString(name.c_str());

	return tag != TypeTag::Unresolved ? Get(tag, contained) : StructType::Get(name);
}

Type* Type::FromLLVM(llvm::Type* raw)
{
	if (raw->isPointerTy())
	{
		ASSERT(false);
	}

	ASSERT(LLVMToNeoTypes.count(raw));

	return LLVMToNeoTypes[raw];
}

AliasType* AliasType::Get(Type* aliasFor, const std::string& name)
{
	ASSERT(name.length());

	if (RegisteredTypes.count(name))
		return RegisteredTypes[name];

	return RegisteredTypes[name] = new AliasType(aliasFor, name);
}

StructType* StructType::Get(const std::string& name, const std::vector<Type*>& members)
{
	if (RegisteredTypes.count(name))
		return RegisteredTypes[name];

	return RegisteredTypes[name] = new StructType(name, members);
}

ArrayType* ArrayType::Get(Type* elementType, uint64_t count)
{
	Key key = { elementType, count };
	if (RegisteredTypes.count(key))
		return RegisteredTypes[key];

	return RegisteredTypes[key] = new ArrayType(elementType, count);
}

ArrayType* ArrayType::Dummy(uint64_t capacity)
{
	return new ArrayType(nullptr, capacity);
}