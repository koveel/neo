#include "pch.h"

#include "Tree.h"
#include "Cast.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>

#include "Value.h"

#include "Generator.h"
#include "GeneratorContext.h"

static bool IsArithmetic(BinaryType type)
{
	switch (type)
	{
	case BinaryType::Add:
	case BinaryType::Divide:
	case BinaryType::Subtract:
	case BinaryType::Multiply:
		return true;
	}

	return false;
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

//static void ResolveBinaryExpressionTypeDiscrepancy(Value left, Value right, BinaryExpression* binaryExpr)
//{
//	PROFILE_FUNCTION();
//
//	Type* lType = left.type;
//	Type* rType = right.type;
//
//	// If an operand is floating point, make other operand floating point too
//	bool hasFP = lType->IsFloatingPoint() || rType->IsFloatingPoint();
//	if (hasFP)
//	{
//		uint32_t nonFP = lType->IsFloatingPoint() ? 1 : 0;
//		uint32_t fp = 1 - nonFP;
//
//		Cast* cast = Cast::IsValid(exprs[nonFP].second, exprs[fp].second);
//		if (!cast) {
//			throw CompileError(binaryExpr->sourceLine, "cannot convert from '{}' to '{}'", rType->GetName(), lType->GetName());
//		}
//		exprs[nonFP].first = cast->Invoke(exprs[nonFP].first);
//		exprs[nonFP].second = exprs[fp].second;
//		return;
//	}
//
//	// If an operand is signed int, make other operand signed int too
//	bool hasSignedInt = lType->IsSigned() || rType->IsSigned();
//	if (hasSignedInt)
//	{
//		uint32_t nonSign = lType->IsSigned() ? 1 : 0;
//		uint32_t sign = 1 - nonSign;
//
//		Cast* cast = Cast::IsValid(exprs[nonSign].second, exprs[sign].second);
//		if (!cast) {
//			throw CompileError(binaryExpr->sourceLine, "cannot convert from '{}' to '{}'", rType->GetName(), lType->GetName());
//		}
//		exprs[nonSign].first = cast->Invoke(exprs[nonSign].first);
//		exprs[nonSign].second = exprs[sign].second;
//		return;
//	}
//}

Value BinaryExpression::Generate(Generator& generator)
{
	PROFILE_FUNCTION();
	generator.current_expression_source_line = sourceLine;

	if (binaryType == BinaryType::MemberAccess)
		return generator.EmitMemberAccessExpression(this);
	if (binaryType == BinaryType::Subscript)
		return generator.EmitSubscript(this);

	Value lhsv = left->Generate(generator);
	Value rhsv = right->Generate(generator);
	Value pointerLhs = lhsv;

	if (binaryType == BinaryType::Assign)
	{
		ASSERT(!lhsv.is_rvalue && "expected lvalue for lhs of binary assignment");
	}

	type = left->type;

	bool isPointerArithmetic = lhsv.type->IsPointer() && IsArithmetic(binaryType);

	RValue lhsrv, rhsrv;
	// Unless assiging, treat variables as the underlying values
	if (binaryType != BinaryType::Assign) {
		lhsrv = generator.MaterializeToRValue(lhsv);
	}
	rhsrv = generator.MaterializeToRValue(rhsv);

	if (left->type != right->type)
	{
		if (left->type->IsPointer() && IsArithmetic(binaryType)) {
			bool subtracting = binaryType == BinaryType::Subtract;
			llvm::Value* offset = !subtracting ? rhsrv.value : generator.llvm_context->builder->CreateNeg(rhsrv.value);

			//RValue& lv = lhsv.lvalue;
			//ASSERT(lhsv.is_rvalue);

			llvm::Value* result = generator.EmitInBoundsGEP(lhsrv.type->contained, lhsrv.value, { offset });
			if (isCompoundAssignment) {
				ASSERT(!lhsv.is_rvalue);
				generator.EmitStore(result, lhsv.lvalue.address.ptr);
			}

			return RValue{ result, lhsrv.type };
		}

		//ResolveBinaryExpressionTypeDiscrepancy({ lhs.raw, left->type }, { rhs.raw, right->type }, this);
		__debugbreak();
	}

	llvm::Instruction::BinaryOps instruction = (llvm::Instruction::BinaryOps)-1;
	switch (binaryType)
	{
	case BinaryType::Assign:
	{
		LValue& lhslv = lhsv.lvalue;
		generator.EmitStore(rhsrv.value, lhslv.address.ptr);
		return lhslv;
	}
	case BinaryType::Equal:
	case BinaryType::NotEqual:
	case BinaryType::Less:
	case BinaryType::LessEqual:
	case BinaryType::Greater:
	case BinaryType::GreaterEqual:
		return RValue{ generator.EmitComparisonOperator(LLVMCompareOpFromBinaryExpr(binaryType, left->type), lhsrv.value, rhsrv.value), left->type };
	default:
	{
		instruction = LLVMBinaryOpFromBinaryExpr(binaryType, left->type);
		break;
	}
	}
	
	llvm::Value* result = generator.EmitBinaryOperator(instruction, lhsrv.value, rhsrv.value, "bin.op");

	if (isCompoundAssignment) {
		LValue lhslv = lhsv.lvalue;
		generator.EmitStore(result, lhslv.address.ptr);
	}

	return RValue{ result, left->type };
}