#include "pch.h"

#include "Lexer.h"
#include "Parser.h"

// returns
// string_view into the source code at the place the error was
// offset from the start of the string_view, where the error originated from
// for the error msg
std::pair<std::string_view, size_t> BuildSourceViewString(const File& file, uint32_t offset)
{
	// Get just the error line
	// most braindead way to do this ever?
	size_t lhs = offset;
	while (lhs > 0)
	{
		char current = file.source[--lhs];
		if (current == '\n')
		{
			do
			{
				current = file.source[++lhs];
			} while (IsWhitespace(current));
			break;
		}
	}
	size_t rhs = offset;
	while (rhs < file.length())
	{
		char current = file.source[rhs];
		if (current == '\n' || current == '\0')
			break;

		rhs++;
	}


	size_t lineLength = rhs - lhs;
	std::string_view line = { file.source.c_str() + lhs, lineLength};

	return { line, offset - lhs };
}

void Internal_LogError(const std::string& error)
{
	Parser* parser = Parser::GetParser();
	const Lexer* lexer = parser->lexer;

	const char* filesource = lexer->file.source.c_str();
	const char* errorLocation = lexer->previousToken.start + lexer->previousToken.length;
	auto pair = BuildSourceViewString(lexer->file, errorLocation - filesource);

	uint32_t line = lexer->line;
	uint32_t column = lexer->column;

	std::cout << lexer->file.name << "\n";
	SetConsoleColor(12);
	std::cout << "[" << lexer->line << "] error: " << error << "\n";
	ResetConsoleColor();

	std::cout << "\t";
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