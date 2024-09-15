#pragma once

#include "Parser.h"

#define ASSERT(x) if (!(x)) { __debugbreak(); }

template<typename... Args>
extern void LogError(const char* format, Args&&... args)
{
	Parser* parser = Parser::GetParser();
	SetConsoleColor(12);
	fprintf(stderr, "error (line %d): %s\n", parser->lexer->line, FormatString(format, std::forward<Args>(args)...).c_str());
	ResetConsoleColor();
	Parser::Panic();
}

// Error during code gen
struct CompileError : std::exception
{
	template<typename... Args>
	CompileError(int line, const char* format, Args&&... args)
		: line(line), message(FormatString(format, std::forward<Args>(args)...))
	{
	}

	int line = 0;
	std::string message;
};