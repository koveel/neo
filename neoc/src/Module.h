#pragma once

#include "codegen/Enum.h"

class Module
{
public:
	Module() = default;
	
	std::unique_ptr<struct CompoundExpression> SyntaxTree;
	//std::unordered_map<std::string, struct FunctionDefinitionExpression*> DefinedFunctions;
	std::unordered_map<std::string, Enumeration> DefinedEnums;
};