#include "pch.h"

#include "Lexer.h"

static Lexer* lexer;

static bool IsAtEnd()
{
	return *lexer->current == '\0';
}

static void Advance(int length)
{
	lexer->current += length;
	lexer->column += length;
}

static char Peek()
{
	return *lexer->current;
}


static bool IsAtComment()
{
	return lexer->current[0] == '/' && lexer->current[1] == '/';
}
static void SkipComment()
{
	if (IsAtComment())
	{
		while (*lexer->current != '\n' && *lexer->current != '\0')
			Advance(1);
	}
}

static void SkipWhitespace()
{
	PROFILE_FUNCTION();
	
	SkipComment();

	while (IsWhitespace(*lexer->current))
	{
		if (*lexer->current == '\n')
		{
			lexer->line++;
			lexer->column = 0;
		}

		Advance(1);
	}

	if (IsAtComment())
		SkipWhitespace(); // Recurse if there is another comment after this comment
}

static Token MakeIdentifier()
{
	PROFILE_FUNCTION();
	
	// Advance through identifier name
	while (IsAlpha(Peek()) || IsDigit(Peek()))
		Advance(1);

	Token token;

	token.type = TokenType::ID;
	token.line = lexer->line;
	token.start = lexer->tokenStart;
	token.length = (int)(lexer->current - lexer->tokenStart);

	return token;
}

static Token MakeNumber()
{
	PROFILE_FUNCTION();
		
	const char* currentBeforeDot;
	int columnBeforeDot = 0;

	// Advance through digits
	// A little more complicated than it should be cause we need to make sure theres only one decimal (.)
	// 0..2 should be lexed as Number, MiniEllipsis, Number   instead of Number, Number
	
	bool atDot = false, previousCharWasDot = false;
	while (IsDigit(Peek()) || (Peek() == '.') || Peek() == 'f')
	{
		atDot = Peek() == '.';
		if (atDot && !previousCharWasDot)
		{
			// cheeky little save point
			currentBeforeDot = lexer->current;
			columnBeforeDot = lexer->column;
		}

		if (atDot && previousCharWasDot)
		{
			// The dots arent a part of the number!!
			lexer->current = currentBeforeDot;
			lexer->column = columnBeforeDot;

			break;
		}
		Advance(1);

		previousCharWasDot = atDot;
	}
	//lexer->start = current;

	Token token;

	token.type = TokenType::Number;
	token.line = lexer->line;
	token.start = lexer->tokenStart;
	token.length = (int)(lexer->current - lexer->tokenStart);

	return token;
}

static Token StringToken()
{
	PROFILE_FUNCTION();

	Advance(1);

	lexer->tokenStart = lexer->current; // Manually do this to exclude quotations
	// Advance through chars
	do
	{
		char c = Peek();
		if (c == '\n' || c == '\0')
		{
			Advance(-1);
			LogError("unterminated string");
			return {};
		}

		Advance(1);
	} while (Peek() != '\"');

	Token token;

	token.type = TokenType::String;
	token.line = lexer->line;
	token.start = lexer->tokenStart;
	token.length = (int)(lexer->current - lexer->tokenStart);

	// Advance through closing quote
	Advance(1);

	return token;
}

static Token MakeToken(TokenType type, int length)
{
	Token token;
	
	token.type = type;
	token.line = lexer->line;
	token.start = lexer->tokenStart;
	token.length = length;
	
	Advance(length);

	return token;
}

static bool CheckKeyword(const char* keyword, int length)
{
	for (int i = 0; i < length; i++)
	{
		if (lexer->current[i] != keyword[i])
			return false;
	}

	return true;
}

static void ProcessToken(Token* token)
{
	PROFILE_FUNCTION();

	//SkipWhitespace();

	switch (*lexer->current)
	{
	case '(': *token = MakeToken(TokenType::LeftParen, 1); return;
	case ')': *token = MakeToken(TokenType::RightParen, 1); return;

	case '{': *token = MakeToken(TokenType::LeftCurlyBracket, 1); return;
	case '}': *token = MakeToken(TokenType::RightCurlyBracket, 1); return;

	case '[': *token = MakeToken(TokenType::LeftSquareBracket, 1); return;
	case ']': *token = MakeToken(TokenType::RightSquareBracket, 1); return;

	case '<':
	{
		if (lexer->current[1] == '=')
		{
			*token = MakeToken(TokenType::LessEqual, 2); 
			return;
		}
		if (lexer->current[1] == '<')
		{
			if (lexer->current[2] == '=')
				*token = MakeToken(TokenType::DoubleLessEqual, 3);
			else
				*token = MakeToken(TokenType::DoubleLess, 2);

			return;
		}

		*token = MakeToken(TokenType::Less, 1);
		return;
	}
	case '>':
	{
		if (lexer->current[1] == '=')
		{
			*token = MakeToken(TokenType::GreaterEqual, 2); 
			return;
		}
		if (lexer->current[1] == '>')
		{
			if (lexer->current[2] == '=')
				*token = MakeToken(TokenType::DoubleGreaterEqual, 3);
			else
				*token = MakeToken(TokenType::DoubleGreater, 2);

			return;
		}

		*token = MakeToken(TokenType::Greater, 1);
		return;
	}
	case '^':
	{
		if (lexer->current[1] == '=')
		{
			*token = MakeToken(TokenType::XorEqual, 2);
			return;
		}

		*token = MakeToken(TokenType::Xor, 1);
		return;
	}
	case '~':  *token = MakeToken(TokenType::Tilde, 1); return;
	case '+':
	{
		if (lexer->current[1] == '+')
			*token = MakeToken(TokenType::Increment, 2);
		else if (lexer->current[1] == '=')
			*token = MakeToken(TokenType::PlusEqual, 2);
		else
			*token = MakeToken(TokenType::Plus, 1);

		return;
	}
	case '-':
	{
		switch (lexer->current[1])
		{
		case '-': *token = MakeToken(TokenType::Decrement, 2); return;
		case '=': *token = MakeToken(TokenType::DashEqual, 2); return;
		case '>': *token = MakeToken(TokenType::RightArrow, 2); return;
		default:  *token = MakeToken(TokenType::Dash, 1); return;
		}
	}
	case '*':
	{
		if (lexer->current[1] == '=')
		{
			*token = MakeToken(TokenType::StarEqual, 2);
			return;
		}

		*token = MakeToken(TokenType::Star, 1); 
		return;
	}
	case '/':
	{
		if (lexer->current[1] == '=')
		{
			*token = MakeToken(TokenType::ForwardSlashEqual, 2); 
			return;
		}

		*token = MakeToken(TokenType::ForwardSlash, 1); 
		return;
	}
	case '\\': *token = MakeToken(TokenType::BackSlash, 1); return;
	case '=':
	{
		if (lexer->current[1] == '=')
		{
			*token = MakeToken(TokenType::DoubleEqual, 2); 
			return;
		}

		*token = MakeToken(TokenType::Equal, 1); 
		return;
	}
	case '!':
	{
		if (lexer->current[1] == '=')
		{
			*token = MakeToken(TokenType::ExclamationEqual, 2); 
			return;
		}

		*token = MakeToken(TokenType::Exclamation, 1);
		return;
	}
	case ':':
	{
		if (lexer->current[1] == ':')
			*token = MakeToken(TokenType::DoubleColon, 2);
		else if (lexer->current[1] == '=')
			*token = MakeToken(TokenType::WalrusTeeth, 2);
		else
			*token = MakeToken(TokenType::Colon, 1);

		return;
	}
	case ';':
	{
		*token = MakeToken(TokenType::Semicolon, 1);
		return;
	}
	case '.':
	{
		if (lexer->current[1] == '.')
		{
			if (lexer->current[2] == '.')
				*token = MakeToken(TokenType::Ellipsis, 3);
			else
				*token = MakeToken(TokenType::MiniEllipsis, 2);
		} 
		else {
			*token = MakeToken(TokenType::Dot, 1); 
		}

		return;
	}
	case ',': *token = MakeToken(TokenType::Comma, 1); return;
	case '?': *token = MakeToken(TokenType::QuestionMark, 1); return;
	case '&': 
	{
		if (lexer->current[1] == '&')
		{
			*token = MakeToken(TokenType::DoubleAmpersand, 2);
		}
		else
		{
			if (lexer->current[2] == '=')
				*token = MakeToken(TokenType::AmpersandEqual, 2);
			else
				*token = MakeToken(TokenType::Ampersand, 1);

			return;
		}

		return;
	}
	case '|':
	{
		if (lexer->current[1] == '|')
			*token = MakeToken(TokenType::DoublePipe, 2);
		else if (lexer->current[1] == '=')
			*token = MakeToken(TokenType::PipeEqual, 2);
		else
			*token = MakeToken(TokenType::Pipe, 1);

		return;
	}
	case '%':
	{
		if (lexer->current[1] == '=')
			*token = MakeToken(TokenType::PercentEqual, 2);
		else
			*token = MakeToken(TokenType::Percent, 1);

		return;
	}
	case '@': *token = MakeToken(TokenType::At, 1); return;
	case '#': *token = MakeToken(TokenType::Hashtag, 1); return;

	case '\"': *token = StringToken(); return;

	// Keywords
	case 'b':
	{
		if (CheckKeyword("break", 5))
		{
			*token = MakeToken(TokenType::Break, 5);
			return;
		}

		break;
	}
	case 'c':
	{
		if (CheckKeyword("const", 5))
		{
			*token = MakeToken(TokenType::Const, 5);
			return;
		}
		if (CheckKeyword("continue", 8))
		{
			*token = MakeToken(TokenType::Continue, 8);
			return;
		}
		if (CheckKeyword("cast", 4))
		{
			*token = MakeToken(TokenType::Cast, 4);
			return;
		}

		break;
	}
	case 'e':
	{
		if (CheckKeyword("else", 4))
		{
			*token = MakeToken(TokenType::Else, 4);
			return;
		}

		break;
	}
	case 'f':
	{
		if (CheckKeyword("false", 5))
		{
			*token = MakeToken(TokenType::False, 5);
			return;
		}
		if (CheckKeyword("for", 3))
		{
			*token = MakeToken(TokenType::For, 3);
			return;
		}

		break;
	}
	case 'i':
	{
		if (CheckKeyword("if", 2))
		{
			*token = MakeToken(TokenType::If, 2);
			return;
		}
		if (CheckKeyword("import", 6))
		{
			*token = MakeToken(TokenType::Import, 6);
			return;
		}

		break;
	}
	case 'n':
	{
		if (CheckKeyword("null", 4))
		{
			*token = MakeToken(TokenType::Null, 4);
			return;
		}

		break;
	}
	case 'r':
	{
		if (CheckKeyword("return", 6))
		{
			*token = MakeToken(TokenType::Return, 6);
			return;
		}

		break;
	}
	case 's':
	{
		if (CheckKeyword("struct", 6))
		{
			*token = MakeToken(TokenType::Struct, 6);
			return;
		}

		break;
	}
	case 't':
	{
		if (CheckKeyword("true", 4))
		{
			*token = MakeToken(TokenType::True, 4);
			return;
		}

		break;
	}
	}

	// Handle end token
	if (IsAtEnd())
	{
		*token = MakeToken(TokenType::Eof, 0);
		return;
	}

	// Handle numbers and identifiers
	if (IsAlpha(Peek()))
	{
		*token = MakeIdentifier();
		return;
	}
	else if (IsDigit(Peek()))
	{
		*token = MakeNumber();
		return;
	}

	// Unknown token
	Advance(1);

	LogError("unexpected token '{}'", lexer->tokenStart);
}

Token Lexer::Next()
{
	PROFILE_FUNCTION();

	// Advance
	SkipWhitespace();

	tokenStart = current;

	previousToken = currentToken;

	ProcessToken(&currentToken);
	const char* oldStart = tokenStart;

	// We don't want the lexer to *actually* advance when we process the next token
	const char* oldCurrent = current;
	int oldLine = line;
	int oldColumn = column;
	tokenStart = current;
	
	SkipWhitespace();

	ProcessToken(&nextToken);

	current = oldCurrent;
	tokenStart = oldStart;
	line = oldLine;
	column = oldColumn;

	return currentToken;
}

bool Lexer::Expect(TokenType type)
{
	return currentToken.type == type;
}

Lexer::Lexer(const File& file)
	: file(file)
{
	lexer = this;

	current = file.source.c_str();
	tokenStart = current;
}