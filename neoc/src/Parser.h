#pragma once

#include "Lexer.h"

struct ParseResult
{
	bool Succeeded = false;
	std::unique_ptr<struct CompoundStatement> Module;
};

enum class ParseState
{
	Default,
	Definition, // { ... }
};

class Parser
{
public:
	Parser();
	ParseResult Parse(Lexer* lexer);

	// Flag error, attempt synchronization
	static void Panic();

	static Parser* GetParser(); // fuck this bruh
public:
	Token current;
	uint32_t scopeDepth = 0;

	Lexer* lexer = nullptr;
	ParseResult* result = nullptr;
};