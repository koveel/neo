#include "Lexer.h"
#include "Module.h"

class Lexer;

struct ParseResult
{
	bool Succeeded = true;
	Module module;
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