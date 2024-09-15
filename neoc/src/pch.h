#pragma once

// std stuff (not that std)
#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <unordered_map>

// my stuff
#include "PlatformUtils.h"
#include "Utils.h"
#include "Error.h"
#include "Profiler.h"

// Highk down syndrome mode
namespace llvm
{
	class Type;
	class Value;
}