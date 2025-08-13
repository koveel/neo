#pragma once

#include "codegen/Enum.h"

// unnamed
struct FunctionSignature
{
	Type* Return = nullptr;
	std::vector<Type*> Parameters;
};

class Module
{
public:
	Module() = default;
	
	std::unique_ptr<struct CompoundExpression> SyntaxTree;
	std::unordered_map<std::string, FunctionSignature> DefinedFunctions;
	std::unordered_map<std::string, Enumeration> DefinedEnums;

	//std::unique_ptr<llvm::Module> llvm_module;
	llvm::Module* llvm_module = nullptr;
};