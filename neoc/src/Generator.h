#pragma once

#include "Parser.h"
#include "CmdLine/CommandLineArguments.h"

struct CompileResult
{
	std::string ir;
	bool Succeeded = true;
};

class Generator
{
public:
	Generator();

	CompileResult Generate(ParseResult& parseResult, const CommandLineArguments& compilerArgs);
};