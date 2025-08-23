#pragma once

#include "Lexer.h"
#include "Type.h"
#include "codegen/Value.h"

class Generator;

enum class ExpressionType
{
	Default = 1,
	Compound,
	Primary, String,
	Unary, Binary, Cast,
	VariableDefinition, VariableAccess,
	Branch, Loop, LoopControlFlow, Range, ArrayInitialize,
	FunctionDefinition, FunctionCall, Return,
	StructDefinition,
	ConstantDefinition, EnumDefinition,
};

struct Expression
{
	Type* type = nullptr;	 
	ExpressionType exprType = ExpressionType::Default;

	uint32_t sourceLine = 0, sourceStart = 0;

	virtual Value Generate(Generator&) { return {}; }
	virtual void ResolveType() {}
};

// simplifies things
struct NullExpression : public Expression
{
	Value Generate(Generator&) override;

	static ExpressionType GetExprType() { return ExpressionType::ConstantDefinition; }
};

struct PrimaryExpression : public Expression
{
	union
	{
		int64_t i64 = 0;
		uint64_t u64;
		//int64_t* ip64;
		bool null;
		bool b32;
		double f64;
	} value;

	//using Expression::Expression;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::Primary; }
};

struct StringExpression : public Expression
{
	struct
	{
		const char* start = nullptr;
		uint32_t length = 0;
	} value;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::String; }
};

enum class UnaryType
{
	Not = 1, BitwiseNot,
	Negate,
	PrefixIncrement, PrefixDecrement,
	PostfixIncrement, PostfixDecrement,

	AddressOf, Deref,
};

struct UnaryExpression : public Expression
{
	Token operatorToken;
	UnaryType unaryType = (UnaryType)0;
	std::unique_ptr<Expression> operand = nullptr;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::Unary; }

	void ResolveType() override;
};

enum class BinaryType
{
	None = 0,

	Add, Subtract, Multiply, Divide,
	Modulo,

	Assign,
	Equal, NotEqual,
	Less, LessEqual,
	Greater, GreaterEqual,

	Range, // ..
	MemberAccess,
	Subscript,

	Xor,
	BitwiseOr,
	BitwiseAnd,
	LeftShift,
	RightShift,

	And, Or,
};

struct BinaryExpression : public Expression
{
	Token operatorToken;
	BinaryType binaryType = (BinaryType)0;
	bool isCompoundAssignment = false;
	std::unique_ptr<Expression> left = nullptr, right = nullptr;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::Binary; }

	void ResolveType() override;
};

struct CastExpression : public Expression
{
	Token token;
	std::unique_ptr<Expression> from;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::Cast; }

	void ResolveType() override;
};

struct BranchExpression : public Expression
{
	// If, else, else if
	struct Branch
	{
		std::unique_ptr<Expression> condition; // nullptr for 'else'
		std::vector<std::unique_ptr<Expression>> body;
	};
	std::vector<Branch> branches;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::Branch; }
};

enum class LoopControlFlowType
{
	Continue = 0,
	Break = 1
};

// Fck this
struct LoopControlFlowExpression : public Expression
{
	LoopControlFlowType controlType;

	Value Generate(Generator&);
	static ExpressionType GetExprType() { return ExpressionType::LoopControlFlow; }
};

struct LoopExpression : public Expression
{
	std::string iteratorVariableName;
	std::unique_ptr<Expression> range;
	std::vector<std::unique_ptr<Expression>> body;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::Loop; }

	void ResolveType() override;
};

// Block of statements?? tf is this shit
struct CompoundExpression : public Expression
{
	std::vector<std::unique_ptr<Expression>> children;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::Compound; }

	void ResolveType() override;
};

enum class DefinitionFlags : uint32_t
{
	Internal = 1 << 0,
};

struct DefinitionStatement : public Expression
{
	using Expression::Expression;

	std::string name;
	DefinitionFlags flags = (DefinitionFlags)0;

	std::unique_ptr<Expression> initializer = nullptr;
};

struct ConstantDefinitionExpression : public DefinitionStatement
{
	// only ids for now
	std::string lhs, rhs;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::ConstantDefinition; }

	void ResolveType() override;
};

struct VariableDefinitionExpression : public DefinitionStatement
{
	std::shared_ptr<Expression> initializer = nullptr;
	std::vector<std::string> succeedingDefinitionNames;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::VariableDefinition; }

	void ResolveType() override;
};

struct VariableAccessExpression : public Expression
{
	std::string name;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::VariableAccess; }

	void ResolveType() override;
};

struct FunctionPrototype
{
	std::string Name;
	Type* ReturnType = nullptr;
	std::vector<std::unique_ptr<VariableDefinitionExpression>> Parameters;
};

struct FunctionDefinitionExpression : public Expression
{
	FunctionPrototype prototype;
	std::vector<std::unique_ptr<Expression>> body;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::FunctionDefinition; }

	void ResolveType() override;
};

struct FunctionCallExpression : public Expression
{
	std::string name;
	std::vector<std::unique_ptr<Expression>> arguments;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::FunctionCall; }

	void ResolveType() override;
};

struct ReturnStatement : public Expression
{
	std::unique_ptr<Expression> value;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::Return; }

	void ResolveType() override;
};

struct EnumDefinitionExpression : public Expression
{
	std::string name;
	std::vector<std::unique_ptr<Expression>> members;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::EnumDefinition; }
};

struct StructDefinitionExpression : public Expression
{
	std::string name;
	std::vector<std::unique_ptr<VariableDefinitionExpression>> members;

	Value Generate(Generator&) override;
	static ExpressionType GetExprType() { return ExpressionType::StructDefinition; }
};