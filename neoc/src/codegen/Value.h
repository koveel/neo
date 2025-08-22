#pragma once

struct Type;
struct Value;

// only difference between l and rvalue structs is semantics for now
struct LValue
{
	struct
	{
		llvm::Value* ptr = nullptr;
		// addr space etc
	} address;
	Type* type = nullptr;

	LValue() = default;
	LValue(llvm::Value* ptr, Type* type)
		: address({ ptr }), type(type)
	{}

	//operator Value() const;
};

struct RValue
{
	llvm::Value* value = nullptr;
	Type* type = nullptr;

	RValue() = default;
	RValue(llvm::Value* value, Type* type)
		: value(value), type(type)
	{}

	//operator Value() const;
};

struct Value
{
	union // ub?
	{
		LValue lvalue;
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