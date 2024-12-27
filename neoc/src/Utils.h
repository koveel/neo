#pragma once

template<typename... Args>
static std::string FormatString(const char* format, Args&&... args)
{
	auto size = (size_t)snprintf(nullptr, 0, format, args...) + 1; // Room for null terminating char
	char* buffer = new char[size];
	std::snprintf(buffer, size, format, std::forward<Args>(args)...);

	std::string result{ buffer, buffer + size - 1 };

	delete[] buffer;

	return result;
}

// check if a string contains a char up to a certain amount of characters
bool strnchr(const char* string, const char c, int n);

void hash_combine(std::size_t& seed, uint64_t v);