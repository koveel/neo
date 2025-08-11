#include "pch.h"
#include <iostream>
#include <filesystem>

#include "PlatformUtils.h"

#include "codegen/Generator.h"
#include "Tree.h"

#include "Emitter.h"

static File ReadFile(const std::filesystem::path& filepath)
{
	std::string fileContents;

	// Read file
	std::ifstream stream(filepath);

	if (!stream.good())
	{
		fprintf(stderr, "failed to open file \"%s\".\n", filepath.c_str());
		return {};
	}

	stream.seekg(0, std::ios::end);
	fileContents.reserve((size_t)stream.tellg() + 1); // Room for null-terminating character
	stream.seekg(0, std::ios::beg);

	fileContents.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());

	stream.close();

	return { filepath.filename().string(), fileContents };
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
	const char* argV[] = { "neoc", "C:/dev/neo/neoc/main.neo", "-br" };
	uint32_t argC = std::size(argV);

	std::string cmdParseError;
	CommandLineArguments arguments = CommandLineArguments::FromCommandLine(argC, argV, cmdParseError);
	if (!cmdParseError.empty())
	{
		fprintf(stderr, "%s\n", cmdParseError.c_str());
		return 1;
	}

	//std::string mainPath = arguments.path;
	std::filesystem::path mainPath = arguments.path;
#endif
	if (mainPath.extension() != ".neo")
	{
		fprintf(stderr, "expected a .neo file\n");
		return 1;
	}

	auto startTime = std::chrono::system_clock::now();

	File file = ReadFile(mainPath.c_str());

	Lexer lexer = Lexer(file);
	Parser parser;

	ParseResult parseResult = parser.Parse(&lexer);

	if (parseResult.Succeeded)
	{
		Generator generator(parseResult.module);
		auto result = generator.Generate(parseResult, arguments);
	
		double elapsedTime = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count();
		printf("compilation took %f seconds\n", elapsedTime);

		if (!result.Succeeded)
			return 0;
	
		std::filesystem::path outputPath = mainPath.replace_extension(".ll");
	
		Emitter emitter;
		emitter.Emit(result.ir, outputPath);
	
		//if (arguments.buildAndRun)
		{
			//std::string command = FormatString("neo {}", outputPath.c_str());
			std::string command = FormatString("lli {}", outputPath.string().c_str());
			LaunchProcess(command.c_str());
		}
	}

	PROFILE_END_SESSION();
}