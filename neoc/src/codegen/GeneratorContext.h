#pragma once

struct LLVM
{
	std::unique_ptr<llvm::LLVMContext> context;
	std::unique_ptr<llvm::IRBuilder<>> builder;

	struct
	{
		llvm::Function* function = nullptr;
		llvm::Value* return_value_alloca = nullptr;
		llvm::BasicBlock* return_block = nullptr;
	} current_function;

	struct
	{
		llvm::BasicBlock* end_block = nullptr;
		llvm::BasicBlock* condition_block = nullptr;
	} current_loop;
};