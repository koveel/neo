#pragma once

#include <filesystem>

struct Emitter
{
	void Emit(const std::string& ir, const std::filesystem::path& filepath);
};