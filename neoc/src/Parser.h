#pragma once

class Lexer;

struct ParseResult
{
	bool Succeeded = false;
	std::unique_ptr<struct CompoundStatement> Module;
};

class Parser
{
public:
	Parser();
	ParseResult Parse(Lexer* lexer);

	// Flag error, attempt synchronization
	static void Panic();

	static Parser* GetParser(); // only used in error.h cause im dumb
public:
	Token current;
	uint32_t scopeDepth = 0;

	Lexer* lexer = nullptr;
	ParseResult* result = nullptr;
};