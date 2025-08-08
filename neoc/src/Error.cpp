#include "pch.h"

#include "Lexer.h"
#include "Parser.h"

// returns
// string_view into the source code at the place the error was
// offset from the start of the string_view, where the error originated from
// for the error msg
static std::pair<std::string_view, size_t> BuildSourceViewString(const Lexer& lexer)
{
	const char* filesource = lexer.file.source.c_str();
	const char* errorLocation = lexer.previousToken.start + lexer.previousToken.length;
	size_t rangeOffset = errorLocation - filesource;

	// Get just the error line
	// most braindead way to do this ever?
	size_t lhs = rangeOffset;
	while (lhs > 0)
	{
		char current = filesource[--lhs];
		if (current == '\n')
		{
			do
			{
				current = filesource[++lhs];
			} while (IsWhitespace(current));
			break;
		}
	}
	size_t rhs = rangeOffset;
	while (rhs < lexer.file.length())
	{
		char current = filesource[rhs];
		if (current == '\n' || current == '\0')
			break;

		rhs++;
	}


	size_t lineLength = rhs - lhs;
	std::string_view line = { filesource + lhs, lineLength };

	return { line, rangeOffset - lhs };
}

void Internal_LogError(const std::string& error)
{
	Parser* parser = Parser::GetParser();
	const Lexer* lexer = parser->lexer;

	uint32_t line = lexer->line;
	uint32_t column = lexer->column;

	std::cout << lexer->file.name << "\n";
	SetConsoleColor(12);
	std::cout << "[" << lexer->line << "] error: " << error << "\n";
	ResetConsoleColor();

	std::cout << "\t";
	auto pair = BuildSourceViewString(*lexer);
	std::cout << pair.first << "\n\t";
	SetConsoleColor(12);

	auto indent = [&pair](size_t offset) {
		for (size_t i = 0; i < std::max(pair.second, offset) - offset; i++)
			std::cout << " ";
	};
	indent(0);
	std::cout << "|\n\t";
	indent(2);
	std::cout << "there\n";
	ResetConsoleColor();

	std::cout << "\n";

	Parser::Panic();
}