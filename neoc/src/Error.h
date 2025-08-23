#pragma once

#include "cmdline/File.h"

#define ASSERT(x) if (!(x)) { __debugbreak(); }

void Internal_LogError(const std::string& error); // parser
void Internal_AssertionFailed(const std::string& error); // generator
std::pair<std::string_view, size_t> BuildSourceViewString(const File& file, uint32_t offset);

template<typename... Args>
static void LogError(const char* format, Args&&... args)
{
 	Internal_LogError(FormatString(format, std::forward<Args>(args)...));
}

template<typename... Args>
static void Assert(bool condition, const char* format, Args&&... args)
{
	if (condition)
		return;

	Internal_AssertionFailed(FormatString(format, std::forward<Args>(args)...));
}

// Error during code gen
struct CompileError : std::exception
{
	CompileError(const std::string& msg)
		: message(msg)
	{}

	std::string message;
};