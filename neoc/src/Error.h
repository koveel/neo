#pragma once

#include "Parser.h"

// Error during parsing / lexing
//struct ParseError : std::exception
//{
//	int GetLine();
//	int GetColumn();
//	
//	template<typename... Args>
//	ParseError(const char* format, Args&&... args)
//		: line(GetLine()), column(GetColumn())
//	{
//		message = FormatString(format, std::forward<Args>(args)...);
//
//		column = GetColumn();
//	}
//
//	int line = 0, column = 0;
//	std::string message;
//};

template<typename... Args>
extern void LogError(const char* format, Args&&... args)
{
	Parser* parser = Parser::GetParser();
	fprintf(stderr, "error (line %d): %s\n", parser->lexer->line, FormatString(format, std::forward<Args>(args)...).c_str());
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