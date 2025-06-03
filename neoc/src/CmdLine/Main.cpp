#include "pch.h"
#include <iostream>

#include "PlatformUtils.h"

#include "codegen/Generator.h"
#include "Tree.h"

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

//#define NEOC_DIST

int main(int argc, const char* argv[])
{
	PROFILE_BEGIN_SESSION("Profile", "ProfileResult.json");

	// If we are running from the cmd line:
#ifdef NEOC_DIST
	if (argc == 1)
	{
		fprintf(stderr, "Usage: neoc <path>\n");
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
#else
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

	auto startTime = std::chrono::system_clock::now();

	std::string contents = ReadFile(mainPath.c_str());
	
	Lexer lexer = Lexer(contents.c_str());
	Parser parser;

	ParseResult parseResult = parser.Parse(&lexer);

	if (parseResult.Succeeded)
	{
		Generator generator;
		auto result = generator.Generate(parseResult, arguments);
	
		double elapsedTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count();
		printf("compilation took %f seconds\n", elapsedTime);

		if (!result.Succeeded)
			return 0;
	
		std::string outputPath = mainPath.substr(0, mainPath.size() - 3) + "ll";
	
		Emitter emitter;
		emitter.Emit(result.ir, outputPath.c_str());
	
		//if (arguments.buildAndRun)
		{
			//std::string command = FormatString("neo {}", outputPath.c_str());
			std::string command = FormatString("lli {}", outputPath.c_str());
			LaunchProcess(command.c_str());
		}
	}

	//std::cin.get();

	PROFILE_END_SESSION();
}