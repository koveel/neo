#include "pch.h"
#include "Utils.h"

bool strnchr(const char* string, const char c, int n)
{
	for (int i = 0; i < n; i++)
	{
		if (string[i] == c)
			return true;
	}

	return false;
}

void hash_combine(std::size_t& seed, uint64_t h)
{
	std::hash<uint64_t> hasher;
	seed ^= hasher(h) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}