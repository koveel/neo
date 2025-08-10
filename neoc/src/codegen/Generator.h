#pragma once

#include "Type.h"
#include "Parser.h"
#include "Scope.h"
#include "CmdLine/CommandLineArguments.h"

struct CompileResult
{
	std::string ir;
	bool Succeeded = true;
};

class Type;
class StructType;
struct Expression;
struct BinaryExpression;
struct CompoundExpression;

class Generator
{
public:
	Module& module;
public:
	Generator(Module& module);

	// Instructions
	static llvm::Value* EmitStore(llvm::Value* value, llvm::Value* ptr);
	static llvm::Value* EmitLoad(llvm::Value* ptr);
	static llvm::Value* EmitBinaryOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs);
	static llvm::Value* EmitComparisonOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs);

	static llvm::Value* EmitAlloca(llvm::Type* type, llvm::Value* arraySize = nullptr);

	static llvm::Value* EmitSubscript(BinaryExpression* expression);
	static llvm::Value* EmitStructureMemberAccess(BinaryExpression* expression);
	static llvm::Value* HandleMemberAccessExpression(BinaryExpression* expression);

	static llvm::Value* LoadValueIfVariable(llvm::Value* generated, Expression* expr);
	static llvm::Value* LoadValueIfVariable(llvm::Value* generated, std::unique_ptr<Expression>& expr);
	static llvm::Value* CastValueIfNecessary(llvm::Value* v, Type* from, Type* to, bool isExplicit, Expression* source);

	static llvm::Value* EmitStructGEP(llvm::Value* ptr, uint32_t memberIndex);
	static llvm::Value* EmitInBoundsGEP(llvm::Type* type, llvm::Value* ptr, std::initializer_list<llvm::Value*> indices);

	static llvm::Value* GetNumericConstant(TypeTag tag, int64_t value);

	// Struct
	static void InitializeStructMembersAggregate(llvm::Value* structPtr, StructType* type, CompoundExpression* initializer);
	static void InitializeStructMembersToDefault(llvm::Value* structPtr, StructType* type);

	// Array
	static llvm::Value* CreateArrayAlloca(llvm::Type* arrayType, const std::vector<std::unique_ptr<Expression>>& elements);

	// Values
	static bool ScopeValue(const std::string& name, const Value& value);

	CompileResult Generate(ParseResult& parseResult, const CommandLineArguments& compilerArgs);
};