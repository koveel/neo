#pragma once

#include <string>
#include <memory>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <format>

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