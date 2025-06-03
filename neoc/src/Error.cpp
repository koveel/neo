#include "pch.h"

#include "Lexer.h"
#include "Parser.h"

void Internal_LogError(const std::string& error)
{
	Parser* parser = Parser::GetParser();
	SetConsoleColor(12);
	std::cout << "[line " << parser->lexer->line << "] error: " << error << "\n";
	ResetConsoleColor();
	Parser::Panic();
}