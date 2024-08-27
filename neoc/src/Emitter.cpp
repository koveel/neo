#include "pch.h"

#include "Emitter.h"


void Emitter::Emit(const std::string& ir, const char* filepath)
{
	PROFILE_FUNCTION();

	std::ofstream outStream(filepath);

	outStream.write(ir.c_str(), ir.size());

	outStream.close();
}