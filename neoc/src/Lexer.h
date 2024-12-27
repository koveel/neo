#pragma once

enum class TokenType : char
{
	Error, Eof,

	// Brackets
	LeftParen, RightParen, LeftCurlyBracket, RightCurlyBracket, LeftSquareBracket, RightSquareBracket,
	
	// Single character
	Plus, Dash, Star, Equal, Exclamation, ForwardSlash, Tilde,
	BackSlash, Quotation, Dot, Comma, QuestionMark, 
	DoubleEqual, ExclamationEqual, Greater, GreaterEqual, Less, LessEqual,

	// Compound assignment and whatnot
	PlusEqual, DashEqual, StarEqual, ForwardSlashEqual,
	Increment, Decrement,

	// Colon stuff... ew
	Colon, DoubleColon, Semicolon, WalrusTeeth,

	// Keywords
	If, Else,
	True, False,
	Return,
	Const,
	Struct,
	Null,
	Import,
	For, Continue, Break,
	Cast,

	// Misc
	Percent, At, Hashtag, ID,
	Pipe, DoublePipe,
	Ampersand, DoubleAmpersand,
	RightArrow, Ellipsis, MiniEllipsis,

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
	Lexer(const char* source);
	
	Token Next();

	bool Expect(TokenType type); // Returns whether or not current is of type 'type'
public:
	// Used for peeking
	Token previousToken, currentToken, nextToken;

	uint32_t line = 1, column = 0;
	const char* current; // Current character
	const char* start; // First character of token being lexed
};