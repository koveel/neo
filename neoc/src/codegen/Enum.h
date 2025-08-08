#pragma once

#include "Type.h"

// 10x developer
struct Enumeration
{
	std::string name;
	Type* integralType = nullptr;
	std::unordered_map<std::string, class llvm::Value*> members;
};