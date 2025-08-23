#include "pch.h"

#include "Tree.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>

#include "Generator.h"
#include "CodegenUtils.h"

void Generator::VisitFunctionDefinition(FunctionDefinitionExpression* expr)
{
	auto& llvm_module = module.llvm_module;

	FunctionPrototype& prototype = expr->prototype;
	Assert(!llvm_module->getFunction(prototype.Name), "redefinition of function '{}'", prototype.Name);

	std::vector<Type*> parameterTypes(prototype.Parameters.size());
	uint32_t i = 0;
	for (auto& param : prototype.Parameters)
		parameterTypes[i++] = param->type;
	i = 0;

	// Keep track of all functions in module for function call expressions
	module.DefinedFunctions[prototype.Name] = { prototype.ReturnType, parameterTypes };

	// Param types
	std::vector<llvm::Type*> llvmParameterTypes(prototype.Parameters.size());
	for (auto& ty : parameterTypes)
		llvmParameterTypes[i++] = ty->raw;

	llvm::Type* returnType = prototype.ReturnType->raw;

	llvm::FunctionType* functionType = llvm::FunctionType::get(returnType, llvmParameterTypes, false);
	llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, prototype.Name, *module.llvm_module);
}

void Generator::VisitEnumDefinition(EnumDefinitionExpression* expr)
{
	ASSERT(!module.DefinedEnums.count(expr->name));

	Enumeration& enumeration = module.DefinedEnums[expr->name];
	enumeration.name = expr->name;

	AliasType* alias = expr->type->IsAlias();
	ASSERT(alias);
	enumeration.integralType = alias->aliasedType;

	bool hasUsedDeducedValue = false;
	uint64_t memberIndex = 0;
	uint64_t memberValueTracker = 0; // idkf
	for (auto& member : expr->members)
	{
		// Must be either a variable expr or binary expression (variable = constant)
		if (auto variable = ToExpr<VariableAccessExpression>(member))
		{
			hasUsedDeducedValue = true;

			const std::string& memberName = variable->name;

			llvm::Value* value = value = GetNumericConstant(enumeration.integralType->tag, memberValueTracker++);
			enumeration.members[memberName] = value;
		}
		else if (auto binary = ToExpr<BinaryExpression>(member))
		{
			Assert(!hasUsedDeducedValue, "within an enum, explicitly assigning values is only allowed before all deduced ones");

			VariableAccessExpression* variable = ToExpr<VariableAccessExpression>(binary->left);
			Assert(variable, "expected identifier for enum member {}", memberIndex);

			const std::string& memberName = variable->name;
			Value value = binary->right->Generate(*this);
			ASSERT(value.is_rvalue);
			value = CastRValueIfNecessary(value.rvalue, enumeration.integralType, false, binary->right.get());

			enumeration.members[memberName] = value.rvalue.value;
		}
		else {
			Assert(false, "invalid member {} for enum '{}'", memberIndex, expr->name);
		}

		memberIndex++;
	}
}

// TODO: FIX AND ABSTRACT
void Generator::VisitTopLevelDefinitions()
{
	PROFILE_FUNCTION();

	// only top level
	for (auto& node : module.SyntaxTree->children)
	{
		switch (node->exprType) {
		case ExpressionType::FunctionDefinition: {
			VisitFunctionDefinition(ToExpr<FunctionDefinitionExpression>(node));
			break;
		}
		case ExpressionType::EnumDefinition: {
			VisitEnumDefinition(ToExpr<EnumDefinitionExpression>(node));
			break;
		}
		}

		continue;
	}
}