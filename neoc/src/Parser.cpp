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
	}

	return (BinaryType)0;
}

// Higher priorities will be lower in the AST (hopefully?)
static int GetBinaryPriority(BinaryType type)
{
	// TODO: confirm precedence is correct which it probably isnt cause im dumb ash
	switch (type)
	{
		case BinaryType::CompoundMul:
		case BinaryType::Multiply:
		case BinaryType::CompoundDiv:
		case BinaryType::Divide:
			return 30;
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

template<typename T, typename = std::enable_if<std::is_base_of_v<Statement, T>>>
static std::unique_ptr<T> MakeStatement()
{
	auto statement = std::make_unique<T>(parser->lexer->line);
	return statement;
}

static std::unique_ptr<Expression> ParseStatement();
static std::unique_ptr<Expression> ParseUnaryExpression();
static std::unique_ptr<Expression> ParseReturnStatement();
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

		if (newPriority == 0 || newPriority <= priority) 
			return left;

		Advance();  // Through operator

		auto binary = MakeExpression<BinaryExpression>(left->type);
		binary->binaryType = type;
		binary->operatorToken = token;
		binary->left = std::move(left);

		binary->right = ParseExpression(newPriority);

		left = std::move(binary);
	}
}

 // Lowk down syndrome mode
static std::unique_ptr<Expression> ParseLine()
{
	bool expectSemicolon = true;
	auto expr = ParseExpression(-1);

	// Scuffed, dont come at me
	switch (expr->nodeType)
	{
	case NodeType::Compound:
	case NodeType::BranchExpr:
	case NodeType::StructDefinition:
	case NodeType::FunctionDefinition:
		expectSemicolon = false;
		break;
	}

	if (expectSemicolon)
		Expect(TokenType::Semicolon, "expected ';' after expression");

	return expr;
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
	auto makeUnary = [token](UnaryType type) -> std::unique_ptr<Expression>
	{
		auto unary = MakeExpression<UnaryExpression>();
		unary->operatorToken = *token;
		unary->unaryType = type;

		Advance(); // To operand
		unary->operand = ParsePrimaryExpression();

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
	}

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
		return ParsePrimaryExpression(); // Parse a primary expression if we shouldn't parse a unary one
	}

	token = &next;
	
	auto unary = MakeExpression<UnaryExpression>();
	unary->operatorToken = *token;
	unary->unaryType = type;

	unary->operand = ParsePrimaryExpression();

	Advance();

	return unary;
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
			return primary;
		}
		case TokenType::Return:
		{
			return ParseReturnStatement();
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
				primary->type = Type::FindOrAdd(TypeTag::Float64); // Any floating point value is double by default???
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
		case TokenType::LeftCurlyBracket:
			return ParseCompoundStatement();
		case TokenType::Const: // TODO: const after id
		case TokenType::ID:
		{
			return ParseIdentifierExpression();
		}

		// Do everything
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

static std::unique_ptr<Expression> ParseVariableDefinitionStatement()
{
	PROFILE_FUNCTION();

	Token nameToken = parser->current;
	
	Advance(); // To : or :=
	
	//variable->scope = p arser->scopeDepth;
	//variable->modifiers.isGlobal = parser->scopeDepth == 0;
	
	// TODO: use string view type shit
	std::string variableName = std::string(nameToken.start, nameToken.length);

	auto variable = MakeStatement<VariableDefinitionStatement>();

	variable->Name.start = nameToken.start;
	variable->Name.length = nameToken.length;

	Token* token = &parser->current;
	if (token->type == TokenType::WalrusTeeth) // Automatic type deduction
	{
		Advance(); // Through :=

		// Now we are at the expression to assign to the variable
		auto expression = ParseExpression(-1);
		variable->type = expression->type;
		variable->initializer = std::move(expression);
	}
	else // Mr. Programmer is kindly giving us the type
	{
		Token typeToken = Advance();

		if (typeToken.type == TokenType::Const)
		{
			variable->modifiers.isConst = true;
			typeToken = Advance(); // Through 'const'
		}

		// Handle pointer type
		uint32_t pointerDepth = 0;
		while (token->type == TokenType::Star)
		{
			Advance();
			pointerDepth++;
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

		// Parse initializer
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
		auto tmp = dynamic_cast<VariableDefinitionStatement*>(parameter.get());
		std::unique_ptr<VariableDefinitionStatement> variable;
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
		function->body = ParseCompoundStatement();
	}


	// Success!

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
	structure->name = structName;

	Expect(TokenType::LeftCurlyBracket, "expected '{' after 'struct'");

	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		auto member = ParseVariableDefinitionStatement();
		structure->members.push_back(std::move(member));

		Expect(TokenType::Semicolon, "expected ';' after variable definition");
	}

	Expect(TokenType::RightCurlyBracket, "expected '}' to end struct definition");

	return structure;
}

static std::unique_ptr<Expression> ParseStructMemberAccessExpression()
{
	Token* token = &parser->current;

	auto node = MakeExpression<VariableAccessExpression>();
	std::string identifierName = std::string(token->start, token->length);

	Advance(); // To .
	Advance(); // Through .

	Token member = *token;
	Expect(TokenType::ID, "expected identifier after '%s.'", identifierName.c_str());

	std::string memberName = std::string(member.start, member.length);
	node->name = identifierName + "." + memberName;

	return node;
}

static std::unique_ptr<Expression> ParseIdentifierExpression()
{
	Token* token = &parser->current;
	std::string identifier = std::string(token->start, token->length);

	//ScopedValue value;
	//Scope* scope = parser->currentScope;
	//if (!scope->HasValue(identifier, &value))
	//{
	//	LogError("identifier \"%s\" not defined in scope", identifier.c_str());
	//	return nullptr;
	//}

	Token* next = &parser->lexer->nextToken;
	switch (next->type)
	{
		//case TokenType::Increment:
		//case TokenType::Decrement:
		//{
		//	return ParseUnaryExpression(); // var++/--
		//}
		case TokenType::Dot:
		{
			return ParseStructMemberAccessExpression();
		}
		case TokenType::DoubleColon:
		{
			Advance(); // To ::

			// Type / function definition
			Token next = parser->lexer->nextToken;
			switch (next.type)
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

static std::unique_ptr<Expression> ParseStatement()
{
	PROFILE_FUNCTION();

	Token* token = &parser->current;

	switch (token->type)
	{
		case TokenType::LeftCurlyBracket:
			return ParseCompoundStatement();
		case TokenType::If:
			return ParseBranchStatement();
		case TokenType::Import:
			//return ParseImportStatement();
		case TokenType::Const: // TODO: const after id
		case TokenType::ID:
		{
			Token* next = &parser->lexer->nextToken;
			switch (next->type)
			{
			case TokenType::Increment:
			case TokenType::Decrement:
				return ParseUnaryExpression();
			case TokenType::WalrusTeeth:
			case TokenType::Colon:
			{
				return ParseVariableDefinitionStatement();
			}
			case TokenType::DoubleColon:
			{
				Advance();

				switch (next->type)
				{
				case TokenType::LeftParen:
					//return ParseFunctionDefinition(&identifier);
				case TokenType::Struct:
					//return ParseStructureDefinition(&identifier);
				default:
					LogError("invalid declaration");
					return nullptr;
				}
			}
			case TokenType::LeftParen:
				//return ParseFunctionCall();
			case TokenType::Dot:
				//return ParseMemberAccessStatement();
			default:
				return ParseExpression(-1);
			}

			LogError("invalid identifier expression", next->length, next->start);
			return nullptr;
		}
	}

	//return ParseExpressionStatement();
}

static std::unique_ptr<Expression> ParseBranchStatement(TokenType branchType)
{
	PROFILE_FUNCTION();

	Token* current = &parser->current;
	auto branch = MakeExpression<BranchExpression>();

	Advance(); // Through if/else
	bool expectCondition = true;
	const char* branchKeyword = branchType == TokenType::If ? "if" : "else if"; // For error message

	if (branchType == TokenType::Else)
	{
		// So else if doesn't explode everything
		if (expectCondition = current->type == TokenType::If)
			Advance(); // To (
	}

	if (expectCondition)
	{
		Expect(TokenType::LeftParen, "expected '(' after '%s'", branchKeyword);
		branch->condition = ParseExpression(-1);
		Expect(TokenType::RightParen, "expected ')' after expression");
	}
	
	if (current->type == TokenType::LeftCurlyBracket)
	{
		branch->body = ParseCompoundStatement();
	}
	else
	{
		// No brackets
		branch->body = ParseExpression(-1);
		Expect(TokenType::Semicolon, "expected ';' to end line");
	}

	// Has an else branch
	if (current->type == TokenType::Else)
	{
		branch->elseBranch = ParseBranchStatement(TokenType::Else);
	}

	return branch;
}

static std::unique_ptr<Expression> ParseCompoundStatement()
{
	PROFILE_FUNCTION();

	Expect(TokenType::LeftCurlyBracket, "expect '{' to begin compound statement");

	//parser->EnterState(ParseState::Definition);
		
	Token* token = &parser->current;
	auto compound = MakeStatement<CompoundStatement>();

	// Parse the statements in the block
	while (token->type != TokenType::RightCurlyBracket && token->type != TokenType::Eof)
	{
		// Is this shit even necessary
		try
		{
			// auto statement = ParseExpression(-1);
			auto statement = ParseLine();

			//if (statement->nodeType != NodeType::Compound)
			//	Expect(TokenType::Semicolon, "expected ';' to end line");

			compound->children.push_back(std::move(statement));
		}
		catch (ParseError&)
		{
			continue;
		}
	}

	//parser->ExitState();

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
			auto statement = ParseLine();

			// Ugly ik
			//if (statement->nodeType != NodeType::Compound) // Anything ending with right curly bracket }
			//	Expect(TokenType::Semicolon, "expected ';' to end line");

			compound->children.push_back(std::move(statement));
		}
		catch (ParseError&)
		{
			continue;
		}
	}
}

static bool AttemptSynchronization()
{
	// TODO: handle scope change?

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

static void BeginParsingProcedure(ParseResult* result, bool totalSuccess = true)
{
	Advance();
	ParseModule(result->Module.get());

	result->Succeeded = totalSuccess;
}

Parser::Parser()
{
	parser = this;

	// Register all basic types
	for (int i = 0; i < (int)TypeTag::COUNT; i++)
	{
		TypeTag tag = (TypeTag)i;
		Type type { tag };

		Type::Register(Type::TagToString(tag), type);
	}
}

ParseResult Parser::Parse(Lexer* lexer)
{
	PROFILE_FUNCTION();
	 
	ParseResult result{};

	parser = this;
	parser->lexer = lexer;
	parser->result = &result;
		
	result.Module = MakeStatement<CompoundStatement>();

	try
	{
		BeginParsingProcedure(&result);
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
	ParseResult* result = parser->result;

	if (syncSucceeded)
	{
		throw ParseError(); // Idek anymore
		// Resume parsing
		//BeginParsingProcedure(result, false);
	}
	else
	{
		// The world just exploded
		result->Succeeded = false;
		result->Module = nullptr;

		throw FatalError(); // Fuck this shit
	}
}