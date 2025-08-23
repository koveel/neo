#pragma once

class Type;
struct Value;

struct LValue
{
	struct
	{
		llvm::Value* ptr = nullptr;
		// addr space etc...
	} address;
	Type* type = nullptr;
};

struct RValue
{
	llvm::Value* value = nullptr;
	Type* type = nullptr;
};

struct Value
{
	union // ub?
	{
		LValue lvalue{};
		RValue rvalue;
	};
	bool is_rvalue = false;
	Type* type = nullptr;

	//Value() = default;
	Value() {}
	Value(const LValue& lv)
		: lvalue(lv), type(lvalue.type)
	{}
	Value(const RValue& rv)
		: rvalue(rv), type(rvalue.type), is_rvalue(true)
	{}
};

// now implement conversions
//inline LValue::operator Value() const {
//	return { *this };
//}
//
//inline RValue::operator Value() const {
//	return { *this };
//}