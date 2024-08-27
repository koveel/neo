#pragma once

struct Emitter
{
	void Open();
	void Emit(const std::string& ir);
	void Emit(const std::string& ir, const char* filepath);
};