#include "pch.h"

#include "Tree.h"

#include "Cast.h"
#include "Generator.h"
#include <llvm/IR/InstrTypes.h>

static bool IsArithmetic(BinaryType type)
{
	uint32_t num = (uint32_t)type;
	return num >= 1 && num <= 8;
}

static bool IsComparison(BinaryType type)
{
	return type >= BinaryType::Equal && type <= BinaryType::GreaterEqual;
}

static llvm::Instruction::BinaryOps LLVMBinaryOpFromBinaryExpr(BinaryType operation, Type* lhs)
{
	using namespace llvm;

	// Floating point
	bool isFP = lhs->IsFloatingPoint();
	if (isFP)
	{
		switch (operation)
		{
		case BinaryType::Add:      return Instruction::BinaryOps::FAdd;
		case BinaryType::Subtract: return Instruction::BinaryOps::FSub;
		case BinaryType::Multiply: return Instruction::BinaryOps::FMul;
		case BinaryType::Divide:   return Instruction::BinaryOps::FDiv;
		}
	}

	bool isDivision = operation == BinaryType::Divide;
	bool isModulo = operation == BinaryType::Modulo;

	static Instruction::BinaryOps integerDivisionOps[] = { Instruction::BinaryOps::UDiv, Instruction::BinaryOps::SDiv };
	static Instruction::BinaryOps integerRemainderOps[] = { Instruction::BinaryOps::URem, Instruction::BinaryOps::SRem };

	// Integer division
	bool isSigned = lhs->IsSigned();
	if (isDivision) {
		return integerDivisionOps[isSigned];
	}
	if (isModulo) {
		return integerRemainderOps[isSigned];
	}

	// Other integer arithmetic
	switch (operation)
	{
	case BinaryType::Add:        return Instruction::BinaryOps::Add;
	case BinaryType::Subtract:   return Instruction::BinaryOps::Sub;
	case BinaryType::Multiply:   return Instruction::BinaryOps::Mul;
	case BinaryType::LeftShift:  return Instruction::BinaryOps::Shl;
	case BinaryType::RightShift: return Instruction::BinaryOps::LShr;
	case BinaryType::Xor:        return Instruction::BinaryOps::Xor;
	case BinaryType::BitwiseAnd: return Instruction::BinaryOps::And;
	case BinaryType::Or: 
	case BinaryType::BitwiseOr:  return Instruction::BinaryOps::Or;
	}

	ASSERT(false);
	return {};
}

// also wack
static llvm::CmpInst::Predicate LLVMCompareOpFromBinaryExpr(BinaryType operation, Type* lhs)
{
	using namespace llvm;

	ASSERT(IsComparison(operation));

	bool isFP = lhs->IsFloatingPoint();
	if (isFP)
	{
		switch (operation)
		{
		case BinaryType::Equal:        return CmpInst::Predicate::FCMP_UEQ;
		case BinaryType::NotEqual:     return CmpInst::Predicate::FCMP_UNE;
		case BinaryType::Less:         return CmpInst::Predicate::FCMP_ULT;
		case BinaryType::LessEqual:    return CmpInst::Predicate::FCMP_ULE;
		case BinaryType::Greater:      return CmpInst::Predicate::FCMP_UGT;
		case BinaryType::GreaterEqual: return CmpInst::Predicate::FCMP_UGE;
		}
	}

	bool isSigned = lhs->IsSigned();
	switch (operation)
	{
	case BinaryType::Equal:        return CmpInst::Predicate::ICMP_EQ;
	case BinaryType::NotEqual:     return CmpInst::Predicate::ICMP_NE;
	case BinaryType::Less:         return isSigned ? CmpInst::Predicate::ICMP_SLT : CmpInst::Predicate::ICMP_ULT;
	case BinaryType::LessEqual:    return isSigned ? CmpInst::Predicate::ICMP_SLE : CmpInst::Predicate::ICMP_ULE;
	case BinaryType::Greater:      return isSigned ? CmpInst::Predicate::ICMP_SGT : CmpInst::Predicate::ICMP_UGT;
	case BinaryType::GreaterEqual: return isSigned ? CmpInst::Predicate::ICMP_SGE : CmpInst::Predicate::ICMP_UGE;
	}

	ASSERT(false);
	return {};
}

static void ResolveBinaryExpressionTypeDiscrepancy(std::pair<llvm::Value*&, Type*&> left, std::pair<llvm::Value*&, Type*&> right,
	BinaryExpression* binaryExpr)
{
	PROFILE_FUNCTION();

	std::pair<llvm::Value*&, Type*&> exprs[2] = { left, right };

	Type* lType = left.second;
	Type* rType = right.second;

	// If an operand is floating point, make other operand floating point too
	bool hasFP = lType->IsFloatingPoint() || rType->IsFloatingPoint();
	if (hasFP)
	{
		uint32_t nonFP = lType->IsFloatingPoint() ? 1 : 0;
		uint32_t fp = 1 - nonFP;

		Cast* cast = Cast::IsValid(exprs[nonFP].second, exprs[fp].second);
		if (!cast) {
			throw CompileError(binaryExpr->sourceLine, "cannot convert from '{}' to '{}'", rType->GetName(), lType->GetName());
		}
		exprs[nonFP].first = cast->Invoke(exprs[nonFP].first);
		exprs[nonFP].second = exprs[fp].second;
		return;
	}

	// If an operand is signed int, make other operand signed int too
	bool hasSignedInt = lType->IsSigned() || rType->IsSigned();
	if (hasSignedInt)
	{
		uint32_t nonSign = lType->IsSigned() ? 1 : 0;
		uint32_t sign = 1 - nonSign;

		Cast* cast = Cast::IsValid(exprs[nonSign].second, exprs[sign].second);
		if (!cast) {
			throw CompileError(binaryExpr->sourceLine, "cannot convert from '{}' to '{}'", rType->GetName(), lType->GetName());
		}
		exprs[nonSign].first = cast->Invoke(exprs[nonSign].first);
		exprs[nonSign].second = exprs[sign].second;
		return;
	}
}

llvm::Value* BinaryExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();

	using namespace llvm;

	if (binaryType == BinaryType::MemberAccess)
		return generator.HandleMemberAccessExpression(this);
	if (binaryType == BinaryType::Subscript)
		return generator.EmitSubscript(this);

	llvm::Value* lhs = left->Generate(generator);
	llvm::Value* rhs = right->Generate(generator);
	llvm::Value* pointerLhs = lhs;

	type = left->type;

	// Unless assiging, treat variables as the underlying values
	if (binaryType != BinaryType::Assign)
		lhs = generator.LoadValueIfVariable(lhs, left);
	rhs = generator.LoadValueIfVariable(rhs, right);

	::Type* leftType = left->type;
	::Type* rightType = right->type;
	if (leftType != rightType)
	{
		ResolveBinaryExpressionTypeDiscrepancy({ lhs, leftType }, { rhs, rightType }, this);
		//rhs = generator.CastValueIfNecessary(rhs, right->type, left->type, false, this);
	}

	llvm::Type* lhsType = lhs->getType();
	llvm::Type* rhsType = rhs->getType();

	Instruction::BinaryOps instruction = (Instruction::BinaryOps)-1;
	switch (binaryType)
	{
	case BinaryType::Assign:
	{
		generator.EmitStore(rhs, lhs);
		return rhs;
	}
	case BinaryType::Equal:
	case BinaryType::NotEqual:
	case BinaryType::Less:
	case BinaryType::LessEqual:
	case BinaryType::Greater:
	case BinaryType::GreaterEqual:
		return generator.EmitComparisonOperator(LLVMCompareOpFromBinaryExpr(binaryType, leftType), lhs, rhs);
	default:
	{
		instruction = LLVMBinaryOpFromBinaryExpr(binaryType, leftType);
		break;
	}
	}

	
	llvm::Value* result = generator.EmitBinaryOperator(instruction, lhs, rhs);

	if (isCompoundAssignment)
		generator.EmitStore(result, pointerLhs);

	return result;
}