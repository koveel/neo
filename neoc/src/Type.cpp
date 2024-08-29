#include "pch.h"

#include "Type.h"

std::unordered_map<std::string, Type> Type::RegisteredTypes;
 
const char* Type::TagToString(TypeTag tag)
{
	switch (tag)
	{
	case TypeTag::Void: return "void";
	case TypeTag::Int8: return "i8";
	case TypeTag::Int16: return "i16";
	case TypeTag::Int32: return "i32";
	case TypeTag::Int64: return "i64";
	case TypeTag::Float32: return "f32";
	case TypeTag::Float64: return "f64";
	case TypeTag::String: return "string";
	case TypeTag::Bool: return "bool";
	}

	return "";
}

static TypeTag TagFromString(const char* str)
{
	std::pair<const char*, TypeTag> pairs[] =
	{
		{ "void",   TypeTag::Void    },
		{ "i8",     TypeTag::Int8    },
		{ "i16",    TypeTag::Int16   },
		{ "i32",    TypeTag::Int32   },
		{ "i64",    TypeTag::Int64   },
		{ "f32",    TypeTag::Float32 },
		{ "f64",    TypeTag::Float64 },
		{ "string", TypeTag::String  },
		{ "bool",   TypeTag::Bool    },
	};

	for (const auto& pair : pairs)
	{
		if (strcmp(pair.first, str) == 0)
			return pair.second;
	}

	return TypeTag::Void;
}

Type* Type::Find(const std::string& name)
{
	if (RegisteredTypes.count(name))
		return &RegisteredTypes.at(name);

	return nullptr;
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