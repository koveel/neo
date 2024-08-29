#include "pch.h"
#include <iostream>

#include "PlatformUtils.h"

#include "Tree.h"

#include "Generator.h"
#include "Emitter.h"

static std::string ReadFile(const char* filepath)
{
	std::string fileContents;

	// Read file
	std::ifstream stream(filepath);

	if (!stream.good())
	{
		fprintf(stderr, "failed to open file \"%s\".\n", filepath);
		return fileContents;
	}

	stream.seekg(0, std::ios::end);
	fileContents.reserve((size_t)stream.tellg() + 1); // Room for null-terminating character
	stream.seekg(0, std::ios::beg);

	fileContents.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());

	stream.close();

	return fileContents;
}

static void DebugPrintNode(ASTNode* baseNode)
{
	static uint32_t indentation = 0;

	for (uint32_t i = 0; i < indentation; i++)
		printf("\t");

// Sometimes one can find comfort in the preprocessor chaos
#define DYN_CAST_TO(T) auto* node = dynamic_cast<T*>(baseNode)

	switch (baseNode->nodeType)
	{
		case NodeType::PrimaryValue:
		{
			const char* formatStringsForType[] =
			{
				"%d",
				"%d",
				"%d",
				"%d",
				"%f",
				"%f",
				"%d",
			};

			DYN_CAST_TO(PrimaryExpression);

			char line[100];
			sprintf(line, "[Expr]: type = %s, value = %s\n", node->type->GetName().c_str(), formatStringsForType[(size_t)node->type->tag]);

			printf(line, node->value.i64);

			break;
		}
		case NodeType::StringValue:
		{
			DYN_CAST_TO(StringExpression);
			printf("[Expr]: type = string, value = \"%.*s\"\n", node->value.length, node->value.start);

			break;
		}
		case NodeType::VariableDefine:
		{
			DYN_CAST_TO(VariableDefinitionStatement);

			if (node->type)
				printf("[VariableDef %s]: \"%.*s\" isGlobal = %d, isConst = %d\n", node->type->GetName().c_str(), node->Name.length, node->Name.start, node->modifiers.isGlobal, node->modifiers.isConst);
			else
				printf("[VariableDef]: \"%.*s\" isGlobal = %d, isConst = %d\n", node->Name.length, node->Name.start, node->modifiers.isGlobal, node->modifiers.isConst);
			if (node->initializer)
			{
				indentation++;
				DebugPrintNode(node->initializer.get());
				indentation--;
			}
			 
			break;
		}
		case NodeType::Compound:
		{
			DYN_CAST_TO(CompoundStatement);

			SetConsoleColor(15);
			printf("[Scope]:----------------\n");
			SetConsoleColor(7);

			indentation++;
			for (auto& node : node->children)
				DebugPrintNode(node.get());

			indentation--;
			break;
		}
		case NodeType::VariableAccess:
		{
			DYN_CAST_TO(VariableAccessExpression);
			printf("[VarAccess]: name = \"%s\"\n", node->name.c_str());

			break;
		}
		case NodeType::UnaryExpr:
		{
			DYN_CAST_TO(UnaryExpression);
			printf("[Unary %.*s]:\n", node->operatorToken.length, node->operatorToken.start);

			indentation++;
			DebugPrintNode(node->operand.get());
			indentation--;

			break;
		}
		case NodeType::BinaryExpr:
		{
			DYN_CAST_TO(BinaryExpression);
			printf("[Binary %.*s]:\n", node->operatorToken.length, node->operatorToken.start);
			indentation++; 
			
			DebugPrintNode(node->left.get());
			DebugPrintNode(node->right.get());
			
			indentation--;

			break;
		}
		case NodeType::BranchExpr:
		{
			DYN_CAST_TO(BranchExpression);

			printf("[Branch]: \n");
			indentation++;
			
			if (node->condition)
				DebugPrintNode(node->condition.get());
			DebugPrintNode(node->body.get());
			indentation--;

			if (node->elseBranch)
				DebugPrintNode(node->elseBranch.get());

			break;
		}
		case NodeType::FunctionDefinition:
		{
			DYN_CAST_TO(FunctionDefinitionExpression);

			printf("[Function '%s']: param_count = %d, return_type = %s\n", node->prototype.Name.c_str(), (int)node->prototype.Parameters.size(), node->prototype.ReturnType->GetName().c_str());
			indentation++;
			for (auto& param : node->prototype.Parameters)
				DebugPrintNode(param.get());

			for (auto& sub : node->body)
				DebugPrintNode(sub.get());

			indentation--;

			break;
		}
		case NodeType::FunctionCall:
		{
			DYN_CAST_TO(FunctionCallExpression);

			printf("[Call '%s']: arg_count = %d\n", node->name.c_str(), (int)node->arguments.size());
			indentation++;
			for (auto& arg : node->arguments)
				DebugPrintNode(arg.get());
			indentation--;

			break;
		}
		case NodeType::StructDefinition:
		{
			DYN_CAST_TO(StructDefinitionExpression);

			printf("[Struct '%s']:\n", node->name.c_str());
			indentation++;

			for (auto& member : node->members)
				DebugPrintNode(member.get());

			indentation--;
			break;
		}
	}

#undef DYN_CAST_TO
}

static void DebugPrintTree(const std::unique_ptr<CompoundStatement>& module)
{
	if (!module)
		return;

	printf("Module:\n\n");
	for (auto& node : module->children)
	{		
		DebugPrintNode(node.get());
	}
}


int main(int argc, const char* argv[])
{
	PROFILE_BEGIN_SESSION("Profile", "ProfileResult.json");

	// If we are running from the cmd line:
#ifdef NEOC_RELEASE
	if (argc == 1)
	{
		fprintf(stderr, "Usage: limec <path>\n");
		return 1;
	}

	std::string cmdParseError;
	CommandLineArguments arguments = CommandLineArguments::FromCommandLine(argc, argv, cmdParseError);
	if (!cmdParseError.empty())
	{
		fprintf(stderr, "%s\n", cmdParseError.c_str());
		return 1;
	}
	
	std::string mainPath = arguments.path;
#elif NEOC_DEBUG
	const char* argV[] = { "neoc", "C:/Users/edwar/Desktop/syntax_test/main.neo", "-br" };
	uint32_t argC = std::size(argV);

	std::string cmdParseError;
	CommandLineArguments arguments = CommandLineArguments::FromCommandLine(argC, argV, cmdParseError);
	if (!cmdParseError.empty())
	{
		fprintf(stderr, "%s\n", cmdParseError.c_str());
		return 1;
	}

	std::string mainPath = arguments.path;
#endif
	if (mainPath.substr(mainPath.length() - strlen("neo")) != "neo")
	{
		fprintf(stderr, "expected a .neo file\n");
		return 1;
	}
	std::string contents = ReadFile(mainPath.c_str());
	
	Lexer lexer = Lexer(contents.c_str());
	Parser parser;

	ParseResult parseResult = parser.Parse(&lexer);

	//DebugPrintTree(parseResult.Module);

	if (parseResult.Succeeded)
	{
		Generator generator;
		auto result = generator.Generate(parseResult, arguments);
	
		if (!result.Succeeded)
			return 0;
	
		std::string outputPath = mainPath.substr(0, mainPath.size() - 3) + "ll";
	
		Emitter emitter;
		emitter.Emit(result.ir, outputPath.c_str());
	
		//if (arguments.buildAndRun)
		{
			//std::string command = FormatString("neo %s", outputPath.c_str());
			std::string command = FormatString("lli %s", outputPath.c_str());
			LaunchProcess(command.c_str());
		}
	}

	//std::cin.get();

	PROFILE_END_SESSION();
}