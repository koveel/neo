#include "pch.h"

#include "Tree.h"
#include "Parser.h"

static Parser* parser = nullptr;

Parser* Parser::GetParser() { return parser; }

// Retard
struct ParseError : public std::exception
{
	ParseError()
	{
		parser->result->Succeeded = false;
	}
};

// Full retard
struct FatalError : public std::exception
{
};

static Token Advance()
{
	return (parser->current = parser->lexer->Next());
}

// Advances and returns the 'old current' token
static Token Consume()
{
	Token token = parser->current;
	Advance();
	return token;
}

// Error if the type of the current token is not of 'type'
// If no error, Advance is called
template<typename... Args>
static void Expect(TokenType type, const char* errorMessageFmt, Args&&... args)
{
	if (!parser->lexer->Expect(type))
	{
		LogError(errorMessageFmt, std::forward<Args>(args)...);
		return;
	}

	Advance();
}

struct BinaryOperation
{
	BinaryType type = (BinaryType)0;
	bool compoundAssignment = false;
};

// Returns the binary type corresponding to the TokenType
// For example, TokenType::Plus corresponds to BinaryType::Add
static BinaryType GetBinaryType(TokenType type)
{
	switch (type)
	{
	// Arithmetic
	case TokenType::PlusEqual:
	case TokenType::Plus:               return BinaryType::Add;
	case TokenType::DashEqual:
	case TokenType::Dash:               return BinaryType::Subtract;
	case TokenType::StarEqual:
	case TokenType::Star:               return BinaryType::Multiply;
	case TokenType::ForwardSlashEqual:
	case TokenType::ForwardSlash:       return BinaryType::Divide;

	// Bitwise
	case TokenType::XorEqual:
	case TokenType::Xor:                return BinaryType::Xor;
	case TokenType::PipeEqual:
	case TokenType::Pipe:               return BinaryType::BitwiseOr;
	case TokenType::AmpersandEqual:
	case TokenType::Ampersand:          return BinaryType::BitwiseAnd;
	case TokenType::PercentEqual:
	case TokenType::Percent:            return BinaryType::Modulo;
	case TokenType::DoubleLessEqual:
	case TokenType::DoubleLess:         return BinaryType::LeftShift;
	case TokenType::DoubleGreaterEqual:
	case TokenType::DoubleGreater:      return BinaryType::RightShift;

	// Boolean
	case TokenType::Equal:              return BinaryType::Assign;
	case TokenType::DoubleEqual:        return BinaryType::Equal;
	case TokenType::ExclamationEqual:   return BinaryType::NotEqual;
	case TokenType::Less:               return BinaryType::Less;
	case TokenType::LessEqual:          return BinaryType::LessEqual;
	case TokenType::Greater:            return BinaryType::Greater;
	case TokenType::GreaterEqual:       return BinaryType::GreaterEqual;
	case TokenType::DoubleAmpersand:    return BinaryType::And;
	case TokenType::DoublePipe:         return BinaryType::Or;	

	case TokenType::MiniEllipsis:       return BinaryType::Range;
	case TokenType::Dot:                return BinaryType::MemberAccess;
	case TokenType::LeftSquareBracket:  return BinaryType::Subscript;
	}

	return (BinaryType)0;
}

static BinaryOperation GetBinaryOperationFromToken(TokenType token)
{
	BinaryOperation op;

	// Compound assignment?
	switch (token)
	{
	case TokenType::PlusEqual:
	case TokenType::DashEqual:
	case TokenType::StarEqual:
	case TokenType::ForwardSlashEqual:
	case TokenType::XorEqual:
	case TokenType::PipeEqual:
	case TokenType::AmpersandEqual:
	case TokenType::DoubleLessEqual:
	case TokenType::DoubleGreaterEqual:
	case TokenType::PercentEqual:
		op.compoundAssignment = true;
		break;
	}

	op.type = GetBinaryType(token);
	return op;
}

static int GetUnaryPriority(UnaryType unary)
{
	switch (unary)
	{
		case UnaryType::PostfixIncrement:
		case UnaryType::PostfixDecrement:
			return 100;
		case UnaryType::PrefixIncrement:
		case UnaryType::PrefixDecrement:
		case UnaryType::Negate:
		case UnaryType::Not:
		case UnaryType::BitwiseNot:
		case UnaryType::AddressOf:
		case UnaryType::Deref:
			return 99;
	}

	return 0;
}

static int GetBinaryPriority(BinaryOperation operation)
{
	if (operation.compoundAssignment)
		return 88;

	switch (operation.type)
	{
		case BinaryType::Subscript:
		case BinaryType::MemberAccess:
		case BinaryType::Range:
			return 100;
		case BinaryType::Divide:
		case BinaryType::Multiply:
		case BinaryType::Modulo:
			return 98;
		case BinaryType::Add:
		case BinaryType::Subtract:
			return 97;
		case BinaryType::LeftShift:
		case BinaryType::RightShift:
			return 96;
		case BinaryType::Less:
		case BinaryType::LessEqual:
		case BinaryType::Greater:
		case BinaryType::GreaterEqual:
			return 95;
		case BinaryType::Equal:
		case BinaryType::NotEqual:
			return 94;
		case BinaryType::BitwiseAnd:
			return 93;
		case BinaryType::Xor:
			return 92;
		case BinaryType::BitwiseOr:
			return 91;
		case BinaryType::And:
			return 90;
		case BinaryType::Or:
			return 89;
		case BinaryType::Assign:
			return 88;
	}

	return 0;
}

static uint32_t MadeExpressionsCount = 0; // for debug

template<typename T = Expression, typename = std::enable_if<std::is_base_of_v<Expression, T>>>
static std::unique_ptr<T> MakeExpression(Type* type = nullptr)
{
	MadeExpressionsCount++;

	auto expression = std::make_unique<T>(parser->lexer->line);
	expression->type = type;

	return expression;
}

static std::unique_ptr<Expression> ParseLoop();
static std::unique_ptr<Expression> ParseArray();
static std::unique_ptr<Expression> ParseCastExpression();
static std::unique_ptr<Expression> ParseLoopControlFlow();
static std::unique_ptr<Expression> ParseUnaryExpression();
static std::unique_ptr<Expression> ParseReturnStatement();
static std::unique_ptr<Expression> ParseCompoundStatement(TokenType statementDelimiter = TokenType::Semicolon);
static std::unique_ptr<Expression> ParseCompoundExpressionAsArray(Type* elementType = nullptr, uint64_t elementCount = 0);
static std::unique_ptr<Expression> ParsePrimaryExpression();
static std::unique_ptr<Expression> ParseIdentifierExpression();
static std::unique_ptr<Expression> ParseBranchStatement(TokenType branchType = TokenType::If);
static std::unique_ptr<Expression> ParseVariableDefinitionStatement();

static std::unique_ptr<Expression> ParseExpression(int priority) 
{
	PROFILE_FUNCTION();

	std::unique_ptr<Expression> left = ParseUnaryExpression();

	while (1)
	{
		Token token = parser->current;

		BinaryOperation operation = GetBinaryOperationFromToken(token.type);
		int newPriority = GetBinaryPriority(operation);

		bool done = operation.type == BinaryType::MemberAccess ? newPriority <= priority : newPriority < priority; // tf
		if (newPriority == 0 || done)
			return left;		
		
		Advance();  // Through operator

		auto binary = MakeExpression<BinaryExpression>(left->type);
		binary->binaryType = operation.type;
		binary->operatorToken = token;
		binary->isCompoundAssignment = operation.compoundAssignment;
		binary->left = std::move(left);

		binary->right = ParseExpression(newPriority);

		if (operation.type == BinaryType::Subscript)
			Expect(TokenType::RightSquareBracket, "expected ']' after expression");

		left = std::move(binary);
	}
}

static char CharFromTokenType(TokenType type)
{
	switch (type)
	{
	case TokenType::Comma: return ',';
	case TokenType::Semicolon: return ';';
	}
}

static std::unique_ptr<Expression> ParseLine(TokenType expectedEndline = TokenType::Semicolon)
{
	bool expectEndingToken = true;
	Token* token = &parser->lexer->currentToken;
	int priority = -1;

	if (token->type == TokenType::For || token->type == TokenType::If) {
		priority = 100;
	}

	auto expr = ParseExpression(priority);

	switch (expr->nodeType)
	{
	case NodeType::Loop:
	case NodeType::Branch:
	case NodeType::Compound:
	case NodeType::StructDefinition:
	case NodeType::FunctionDefinition:
	case NodeType::EnumDefinition:
		expectEndingToken = false;
		break;
	}

	std::string msg = FormatString("expected '{}' after expression", CharFromTokenType(expectedEndline));
	if (expectEndingToken)
		Expect(expectedEndline, msg.c_str());

	return expr;
}

static Type* ParseType()
{
	Token* token = &parser->current;

	// **i32
	// *[4]i32
	// f32
	// []f32

	while (token->type != TokenType::ID && token->type != TokenType::Eof)
	{
		Token prev = *token;
		if (token->type == TokenType::Star)
		{
			Advance();
			std::string containedTyName = std::string(token->start, token->length);
			return Type::Get(TypeTag::Pointer, ParseType());
		}
		if (token->type == TokenType::LeftSquareBracket)
		{
			Advance();
			uint64_t capacity = 0;

			prev = *token;
			if (token->type == TokenType::Number)
			{
				Advance();
				capacity = (uint64_t)strtoul(prev.start, nullptr, 0);
			}

			Expect(TokenType::RightSquareBracket, "expected ']' after '['");
			if (token->type == TokenType::Equal)
				return ArrayType::Dummy(capacity);

			return ArrayType::Get(ParseType(), capacity);
		}
	}

	std::string typeName = { token->start, token->length };
	Advance();

	return Type::Get(typeName);
}

static std::unique_ptr<Expression> ParseUnaryExpression()
{
	PROFILE_FUNCTION();

	Token* token = &parser->current;

	// Groupings
	if (token->type == TokenType::LeftParen)
	{
		Advance(); // Through (
		std::unique_ptr<Expression> expression = ParseExpression(-1);
		Advance(); // Through )

		return expression;
	}

	// For convenience
	auto makeUnary = [token](UnaryType type)
	{
		auto unary = MakeExpression<UnaryExpression>();
		unary->operatorToken = *token;
		unary->unaryType = type;

		Advance(); // To operand
		unary->operand = ParseExpression(GetUnaryPriority(type));
		unary->type = unary->operand->type;

		return unary;
	};

	switch (token->type)
	{
	case TokenType::Exclamation:
	{
		auto unary = makeUnary(UnaryType::Not);
		return unary;
	}
	case TokenType::Tilde:
	{
		auto unary = makeUnary(UnaryType::BitwiseNot);
		return unary;
	}
	case TokenType::Dash:
	{
		auto unary = makeUnary(UnaryType::Negate);
		return unary;
	}
	case TokenType::Increment:
	{
		auto unary = makeUnary(UnaryType::PrefixIncrement);
		return unary;
	}
	case TokenType::Decrement:
	{
		auto unary = makeUnary(UnaryType::PrefixDecrement);
		return unary;
	}
	case TokenType::At:
	{
		auto unary = makeUnary(UnaryType::AddressOf);
		return unary;
	}
	case TokenType::Star:
	{
		auto unary = makeUnary(UnaryType::Deref);
		return unary;
	}
	//case TokenType::Dot:
	//{
	//	// member access with no lhs
	//	auto binary = MakeExpression<BinaryExpression>();
	//	binary->binaryType = BinaryType::ConciseMemberAccess;
	//	binary->operatorToken = *token;
	//
	//	Advance();
	//
	//	std::string memberName = std::string(token->start, token->length);
	//	auto variable = MakeExpression<VariableAccessExpression>();
	//	variable->name = memberName;
	//
	//	Advance(); // through name
	//	binary->right = std::move(variable);
	//	return binary;
	//}
	case TokenType::ID:
	{
		// 	Handle any postfix unary operators (i++)
		Token next = parser->lexer->nextToken;

		UnaryType type = (UnaryType)0;
		switch (next.type)
		{
		case TokenType::Increment:
			type = UnaryType::PostfixIncrement;
			break;
		case TokenType::Decrement:
			type = UnaryType::PostfixDecrement;
			break;
		default:
			return ParsePrimaryExpression();
		}

		token = &next;

		auto unary = MakeExpression<UnaryExpression>();
		unary->operatorToken = *token;
		unary->unaryType = type;

		unary->operand = ParsePrimaryExpression();
		Advance();
		return unary;
	}
	}

	return ParsePrimaryExpression();
}

static Type* DetermineNumericTypeFromConstant(uint64_t v)
{
	constexpr auto i8 = std::numeric_limits<int8_t>::max();
	constexpr auto i16 = std::numeric_limits<int16_t>::max();
	constexpr auto i32 = std::numeric_limits<int32_t>::max();
	constexpr auto i64 = std::numeric_limits<int64_t>::max();
		
	return v <= i32 ? Type::Get(TypeTag::Int32) : Type::Get(TypeTag::Int64);
}

static std::unique_ptr<Expression> ParsePrimaryExpression()
{
	PROFILE_FUNCTION();
	
	Token token = parser->current;
	
	switch (token.type)
	{
		case TokenType::Null:
		{
			auto primary = MakeExpression<PrimaryExpression>();
			primary->value.ip64 = nullptr;
			primary->type = Type::Get(TypeTag::Pointer);
			return primary;
		}
		case TokenType::Return:
		{
			return ParseReturnStatement();
		}
		case TokenType::For:
		{
			return ParseLoop();
		}
		case TokenType::Break:
		case TokenType::Continue:
		{
			return ParseLoopControlFlow();
		}
		case TokenType::True:
		{
			Advance();

			auto primary = MakeExpression<PrimaryExpression>();
			primary->value.b32 = true;
			primary->type = Type::Get(TypeTag::Bool);

			return primary;
		}
		case TokenType::False:
		{
			Advance();

			auto primary = MakeExpression<PrimaryExpression>();
			primary->value.b32 = false;
			primary->type = Type::Get(TypeTag::Bool);

			return primary;
		}
		case TokenType::If:
		{
			return ParseBranchStatement();
		}
		case TokenType::Number:
		{
			Advance();

			auto primary = MakeExpression<PrimaryExpression>();

			// Integral or floating point?
			if (strnchr(token.start, '.', token.length))
			{
				primary->value.f64 = strtod(token.start, nullptr);
				primary->type = Type::Get(TypeTag::Float32);
			}
			else
			{
				primary->value.u64 = (uint64_t)strtoull(token.start, nullptr, 0);
				primary->type = DetermineNumericTypeFromConstant(primary->value.u64);
			}
			
			return primary;
		}
		case TokenType::String:
		{
			Advance();

			auto primary = MakeExpression<StringExpression>();
			primary->type = Type::Get(TypeTag::String);

			primary->value.start = token.start;
			primary->value.length = token.length;

			return primary;
		}
		case TokenType::LeftCurlyBracket:
			return ParseCompoundStatement(); // TODO: be able to recognize 'typed' compound []f32 = { ... }
		//case TokenType::Const: // TODO: const after id
		case TokenType::ID:
		{
			return ParseIdentifierExpression();
		}
		case TokenType::Cast:
		{
			return ParseCastExpression();
		}
		case TokenType::LeftSquareBracket:
		{
			return ParseArray();
		}
	}

	Advance();
	LogError("unexpected symbol '{}'", std::string_view(token.start, token.length));

	return nullptr;
}

static std::unique_ptr<Expression> ParseReturnStatement()
{
	Token* current = &parser->current;

	Advance(); // Through return

	auto node = MakeExpression<ReturnStatement>();
	node->type = Type::Get(TypeTag::Void);

	if (current->type != TokenType::Semicolon)
	{
		node->value = ParseExpression(-1);
		node->type = node->value->type;
	}

	return node;
}

static bool s_IsParsingCompoundAsBlock = false;

static std::unique_ptr<Expression> ParseCompoundExpressionAsStructAggregate(StructType* type)
{
	PROFILE_FUNCTION();

	Token* token = &parser->current;

	auto compound = MakeExpression<CompoundExpression>();
	compound->type = type;

	Expect(TokenType::LeftCurlyBracket, "expected '{{' to begin struct initializer");

	// Parse the expressions in the block
	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		if (s_IsParsingCompoundAsBlock) {
			compound->children.push_back(ParseLine());
			continue;
		}

		compound->children.push_back(ParseExpression(-1));
		if (token->type != TokenType::RightCurlyBracket) {
			Expect(TokenType::Comma, "expected ',' to separate values in initializer");
		}
		else {
			if (token->type == TokenType::Comma) // Allow trailing comma
				Advance();
		}
	}

	Expect(TokenType::RightCurlyBracket, "expected '}}' to end initializer");

	return compound;
}

static std::unique_ptr<Expression> ParseCompoundExpressionAsArray(Type* elementType, uint64_t elementCount)
{
	PROFILE_FUNCTION();

	Token* token = &parser->current;

	auto compound = MakeExpression<CompoundExpression>();
	Expect(TokenType::LeftCurlyBracket, "expected '{{' to begin array");

	// Parse the expressions in the block
	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		compound->children.push_back(ParseExpression(-1));

		if (token->type != TokenType::RightCurlyBracket) {
			Expect(TokenType::Comma, "expected ',' to separate values in array");
		}
		else {
			if (token->type == TokenType::Comma) // Allow trailing comma
				Advance();
		}
	}

	Expect(TokenType::RightCurlyBracket, "expected '}}' to end array");
	
	// Type
	if (!elementType) {
		const auto& values = compound->children;
		if (values.size() == 0)
			LogError("cannot infer array type without any elements");

		elementType = values[0]->type;
	}
	size_t numExpressions = compound->children.size();
	if (!elementCount) {
		elementCount = numExpressions;
	}
	else {
		if (elementCount != numExpressions)
			LogError("expected {} elements for array initializer - got {}", elementCount, numExpressions);
	}

	compound->type = ArrayType::Get(elementType, elementCount);

	return compound;
}

static std::unique_ptr<Expression> ParseArray()
{
	PROFILE_FUNCTION();

	Token* token = &parser->current;

	Advance(); // through [
	uint64_t elementCount = 0;

	if (token->type != TokenType::RightSquareBracket)
	{
		// Array count
		Advance();
	}
	Expect(TokenType::RightSquareBracket, "expected ']'");
	
	Type* elementType = nullptr;
	bool probablyGivingUsTheType = token->type != TokenType::LeftCurlyBracket && token->type != TokenType::Equal;
	if (probablyGivingUsTheType)
	{
		// Element type
		elementType = ParseType();
	}

	// Parse the expressions in the block
	return ParseCompoundExpressionAsArray(elementType, elementCount);
}

static std::unique_ptr<Expression> ParseVariableDefinitionStatement()
{
	PROFILE_FUNCTION();

	Token nameToken = parser->current;
	
	Advance(); // To : or :=
	
	// TODO: use string view type shit or smth
	std::string variableName = std::string(nameToken.start, nameToken.length);

	auto variable = MakeExpression<VariableDefinitionExpression>();
	variable->name = std::string(nameToken.start, nameToken.length);

	Token* token = &parser->current;
	if (token->type == TokenType::Comma)
	{
		// x, y: f32;
		// x, y := 0.0;
		// x := 0.0, y := 0.0;

		Advance();
		do
		{
			variable->succeedingDefinitionNames.emplace_back(token->start, token->length);
			Advance();

			if (token->type == TokenType::Comma)
				Advance();
		} while (token->type != TokenType::Colon && token->type != TokenType::WalrusTeeth && token->type != TokenType::Eof);
	}

	if (token->type == TokenType::WalrusTeeth) // Automatic type deduction
	{
		Advance(); // Through :=

		// Now we are at the expression to assign to the variable
		auto expression = ParseExpression(-1);
		variable->type = expression->type;
		variable->initializer = std::move(expression);

		return variable;
	}
	else // Mr. Programmer is kindly giving us the type
	{
		Advance(); // through :

		variable->type = ParseType();
	}

	// Handle initializer if there is one
	if (parser->current.type == TokenType::Equal)
	{
		Advance(); // Through =

		if (StructType* structType = variable->type->IsStruct()) {
			variable->initializer =
				token->type == TokenType::LeftCurlyBracket ?
				ParseCompoundExpressionAsStructAggregate(structType) :
				ParseExpression(-1);
			return variable;
		}

		if (ArrayType* arrayType = variable->type->IsArray()) {
			variable->initializer = ParseCompoundExpressionAsArray(arrayType->contained, arrayType->count);
			variable->type = variable->initializer->type;
			return variable;
		}

		variable->initializer = ParseExpression(-1);
	}

	return variable;
}

static std::unique_ptr<Expression> ParseFunctionDefinition(const std::string& functionName)
{
	PROFILE_FUNCTION();

	Token* current = &parser->current;

	Advance(); // Through ::
	Advance(); // Through (

	auto function = MakeExpression<FunctionDefinitionExpression>();
	FunctionPrototype& prototype = function->prototype;
	prototype.Name = functionName;
	prototype.ReturnType = Type::Get(TypeTag::Void);

	while (current->type != TokenType::RightParen && current->type != TokenType::Eof)
	{
		auto parameter = ParseVariableDefinitionStatement();

		// Cast to VariableDefinitionStatement
		auto temp = static_cast<VariableDefinitionExpression*>(parameter.get());
		std::unique_ptr<VariableDefinitionExpression> variable;
		parameter.release();
		variable.reset(temp);

		// multi line type sh
		for (const auto& name : variable->succeedingDefinitionNames)
		{
			auto param = MakeExpression<VariableDefinitionExpression>();
			param->type = variable->type;
			param->name = name;
			//param->initializer = variable->initializer;

			prototype.Parameters.push_back(std::move(param));
		}
		if (variable->succeedingDefinitionNames.size())
			prototype.Parameters.insert(prototype.Parameters.begin(), std::move(variable));
		else
			prototype.Parameters.push_back(std::move(variable));

		if (current->type != TokenType::RightParen)
			Expect(TokenType::Comma, "expected ',' to separate function parameters");
	}

	Expect(TokenType::RightParen, "expected ')' to close parameter list");

	if (current->type == TokenType::RightArrow)
	{
		// Return type
		Advance();

		prototype.ReturnType = ParseType();
	}

	if (current->type != TokenType::LeftCurlyBracket)
	{
		bool isPrototype = current->type == TokenType::Semicolon;

		if (!isPrototype)
		{
			LogError("expected '{{' to start function body for '{}'", functionName.c_str());
			return nullptr;
		}
		Advance();
	}
	else
	{
		Advance();

		// Parse body
		while (current->type != TokenType::RightCurlyBracket && current->type != TokenType::Eof)
		{
			try
			{
				function->body.push_back(ParseLine());
			}
			catch (ParseError&)
			{
				continue;
			}
		}
		Expect(TokenType::RightCurlyBracket, "expected '}}' to close function body");
	}

	return function;
}

static std::unique_ptr<Expression> ParseFunctionCall()
{
	Token* current = &parser->current;

	std::string functionName = std::string(current->start, current->length);

	Advance(); // Through function name
	Advance(); // Through (

	auto call = MakeExpression<FunctionCallExpression>();
	call->name = functionName;

	while (current->type != TokenType::RightParen && current->type != TokenType::Eof)
	{
		auto arg = ParseExpression(-1);
		call->arguments.push_back(std::move(arg));

		if (current->type != TokenType::RightParen)
			Expect(TokenType::Comma, "expected ',' to separate arguments for call to '{}'", functionName.c_str());
	}

	Expect(TokenType::RightParen, "expected ')' to close argument list");

	return call;
}

static std::unique_ptr<Expression> ParseEnumDefinition(const std::string& enumName)
{
	Token* token = &parser->current;

	Advance(); // Through ::
	Advance(); // Through enum

	auto enume = MakeExpression<EnumDefinitionExpression>();
	enume->name = enumName;

	Type* integerType = Type::Get(TypeTag::Int32);
	if (token->type != TokenType::LeftCurlyBracket)
		integerType = ParseType();

	enume->type = AliasType::Get(integerType, enumName);

	Expect(TokenType::LeftCurlyBracket, "expected '{{' to begin enum");

	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		enume->members.push_back(ParseExpression(-1));
		Expect(TokenType::Comma, "expected ',' to seperate enum members");
	}

	Expect(TokenType::RightCurlyBracket, "expected '}}' to end enum");

	return enume;
}

static std::unique_ptr<Expression> ParseStructDefinition(const std::string& structName)
{
	Token* token = &parser->current;

	Advance(); // Through ::
	Advance(); // Through struct
	
	auto structure = MakeExpression<StructDefinitionExpression>();
	structure->name = structName;

	StructType* type = StructType::Get(structName);
	structure->type = type;
	type->definition = structure.get();

	Expect(TokenType::LeftCurlyBracket, "expected '{{' after 'struct'");

	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		auto member = ParseVariableDefinitionStatement();

		// Cast to VariableDefinitionStatement
		auto tmp = static_cast<VariableDefinitionExpression*>(member.get());
		std::unique_ptr<VariableDefinitionExpression> variable;
		member.release();
		variable.reset(tmp);

		// TODO: use 1 vector for both

		// multi line type sh
		for (const auto& name : variable->succeedingDefinitionNames)
		{
			auto var = MakeExpression<VariableDefinitionExpression>();
			var->type = variable->type;
			var->initializer = variable->initializer;
			var->name = name;

			type->members.push_back(var->type);
			structure->members.push_back(std::move(var));
		}
		if (variable->succeedingDefinitionNames.size())
		{
			type->members.insert(type->members.begin(), variable->type);
			structure->members.insert(structure->members.begin(), std::move(variable));
		}
		else
		{
			type->members.push_back(variable->type);
			structure->members.push_back(std::move(variable));
		}

		Expect(TokenType::Semicolon, "expected ';' after variable definition");
	}

	Expect(TokenType::RightCurlyBracket, "expected '}}' to end struct definition");

	return structure;
}

static std::unique_ptr<Expression> ParseCastExpression()
{
	Advance(); // Through 'cast'

	auto cast = MakeExpression<CastExpression>();
	Expect(TokenType::LeftParen, "expected '(' after 'cast'");

	cast->type = ParseType();

	Expect(TokenType::RightParen, "expected ')' after type");

	cast->from = ParseExpression(29);
	return cast;
}

// break, continue
static std::unique_ptr<Expression> ParseLoopControlFlow()
{
	Token token = parser->current;

	auto expr = MakeExpression<LoopControlFlowExpression>();

	if (token.type == TokenType::Break)
		expr->controlType = LoopControlFlowType::Break;
	else if (token.type == TokenType::Continue)
		expr->controlType = LoopControlFlowType::Continue;

	return expr;
}

static std::unique_ptr<Expression> ParseLoop()
{
	Token* current = &parser->current;

	Advance(); // through for

	auto loop = MakeExpression<LoopExpression>();
	//Expect(TokenType::LeftParen, "expected '(' after 'for'");

	Token iteratorToken = *current;
	Expect(TokenType::ID, "expected identifier after '('");

	std::string iteratorName = std::string(iteratorToken.start, iteratorToken.length);
	loop->iteratorVariableName = iteratorName;

	Expect(TokenType::Colon, "expected ':' after identifier");

	s_IsParsingCompoundAsBlock = true;
	loop->range = ParseExpression(-1);

	//Expect(TokenType::RightParen, "expected ')' after expression");

	if (current->type != TokenType::LeftCurlyBracket)
	{
		loop->body.push_back(ParseLine());
	}
	else
	{
		Advance();

		while (current->type != TokenType::RightCurlyBracket && current->type != TokenType::Eof)
			loop->body.push_back(ParseLine());

		Expect(TokenType::RightCurlyBracket, "expected '}}' to close body for loop");
	}
	s_IsParsingCompoundAsBlock = false;

	return loop;
}

static std::unique_ptr<Expression> ParseIdentifierExpression()
{
	Token* token = &parser->current;
	std::string identifier = std::string(token->start, token->length);

	Token* next = &parser->lexer->nextToken;
	switch (next->type)
	{
		case TokenType::DoubleColon:
		{
			Advance(); // To ::

			// Type / function definition
			switch (next->type)
			{
				case TokenType::LeftParen:
				{
					return ParseFunctionDefinition(identifier);
				}
				case TokenType::Struct:
				{
					return ParseStructDefinition(identifier);
				}
				case TokenType::Enum:
				{
					return ParseEnumDefinition(identifier);
				}
				case TokenType::ID:
				{
					// Alias or constant
					Advance();
					std::string aliased = std::string(token->start, token->length);
					Advance();

					AliasType::Get(Type::Get(aliased), identifier);
					return MakeExpression<ConstantDefinitionExpression>();
				}
			}

			LogError("invalid definition - expected a function");
			return nullptr;
		}
		case TokenType::LeftParen:
		{
			return ParseFunctionCall();
		}
		case TokenType::Colon:
		case TokenType::WalrusTeeth:
		{
			return ParseVariableDefinitionStatement();
		}
		default:
		{
			break;
		}
	}

	bool probablyStructAggregateInit = next->type == TokenType::LeftCurlyBracket;

	Token prev = *token;
	Advance();

	if (probablyStructAggregateInit && !s_IsParsingCompoundAsBlock) {
		Type* type = Type::Get(identifier);
		return ParseCompoundExpressionAsStructAggregate(type->IsStruct());
	}

	auto read = MakeExpression<VariableAccessExpression>();
	read->name = identifier;

	return read;
}

static BranchExpression::Branch ParseBranch(uint32_t branchType)
{
	BranchExpression::Branch branch;
	branch.body.reserve(1);

	if (branchType == 0 || branchType == 1) {
		Advance();
	}
	else {
		Advance();
		Advance();
	}

	const char* branchKeywords[]
	{
		"if", "else", "else if"
	};
	const char* branchKeyword = branchKeywords[branchType];

	// 'else' has no condition
	if (branchType != 1)
		branch.condition = ParseExpression(-1);

	Token* current = &parser->current;
	if (current->type != TokenType::LeftCurlyBracket)
	{
		branch.body.push_back(ParseLine());
	}
	else
	{
		Advance();

		while (current->type != TokenType::RightCurlyBracket && current->type != TokenType::Eof)
		{
			auto expr = ParseLine();
			branch.body.push_back(std::move(expr));
		}

		Expect(TokenType::RightCurlyBracket, "expected '}}' to close body for {}", branchKeyword);
	}
	
	return branch;
}

static std::unique_ptr<Expression> ParseBranchStatement(TokenType branchType)
{
	PROFILE_FUNCTION();

	using Branch = BranchExpression::Branch;

	Token* current = &parser->current;
	auto branch = MakeExpression<BranchExpression>();

	s_IsParsingCompoundAsBlock = true;
	branch->branches.push_back(ParseBranch(0));
	bool hasElse = false;

	while (current->type == TokenType::Else)
	{
		bool isElse = parser->lexer->nextToken.type != TokenType::If;
		if (isElse)
		{
			hasElse = true;
			branch->branches.push_back(ParseBranch(1));
			continue;
		}

		if (hasElse)
		{
			LogError("'else if' must precede 'else'");
			return nullptr;
		}

		branch->branches.push_back(ParseBranch(2));
	}
	s_IsParsingCompoundAsBlock = false;

	return branch;
}

static std::unique_ptr<Expression> ParseCompoundStatement(TokenType statementDelimiter)
{
	PROFILE_FUNCTION();

	Expect(TokenType::LeftCurlyBracket, "expect '{{' to begin compound statement");
		
	Token* token = &parser->current;
	auto compound = MakeExpression<CompoundExpression>();

	// Parse the statements in the block
	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		// Is this shit even necessary
		try
		{
			compound->children.push_back(ParseLine(statementDelimiter));
		}
		catch (ParseError&)
		{
			continue;
		}
	}

	Expect(TokenType::RightCurlyBracket, "expect '}}' to end compound statement");

	return compound;
}

static void ParseModule(CompoundExpression* compound)
{
	PROFILE_FUNCTION();

	Token* token = &parser->current;
	while (token->type != TokenType::Eof)
	{
		try
		{
			compound->children.push_back(ParseLine());
		}
		catch (ParseError&)
		{
			continue;
		}
	}
}

static bool AttemptSynchronization()
{
	auto isAtSyncPoint = [](Token* token)
	{
		return token->type == TokenType::Semicolon || token->type == TokenType::RightCurlyBracket;
	};

	// Advance until at sync point
	Token* current = &parser->current;
	while (!isAtSyncPoint(current) && current->type != TokenType::Eof)
	{
		// Panic! at the disco
		Advance();
	}

	if (current->type == TokenType::Eof)
		return false;

	// TODO; Don't consume curly if its a definition?

	Advance(); // Through ; or }

	return true;
}

Parser::Parser()
{
	parser = this;
}

ParseResult Parser::Parse(Lexer* lexer)
{
	PROFILE_FUNCTION();
	 
	ParseResult result{};

	parser = this;
	parser->lexer = lexer;
	parser->result = &result;
		
	result.module.SyntaxTree = MakeExpression<CompoundExpression>();

	try
	{
		Advance();
		ParseModule(result.module.SyntaxTree.get());
	}
	catch (FatalError&)
	{
		std::cout << "fatal parse error - compilation stopped\n";
	}

	std::cout << "generated " << MadeExpressionsCount << " expressions\n";

	return result;
}

void Parser::Panic()
{
	bool syncSucceeded = AttemptSynchronization();

	if (syncSucceeded)
	{
		throw ParseError();
	}
	else
	{
		// The world just exploded
		ParseResult* result = parser->result;
		result->module.SyntaxTree = nullptr;

		throw FatalError(); // Fuck this shit
	}
}