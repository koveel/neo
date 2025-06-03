#pragma once

template<typename Expr>
static Expr* ToExpr(Expression* expr) { return expr->nodeType != Expr::GetNodeType() ? nullptr : static_cast<Expr*>(expr); }
template<typename Expr>
static Expr* ToExpr(std::unique_ptr<Expression>& expr) { return ToExpr<Expr>(expr.get()); }
template<typename Expr>
static Expr* ToExpr(std::shared_ptr<Expression>& expr) { return ToExpr<Expr>(expr.get()); }

