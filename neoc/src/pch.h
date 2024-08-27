#pragma once

// std stuff (not that std)
#include <string>
#include <memory>
#include <vector>
#include <fstream>

// llvm stuff
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>

// my stuff
#include "Utils.h"
#include "Error.h"
#include "Profiler.h"