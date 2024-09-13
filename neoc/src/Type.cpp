#include "pch.h"

#include "Type.h"

std::unordered_map<std::string, Type> Type::RegisteredTypes;
 
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
	{ TypeTag::String,  "string", },
	{ TypeTag::Bool,    "bool",   },
};

const char* Type::TagToString(TypeTag tag)
{
	for (const auto& pair : TagToStringMap)
	{
		if (pair.first == tag)
			return pair.second;
	}

	ASSERT(false);
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

Type* Type::FindOrAdd(const std::string& name)
{
	PROFILE_FUNCTION();

	if (!RegisteredTypes.count(name))
	{
		Type& t = RegisteredTypes[name];
		t.name = name;
		t.tag = TagFromString(name.c_str());
		return &t;
	}

	return &RegisteredTypes[name];
}

Type* Type::FindOrAdd(TypeTag tag)
{
	PROFILE_FUNCTION();

	auto it = std::find_if(std::begin(RegisteredTypes), std::end(RegisteredTypes),
		[tag](auto&& p) { return p.second.tag == tag; });

	// add that thang
	if (it == RegisteredTypes.end())
		return &(RegisteredTypes[TagToString(tag)] = Type(tag));

	return &it->second;
}