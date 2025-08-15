#pragma once

#include "Tree.h"
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
	Scope* currentScope = nullptr;
	struct LLVM* llvm_context = nullptr;
public:
	Generator(Module& module);
	~Generator();

	CompileResult Generate(ParseResult& parseResult, const CommandLineArguments& compilerArgs);

	// Instructions
	llvm::Value* EmitStore(llvm::Value* value, llvm::Value* ptr);
	llvm::Value* EmitLoad(Type* resultType, llvm::Value* ptr);
	llvm::Value* EmitBinaryOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs, const char* debug_name = "");
	llvm::Value* EmitComparisonOperator(uint32_t op, llvm::Value* lhs, llvm::Value* rhs);

	llvm::Value* EmitAlloca(Type* type, llvm::Value* arraySize = nullptr, const char* debug_name = "");

	llvm::Value* EmitSubscript(BinaryExpression* expression, const char* debug_name = "");
	llvm::Value* EmitStructureMemberAccess(BinaryExpression* expression);
	llvm::Value* HandleMemberAccessExpression(BinaryExpression* expression);

	llvm::Value* LoadValueIfVariable(llvm::Value* generated, Expression* expr);
	llvm::Value* LoadValueIfVariable(llvm::Value* generated, std::unique_ptr<Expression>& expr);
	llvm::Value* CastValueIfNecessary(llvm::Value* v, Type* from, Type* to, bool isExplicit, Expression* source);

	llvm::Value* EmitStructGEP(StructType* structType, llvm::Value* structPtr, uint32_t memberIndex);
	llvm::Value* EmitInBoundsGEP(Type* resultType, llvm::Value* ptr, std::initializer_list<llvm::Value*> indices, const char* debug_name = "");

	llvm::Value* GetNumericConstant(TypeTag tag, int64_t value);

	// Struct
	void InitializeStructMembersAggregate(llvm::Value* structPtr, StructType* type, CompoundExpression* initializer);
	void InitializeStructMembersToDefault(llvm::Value* structPtr, StructType* type);

	// Array
	llvm::Value* CreateArrayAlloca(ArrayType* arrayType, const std::vector<std::unique_ptr<Expression>>& elements, const char* debug_name = "");

	// Values
	bool ScopeValue(const std::string& name, const Value& value);
private:
	void ResolveParsedTypes();
	void ResolveType(Type&, int line = -1);
	void ResolvePrimitiveType(Type& type, int possibleSourceLine = -1);
	void ResolveStructType(StructType& type, int possibleSourceLine = -1);
	void ResolveArrayType(ArrayType& type, int possibleSourceLine = -1);

	// visiting.cpp
	void VisitTopLevelDefinitions();
	void VisitFunctionDefinition(FunctionDefinitionExpression* expr);
	void VisitEnumDefinition(EnumDefinitionExpression* expr);

	friend class Type;
};