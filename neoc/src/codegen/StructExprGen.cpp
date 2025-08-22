#include "pch.h"

#include "Tree.h"

#include "Generator.h"
#include <llvm/IR/InstrTypes.h>

#include "CodegenUtils.h"

void Generator::InitializeStructMembersToDefault(llvm::Value* structPtr, StructType* type)
{
	PROFILE_FUNCTION();

	StructDefinitionExpression* definition = type->definition;

	uint32_t i = 0;
	for (auto& member : definition->members)
	{
		auto& init = member->initializer;
		Value initialValue = {};
		if (!init) {
			initialValue = RValue{ GetNumericConstant(member->type->tag, 0), member->type };
		}
		else {
			initialValue = init->Generate(*this);
		}

		llvm::Value* memberPtr = EmitStructGEP(type, structPtr, i++);
		ASSERT(initialValue.is_rvalue);
		EmitStore(initialValue.rvalue.value, memberPtr);
	}
}

static uint32_t GetIndexOfMemberInStruct(const std::string& targetMember, StructType* type)
{
	PROFILE_FUNCTION();

	StructDefinitionExpression* definition = type->definition;

	uint32_t i = 0;
	for (auto& member : definition->members)
	{
		if (targetMember == member->name)
			return i;

		i++;
	}

	return std::numeric_limits<uint32_t>::max();
}

// TODO: cleanup ?
void Generator::InitializeStructMembersAggregate(llvm::Value* structPtr, StructType* type, CompoundExpression* initializer)
{
	PROFILE_FUNCTION();

	StructDefinitionExpression* definition = type->definition;

	std::vector<uint32_t> initializedMembers;
	initializedMembers.reserve(initializer->children.size());

	bool usedNamedInitialization = false;
	uint32_t i = 0;
	for (auto& expr : initializer->children)
	{
		BinaryExpression* binary = nullptr;
		if ((binary = ToExpr<BinaryExpression>(expr)) && binary->binaryType == BinaryType::Assign)
		{
			auto member = ToExpr<VariableAccessExpression>(binary->left);
			const std::string& memberName = member->name;
			ASSERT(member);

			uint32_t memberIndex = GetIndexOfMemberInStruct(memberName, type);
			if (memberIndex == std::numeric_limits<uint32_t>::max())
				throw CompileError(expr->sourceLine, "member '{}' doesn't exist in struct '{}'", memberName.c_str(), type->GetName().c_str());

			if (std::find(initializedMembers.begin(), initializedMembers.end(), memberIndex) != initializedMembers.end())
				throw CompileError(expr->sourceLine, "member '{}' appears multiple times in aggregate initializer. can only assign to it once", memberName.c_str());
			initializedMembers.push_back(memberIndex);

			Value value = binary->right->Generate(*this);
			RValue rv = MaterializeToRValue(value);
			rv = CastRValueIfNecessary(rv, type->members[memberIndex], false, binary->right.get());

			llvm::Value* memberPtr = EmitStructGEP(type, structPtr, memberIndex);
			EmitStore(rv.value, memberPtr);

			usedNamedInitialization = true;
		}
		else
		{
			if (usedNamedInitialization)
				throw CompileError(expr->sourceLine, "if using named initialization \"member = x\", must use it for all subsequent initializations");

			Value value = expr->Generate(*this);
			RValue rv = MaterializeToRValue(value);
			uint32_t memberIndex = i++;
			rv = CastRValueIfNecessary(rv, type->members[memberIndex], false, expr.get());

			llvm::Value* memberPtr = EmitStructGEP(type, structPtr, memberIndex);
			EmitStore(rv.value, memberPtr);
		}
	}
}

Value StructDefinitionExpression::Generate(Generator& generator)
{
	// We already resolved the struct type and its members but here we just flag an error if a member type if unresolved

	for (auto& vardef : members)
	{
		Type* memberType = vardef->type;
		if (memberType->tag != TypeTag::Unresolved)
			continue;

		throw CompileError(vardef->sourceLine, "unresolved type '{}' for member '{}' in struct '{}'",
			memberType->GetName().c_str(), vardef->name.c_str(), name.c_str());
	}

	return {};
}