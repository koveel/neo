#include "pch.h"

#include "Tree.h"
#include "Parser.h"

#include <unordered_map>

#include "PlatformUtils.h"

// TODO: remove type checking, naming conflict checking, etc
// do that during code gen not parsing

static Parser* parser = nullptr;

Parser* Parser::GetParser() { return parser; }

// Retard
struct ParseError : public std::exception
{
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

// Returns the binary type corresponding to the TokenType
// For example, TokenType::Plus corresponds to BinaryType::Add
static BinaryType GetBinaryType(TokenType type)
{
	switch (type)
	{
		case TokenType::PlusEqual:         return BinaryType::CompoundAdd;
		case TokenType::Plus:              return BinaryType::Add;
		case TokenType::DashEqual:         return BinaryType::CompoundSub;
		case TokenType::Dash:              return BinaryType::Subtract;
		case TokenType::StarEqual:         return BinaryType::CompoundMul;
		case TokenType::Star:              return BinaryType::Multiply;
		case TokenType::ForwardSlashEqual: return BinaryType::CompoundDiv;
		case TokenType::ForwardSlash:      return BinaryType::Divide;

		case TokenType::Equal:             return BinaryType::Assign;
		case TokenType::DoubleEqual:       return BinaryType::Equal;
		case TokenType::ExclamationEqual:  return BinaryType::NotEqual;
		case TokenType::Less:              return BinaryType::Less;
		case TokenType::LessEqual:         return BinaryType::LessEqual;
		case TokenType::Greater:           return BinaryType::Greater;
		case TokenType::GreaterEqual:      return BinaryType::GreaterEqual;

		case TokenType::DoubleAmpersand:   return BinaryType::And;
		case TokenType::DoublePipe:        return BinaryType::Or;

		case TokenType::MiniEllipsis:      return BinaryType::Range;
		case TokenType::Dot:               return BinaryType::MemberAccess;
		case TokenType::LeftSquareBracket: return BinaryType::Subscript;
	}

	return (BinaryType)0;
}

// Higher priorities will be lower in the AST (hopefully?)
static int GetBinaryPriority(BinaryType type)
{
	// TODO: confirm precedence is correct which it probably isnt cause im dumb
	switch (type)
	{
		case BinaryType::MemberAccess:
			return 31;
		case BinaryType::Subscript:
			return 29;
		case BinaryType::CompoundMul:
		case BinaryType::Multiply:
		case BinaryType::CompoundDiv:
		case BinaryType::Divide:
			return 28;
		case BinaryType::CompoundAdd:
		case BinaryType::Add:
		case BinaryType::CompoundSub:
		case BinaryType::Subtract:
			return 24;
		case BinaryType::Less:
		case BinaryType::LessEqual:
		case BinaryType::Greater:
		case BinaryType::GreaterEqual:
			return 20;
		case BinaryType::Equal:
		case BinaryType::NotEqual:
			return 19;
		case BinaryType::And:
		case BinaryType::Or:
		case BinaryType::Assign:
		case BinaryType::Range:
			return 18;
	}

	return 0;
}

template<typename T = Expression, typename = std::enable_if<std::is_base_of_v<Expression, T>>>
static std::unique_ptr<T> MakeExpression(Type* type = nullptr)
{
	auto expression = std::make_unique<T>(parser->lexer->line);
	expression->type = type;

	return expression;
}

static std::unique_ptr<Expression> ParseLoop();
static std::unique_ptr<Expression> ParseLoopControlFlow();
static std::unique_ptr<Expression> ParseUnaryExpression();
static std::unique_ptr<Expression> ParseReturnStatement();
static std::unique_ptr<Expression> ParseArrayInitializer(std::pair<Type*, std::unique_ptr<Expression>> typeAndFirstElement = { nullptr, nullptr }/*hate this*/);
static std::unique_ptr<Expression> ParseCompoundStatement();
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

		BinaryType type = GetBinaryType(token.type);
		int newPriority = GetBinaryPriority(type);

		bool done = type == BinaryType::MemberAccess ? newPriority <= priority : newPriority < priority; // tf
		if (newPriority == 0 || done)
			return left;

		Advance();  // Through operator
		
		auto binary = MakeExpression<BinaryExpression>(left->type);
		binary->binaryType = type;
		binary->operatorToken = token;
		binary->left = std::move(left);

		binary->right = ParseExpression(newPriority);

		if (type == BinaryType::Subscript)
			Expect(TokenType::RightSquareBracket, "expected ']' after expression");

		left = std::move(binary);
	}
}

static std::unique_ptr<Expression> ParseLine()
{
	bool expectSemicolon = true;
	auto expr = ParseExpression(-1);

	// Scuffed, dont come at me
	switch (expr->nodeType)
	{
	case NodeType::Loop:
	case NodeType::Branch:
	case NodeType::Compound:
	case NodeType::StructDefinition:
	case NodeType::FunctionDefinition:
		expectSemicolon = false;
		break;
	}

	if (expectSemicolon)
		Expect(TokenType::Semicolon, "expected ';' after expression");

	return expr;
}

static std::unique_ptr<Expression> ParseArrayDefinitionOrAccess();

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
	auto makeUnary = [token](UnaryType type) -> std::unique_ptr<Expression>
	{
		auto unary = MakeExpression<UnaryExpression>();
		unary->operatorToken = *token;
		unary->unaryType = type;

		Advance(); // To operand
		unary->operand = ParseExpression(-1);
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
			//case TokenType::LeftSquareBracket:
			//{
			//	return ParseArrayDefinitionOrAccess();
			//}
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
			primary->type = Type::FindOrAdd(TypeTag::Pointer);
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
			primary->type = Type::FindOrAdd(TypeTag::Bool);

			return primary;
		}
		case TokenType::False:
		{
			Advance();

			auto primary = MakeExpression<PrimaryExpression>();
			primary->value.b32 = false;
			primary->type = Type::FindOrAdd(TypeTag::Bool);

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
				primary->type = Type::FindOrAdd(TypeTag::Float32);
			}
			else
			{
				primary->value.i64 = (int64_t)strtol(token.start, nullptr, 0);
				primary->type = Type::FindOrAdd(TypeTag::Int32);
			}
			
			return primary;
		}
		case TokenType::String:
		{
			Advance();

			auto primary = MakeExpression<StringExpression>();
			primary->type = Type::FindOrAdd(TypeTag::String);

			primary->value.start = token.start;
			primary->value.length = token.length;

			return primary;
		}
		case TokenType::LeftSquareBracket:
			return ParseArrayInitializer();
		case TokenType::LeftCurlyBracket:
			return ParseCompoundStatement();
		case TokenType::Const: // TODO: const after id
		case TokenType::ID:
		{
			return ParseIdentifierExpression();
		}
	}

	Advance();
	LogError("unexpected symbol '%.*s'", token.length, token.start);

	return nullptr;
}

static std::unique_ptr<Expression> ParseReturnStatement()
{
	Token* current = &parser->current;

	Advance(); // Through return

	auto node = MakeExpression<ReturnStatement>();

	node->value = ParseExpression(-1);
	node->type = node->value->type;

	return node;
}

// Annoying cause we need
// f32[0.0, 0.0]
// and
// arr_name[0]
// to parse differently, so rn we just check how many expressions are in the square brackets
static std::unique_ptr<Expression> ParseArrayDefinitionOrAccess()
{
	Token* token = &parser->current;

	std::string typeOrVariableName = std::string(token->start, token->length);
	Token operatorToken = parser->lexer->nextToken;

	Advance(); // Through type / name
	Advance();
	auto indexOrFirstElement = ParseExpression(-1);

	if (token->type == TokenType::Comma)
	{
		// Array definition (> 1 element)

		return ParseArrayInitializer({Type::FindOrAdd(typeOrVariableName), std::move(indexOrFirstElement)});
	}

	//auto expr = MakeExpression<ArrayAccessExpression>();
	//expr->unaryType = UnaryType::ArrayAccess;
	//expr->operatorToken = operatorToken;
	//
	//expr->index = std::move(indexOrFirstElement);
	//
	//auto operand = MakeExpression<VariableAccessExpression>();
	//operand->name = typeOrVariableName;
	//expr->operand = std::move(operand);
	//
	//Expect(TokenType::RightSquareBracket, "expected ']' after array index expresson");

	return nullptr;
}

//static std::unique_ptr<Expression> ParseArrayInitializer(std::pair<Type*, std::unique_ptr<Expression>> typeAndFirstElement = { nullptr, nullptr }/*hate this*/)
static std::unique_ptr<Expression> ParseArrayInitializer(std::pair<Type*, std::unique_ptr<Expression>> typeAndFirstElement/*hate this*/)
{
	PROFILE_FUNCTION();

	auto array = MakeExpression<ArrayInitializationExpression>();

	Token* token = &parser->current;
	if (!typeAndFirstElement.first)
	{
		if (token->type == TokenType::ID)
		{
			// Gives us type
			std::string typeName = std::string(token->start, token->length);
			array->type = Type::FindOrAdd(typeName);

			Advance();
		}

		Advance(); // through [
	}
	else
	{
		// f32[0.0, 0.0]
		//       /|\
		//        | 

		Advance(); // through ,
		array->elements.push_back(std::move(typeAndFirstElement.second));
	}

	while (token->type != TokenType::RightSquareBracket && token->type != TokenType::Eof)
	{
		array->elements.push_back(ParseExpression(-1));

		if (token->type != TokenType::RightSquareBracket)
			Expect(TokenType::Comma, "expected ',' to separate array elements");
	}

	Expect(TokenType::RightSquareBracket, "expected ']' to close array elements");

	return array;
}

// expects array type   []x
static std::unique_ptr<Expression> ParseArrayDefinition(VariableDefinitionExpression* definition)
{
	PROFILE_FUNCTION();

	Token* token = &parser->current;
	Advance();

	auto array = MakeExpression<ArrayDefinitionExpression>();
	array->definition = definition;

	bool dynamic = false;
	if (token->type == TokenType::MiniEllipsis)
	{
		// Dynamic array
		dynamic = true;
		Expect(TokenType::RightSquareBracket, "expected ']' after '..'");
	}
	else
	{
		// todo: constant variables
		Token number = *token;
		Expect(TokenType::Number, "expected number for capacity after '['");

		if (strnchr(number.start, '.', number.length))
			LogError("array capacity must be integer");

		array->capacity = (uint64_t)strtoul(number.start, nullptr, 0);

		//array->capacityExpr = ParseExpression(-1);
		Expect(TokenType::RightSquareBracket, "expected ']' after expression");
	}

	Token typeToken = *token;
	Expect(TokenType::ID, "expected identifier after ']'");
	
	// Get type
	std::string typeName = std::string(typeToken.start, typeToken.length);
	Type* elementType = Type::FindOrAdd(typeName);
	array->type = elementType->GetArrayTypeOf();
	array->type->Array.size = array->capacity;

	// Initializer?
	if (token->type == TokenType::Equal)
	{
		Advance();
		array->initializer = ParseExpression(-1);
	}

	return array;
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
		} while (token->type == TokenType::Comma && token->type != TokenType::Colon && token->type != TokenType::WalrusTeeth && token->type != TokenType::Eof);
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
		Token typeToken = Advance();
		// TODO: modifiers

		// Handle pointer type
		uint32_t pointerDepth = 0;
		while (token->type == TokenType::Star)
		{
			Advance();
			pointerDepth++;
		}

		// Array!!!
		if (token->type == TokenType::LeftSquareBracket)
		{
			variable->initializer = ParseArrayDefinition(variable.get());
			variable->type = variable->initializer->type;
			return variable;
		}

		std::string typePrefix;
		typePrefix.insert(typePrefix.begin(), pointerDepth, '*');
		variable->type = Type::FindOrAdd(typePrefix + std::string(token->start, token->length));

		Advance(); // Through name
	}

	// Handle initializer if there is one
	if (parser->current.type == TokenType::Equal)
	{
		Advance(); // Through =
		variable->initializer = ParseExpression(-1);
	}

	return variable;
}

static std::unique_ptr<Expression> ParseFunctionDefinition(const std::string& functionName)
{
	Token* current = &parser->current;

	Advance(); // Through ::
	Advance(); // Through (

	auto function = MakeExpression<FunctionDefinitionExpression>();
	FunctionPrototype& prototype = function->prototype;
	prototype.Name = functionName;
	prototype.ReturnType = Type::FindOrAdd(TypeTag::Void);

	while (current->type != TokenType::RightParen && current->type != TokenType::Eof)
	{
		auto parameter = ParseVariableDefinitionStatement();

		// Cast to VariableDefinitionStatement
		auto tmp = dynamic_cast<VariableDefinitionExpression*>(parameter.get());
		std::unique_ptr<VariableDefinitionExpression> variable;
		parameter.release();
		variable.reset(tmp);

		prototype.Parameters.push_back(std::move(variable));

		if (current->type != TokenType::RightParen)
			Expect(TokenType::Comma, "expected ',' to separate function parameters");
	}

	Expect(TokenType::RightParen, "expected ')' to close parameter list");

	if (current->type == TokenType::RightArrow)
	{
		// Return type
		Advance();

		Token returnTypeToken = *current;
		Expect(TokenType::ID, "expected a return type after '->'");
		
		Type* returnType = Type::FindOrAdd(std::string(returnTypeToken.start, returnTypeToken.length));
		prototype.ReturnType = returnType;
	}

	if (current->type != TokenType::LeftCurlyBracket)
	{
		bool isPrototype = current->type == TokenType::Semicolon;

		if (!isPrototype)
		{
			LogError("expected '{' to start function body for '%s'", functionName.c_str());
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
		Expect(TokenType::RightCurlyBracket, "expected '}' to close function body");
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
			Expect(TokenType::Comma, "expected ',' to separate arguments for call to '%s'", functionName.c_str());
	}

	Expect(TokenType::RightParen, "expected ')' to close argument list");

	return call;
}

static std::unique_ptr<Expression> ParseStructDefinition(const std::string& structName)
{
	Token* token = &parser->current;

	Advance(); // Through ::
	Advance(); // Through struct
	
	auto structure = MakeExpression<StructDefinitionExpression>();
	auto structType = structure->type;

	structure->name = structName;

	structType = Type::FindOrAdd(structName);
	structType->tag = TypeTag::Struct;
	structType->Struct.definition = structure.get();

	Expect(TokenType::LeftCurlyBracket, "expected '{' after 'struct'");

	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		auto member = ParseVariableDefinitionStatement();

		// Cast to VariableDefinitionStatement
		auto tmp = dynamic_cast<VariableDefinitionExpression*>(member.get());
		std::unique_ptr<VariableDefinitionExpression> variable;
		member.release();
		variable.reset(tmp);

		// multi line type sh
		for (const auto& name : variable->succeedingDefinitionNames)
		{
			auto var = MakeExpression<VariableDefinitionExpression>();
			var->type = variable->type;
			var->initializer = variable->initializer;
			var->name = name;
			structure->members.push_back(std::move(var));
		}
		if (variable->succeedingDefinitionNames.size())
			structure->members.insert(structure->members.begin(), std::move(variable));
		else
			structure->members.push_back(std::move(variable));

		Expect(TokenType::Semicolon, "expected ';' after variable definition");
	}

	Expect(TokenType::RightCurlyBracket, "expected '}' to end struct definition");

	return structure;
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
	Expect(TokenType::LeftParen, "expected '(' after 'for'");

	Token iteratorToken = *current;
	Expect(TokenType::ID, "expected identifier after '('");

	std::string iteratorName = std::string(iteratorToken.start, iteratorToken.length);
	loop->iteratorVariableName = iteratorName;

	Expect(TokenType::Colon, "expected ':' after identifier");

	loop->range = ParseExpression(-1);

	Expect(TokenType::RightParen, "expected ')' after expression");

	if (current->type != TokenType::LeftCurlyBracket)
	{
		loop->body.push_back(ParseLine());
	}
	else
	{
		Advance();

		while (current->type != TokenType::RightCurlyBracket && current->type != TokenType::Eof)
			loop->body.push_back(ParseLine());

		Expect(TokenType::RightCurlyBracket, "expected '}' to close body for loop");
	}

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

	Advance();

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
	{
		Expect(TokenType::LeftParen, "expected '(' after %s", branchKeyword);
		branch.condition = ParseExpression(-1);
		Expect(TokenType::RightParen, "expected ')' after expression");
	}

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

		Expect(TokenType::RightCurlyBracket, "expected '}' to close body for %s", branchKeyword);
	}
	
	return branch;
}

static std::unique_ptr<Expression> ParseBranchStatement(TokenType branchType)
{
	PROFILE_FUNCTION();

	using Branch = BranchExpression::Branch;

	Token* current = &parser->current;
	auto branch = MakeExpression<BranchExpression>();

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

	return branch;
}

static std::unique_ptr<Expression> ParseCompoundStatement()
{
	PROFILE_FUNCTION();

	Expect(TokenType::LeftCurlyBracket, "expect '{' to begin compound statement");
		
	Token* token = &parser->current;
	auto compound = MakeExpression<CompoundStatement>();

	// Parse the statements in the block
	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		// Is this shit even necessary
		try
		{
			compound->children.push_back(ParseLine());
		}
		catch (ParseError&)
		{
			continue;
		}
	}

	Expect(TokenType::RightCurlyBracket, "expect '}' to end compound statement");

	return compound;
}

static void ParseModule(CompoundStatement* compound)
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

	if (current->type == TokenType::Semicolon)
		Advance(); // Through ;

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
		
	result.Module = MakeExpression<CompoundStatement>();

	try
	{
		Advance();
		ParseModule(result.Module.get());

		result.Succeeded = true;
	}
	catch (FatalError&)
	{
		fprintf(stderr, "fatal parse error - compilation stopped\n");
	}

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
		ParseResult* result = parser->result;

		// The world just exploded
		result->Succeeded = false;
		result->Module = nullptr;

		throw FatalError(); // Fuck this shit
	}
}