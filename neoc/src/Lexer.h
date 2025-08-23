#pragma once

static bool IsWhitespace(char c)
{
	return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}
static bool IsAlpha(char c)
{
	return (c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		c == '_';
}
static bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

enum class TokenType : char
{
	Error, Eof,

	// Brackets
	LeftParen, RightParen, LeftCurlyBracket, RightCurlyBracket, LeftSquareBracket, RightSquareBracket,
	
	Plus, Dash, Star, Exclamation, ForwardSlash,
	BackSlash, Quotation, Dot, Comma, QuestionMark, 
	Equal, DoubleEqual, ExclamationEqual, Greater, GreaterEqual, Less, LessEqual,
	Pipe, PipeEqual, DoublePipe,
	Ampersand, AmpersandEqual, DoubleAmpersand,

	// Bitwise
	Tilde,
	DoubleLess, DoubleLessEqual,
	DoubleGreater, DoubleGreaterEqual,
	Xor, XorEqual,

	Percent, PercentEqual,
	At, Hashtag, ID,
	RightArrow, Ellipsis, MiniEllipsis,

	// Compound assignment and whatnot
	PlusEqual, DashEqual, StarEqual, ForwardSlashEqual,
	Increment, Decrement,

	// Colons
	Colon, DoubleColon, Semicolon, WalrusTeeth,

	// Keywords
	If, Else,
	True, False,
	Return,
	Const,
	Struct,
	Enum,
	Null,
	Import,
	For, Continue, Break,
	Cast,
	

	// Literals
	String, Number,
};

struct Token
{
	TokenType type = (TokenType)0;
	const char* start;
	uint32_t line = 0, length = 1;
};

class Lexer
{
public:
	Lexer(const File& file);
	
	Token Next();

	bool Expect(TokenType type); // Returns whether or not current is of type 'type'
public:
	// Used for peeking
	Token previousToken, currentToken, nextToken;

	File file;
	uint32_t line = 1, column = 0;

	const char* current; // Current character
	const char* tokenStart; // First character of token being lexed
};