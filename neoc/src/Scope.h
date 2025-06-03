#pragma once

struct Value
{
	Type* type;
	llvm::Value* raw;
};

class Scope
{
public:
	Scope() = default;
	Scope(Scope* parent)
		: parentScope(parent) {}

	void AddValue(const std::string& name, const Value& value);
	bool HasValue(const std::string& name, Value* out, bool checkParentScopes = true) const;

	// High iq stuff going on
	Scope* Deepen() 
	{ 
		// TODO: fix this obviously
		Scope* scope = new Scope(this);
		scope->depth = depth + 1;
		return scope;
	} 
	Scope* Increase() const { return parentScope; }
public:
	Scope* parentScope = nullptr;
private:
	// Types
	// Functions
	// Structures
	// Variables

	uint32_t depth = 0;
	std::unordered_map<std::string, Value> values;
};