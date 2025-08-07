#pragma once

#include "Lexer.h"
#include "Type.h"

class llvm::Value;

enum class NodeType
{
	Default = 1,
	Compound,
	Primary, String,
	Unary, Binary, Cast,
	VariableDefinition, VariableAccess,
	Branch, Loop, LoopControlFlow, Range, ArrayInitialize,
	FunctionDefinition, FunctionCall, Return,
	StructDefinition, ArrayDefinition,
};

struct ASTNode
{
	uint32_t sourceLine = 0;
	NodeType nodeType = NodeType::Default;

	virtual llvm::Value* Generate() { return nullptr; }
	virtual void ResolveType() {}
};

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
		uint64_t u64;
		int64_t* ip64;
		bool b32;
		double f64;
	} value;

	PrimaryExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::Primary; }
};

struct StringExpression : public Expression
{
	struct
	{
		const char* start = nullptr;
		uint32_t length = 0;
	} value;

	StringExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::String; }
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

	UnaryExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::Unary; }

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
	//ConciseMemberAccess,
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

	BinaryExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::Binary; }

	void ResolveType() override;
};

struct CastExpression : public Expression
{
	Token token;
	std::unique_ptr<Expression> from;

	CastExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::Cast; }

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

	BranchExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::Branch; }
};

enum class LoopControlFlowType
{
	Break, Continue
};

// Fck this
struct LoopControlFlowExpression : public Expression
{
	LoopControlFlowType controlType;

	LoopControlFlowExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate();
	static NodeType GetNodeType() { return NodeType::LoopControlFlow; }
};

struct LoopExpression : public Expression
{
	std::string iteratorVariableName;
	std::unique_ptr<Expression> range;
	std::vector<std::unique_ptr<Expression>> body;

	LoopExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::Loop; }

	void ResolveType() override;
};

// Block of statements?? tf is this shit
struct CompoundExpression : public Expression
{
	std::vector<std::unique_ptr<Expression>> children;

	CompoundExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::Compound; }

	void ResolveType() override;
};

enum class DefinitionFlags : uint32_t
{
	Internal = 1 << 0,
};

struct VariableDefinitionExpression : public Expression
{
	struct Definition
	{
		std::string name;
		DefinitionFlags flags = (DefinitionFlags)0;
	} definition;

	std::shared_ptr<Expression> initializer = nullptr;
	std::vector<std::string> succeedingDefinitionNames;

	VariableDefinitionExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::VariableDefinition; }

	void ResolveType() override;
};

// Will be contained in a VariableDefinitionExpression if initializing an array, to provide capacity and elements
struct ArrayDefinitionExpression : public Expression
{
	VariableDefinitionExpression* variableDef = nullptr;

	uint64_t capacity = 0; // todo: make expr
	std::unique_ptr<Expression> initializer;

	ArrayDefinitionExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::ArrayDefinition; }

	void ResolveType() override;
};

struct VariableAccessExpression : public Expression
{
	std::string name;

	VariableAccessExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::VariableAccess; }

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

	FunctionDefinitionExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::FunctionDefinition; }

	void ResolveType() override;
};

struct FunctionCallExpression : public Expression
{
	std::string name;
	std::vector<std::unique_ptr<Expression>> arguments;
	
	FunctionCallExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::FunctionCall; }

	void ResolveType() override;
};

struct ReturnStatement : public Expression
{
	std::unique_ptr<Expression> value;

	ReturnStatement(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::Return; }

	void ResolveType() override;
};

struct StructDefinitionExpression : public Expression
{
	std::string name;
	std::vector<std::unique_ptr<VariableDefinitionExpression>> members;

	StructDefinitionExpression(uint32_t line)
		: Expression(line)
	{
		nodeType = GetNodeType();
	}

	llvm::Value* Generate() override;
	static NodeType GetNodeType() { return NodeType::StructDefinition; }
};