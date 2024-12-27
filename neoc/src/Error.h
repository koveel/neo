#pragma once

#define ASSERT(x) if (!(x)) { __debugbreak(); }

void Internal_LogError(const char* error);

template<typename... Args>
extern void LogError(const char* format, Args&&... args)
{
	Internal_LogError(FormatString(format, std::forward<Args>(args)...).c_str());
}

// Error during code gen
struct CompileError : std::exception
{
	template<typename... Args>
	CompileError(int line, const char* format, Args&&... args)
		: line(line), message(FormatString(format, std::forward<Args>(args)...))
	{
	}

	int line = 0;
	std::string message;
};