#pragma once

#include "Lexer.h"
#include "Type.h"

enum class NodeType
{
	Default = 1,
	Compound,
	PrimaryValue, StringValue,
	UnaryExpr, BinaryExpr,
	VariableDefine, VariableAccess,
	BranchExpr,
	FunctionDefinition, FunctionCall, Return,
	StructDefinition,
};

struct Expression;
class llvm::Value;

struct ASTNode
{
	uint32_t sourceLine = 0;
	NodeType nodeType = NodeType::Default;

	virtual llvm::Value* Generate() { return nullptr; }
};

// EXPRESSIONS!!!

struct Expression : public ASTNode
{
	Type* type = nullptr;

	Expression(uint32_t line)
	{
		sourceLine = line;
	}
};

struct PrimaryExpression : public Expression
{
	union
	{
		int64_t i64 = 0;
		int64_t* ip64;
		bool b32;
		double f64;
	} value;

	PrimaryExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::PrimaryValue;
	}

	llvm::Value* Generate() override;
};

struct StringExpression : public Expression
{
	// todo: turn into propah struct?
	struct
	{
		const char* start = nullptr;
		uint32_t length = 0;
	} value;

	StringExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::StringValue;
	}

	llvm::Value* Generate() override;
};

enum class UnaryType
{
	Not = 1,
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

	UnaryExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::UnaryExpr;
	}

	llvm::Value* Generate() override;
};

enum class BinaryType
{
	Add = 1, CompoundAdd,
	Subtract, CompoundSub,
	Multiply, CompoundMul,
	Divide, CompoundDiv,
	Assign,
	Equal, NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual,

	And, Or,
};

struct BinaryExpression : public Expression
{
	Token operatorToken;
	BinaryType binaryType = (BinaryType)0;
	std::unique_ptr<Expression> left = nullptr, right = nullptr;

	BinaryExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::BinaryExpr;
	}

	llvm::Value* Generate() override;
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

	BranchExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::BranchExpr;
	}

	llvm::Value* Generate() override;
};


// Block of statements?? tf is this shit
struct CompoundStatement : public Expression
{
	CompoundStatement(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::Compound;
	}
	
	std::vector<std::unique_ptr<ASTNode>> children;

	llvm::Value* Generate() override;
};

// Called a statement.. lowk an expression cause my api ass
struct VariableDefinitionStatement : public Expression
{
	std::unique_ptr<Expression> initializer = nullptr;

	struct
	{
		const char* start = nullptr;
		uint32_t length = 0;
	} Name;

	struct Modifiers
	{
		bool isGlobal = false, isConst = false;
	} modifiers;

	VariableDefinitionStatement(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::VariableDefine;
	}

	llvm::Value* Generate() override;
};

struct VariableAccessExpression : public Expression
{
	std::string name;
	bool loadValue = true; // Scuffed

	VariableAccessExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::VariableAccess;
	}

	llvm::Value* Generate() override;
};

struct FunctionPrototype
{
	std::string Name;
	Type* ReturnType = nullptr;
	std::vector<std::unique_ptr<VariableDefinitionStatement>> Parameters;
};

struct FunctionDefinitionExpression : public Expression
{
	FunctionPrototype prototype;
	std::vector<std::unique_ptr<ASTNode>> body;

	FunctionDefinitionExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::FunctionDefinition;
	}

	llvm::Value* Generate() override;
};

struct FunctionCallExpression : public Expression
{
	std::string name;
	std::vector<std::unique_ptr<Expression>> arguments;
	
	FunctionCallExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::FunctionCall;
	}

	llvm::Value* Generate() override;
};

struct ReturnStatement : public Expression
{
	std::unique_ptr<Expression> value;

	ReturnStatement(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::Return;
	}

	llvm::Value* Generate() override;
};

struct StructDefinitionExpression : public Expression
{
	std::string name;
	std::vector<std::unique_ptr<Expression>> members;

	StructDefinitionExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = NodeType::StructDefinition;
	}

	llvm::Value* Generate() override;
};