#pragma once

struct File
{
	File() = default;
	File(const std::string& name, const std::string& source)
		: name(name), source(source) {
	}

	std::string name;
	std::string source;

	constexpr size_t length() const { return source.length(); }
};