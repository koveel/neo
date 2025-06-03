#pragma once

template<typename... Args>
static std::string FormatString(std::string_view format, Args&&... args)
{
	return std::vformat(format, std::make_format_args(args...));
}

// check if a string contains a char up to a certain amount of characters
bool strnchr(const char* string, const char c, int n);
void hash_combine(std::size_t& seed, uint64_t v);