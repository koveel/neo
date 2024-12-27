#include "pch.h"

#include "Lexer.h"
#include "Parser.h"

void Internal_LogError(const char* error)
{
	Parser* parser = Parser::GetParser();
	SetConsoleColor(12);
	fprintf(stderr, "error (line %d): %s\n", parser->lexer->line, error);
	ResetConsoleColor();
	Parser::Panic();
}