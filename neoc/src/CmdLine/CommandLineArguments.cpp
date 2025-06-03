#include "pch.h"
#include "CommandLineArguments.h"

static uint32_t ParseInt(const char* i)
{
	try
	{
		return std::stoi(i);
	}
	catch (const std::exception& except)
	{
		return -1; // Arguments won't really need to be negative, so returning -1 is fine
	}
}

static float ParseFloat(const char* f)
{
	return std::stof(f);
}

CommandLineArguments CommandLineArguments::FromCommandLine(uint32_t argCount, const char* rawArgs[], std::string& error)
{
	CommandLineArguments argResults;

	// Start at first arg
	uint32_t argIndex = 1;
	argResults.path = rawArgs[argIndex++];

#define ERROR_AND_RET(msg, ...)\
	error = FormatString(msg, __VA_ARGS__); \
	return argResults;

	while (argIndex < argCount)
	{
		// Parse arg
		const char* arg = rawArgs[argIndex];
		const char* index = &arg[0];
		
		uint32_t length = strlen(arg);
		if (arg[0] != '-' || length < 2)
		{
			ERROR_AND_RET("unable to parse compiler args: expected a valid argument at arg index {}", argIndex - 1);
		}
		switch (*++index)
		{
			case 'O':
			{
				if (length > 2)
				{
					if (*++index == '=')
					{
						// No spaces in arg
						uint32_t value = ParseInt(++index);
						if (value == -1)
						{
							ERROR_AND_RET("unable to parse argument value: expected a non-negative integer for '-O' (got -1)");
						}
						if (value > 3)
						{
							ERROR_AND_RET("invalid argument value: expected either 0, 1, 2, or 3 for '-O' (got {})", value);
						}

						argResults.optimizationLevel = value;
						argIndex++;

						break;
					}
					else
					{
						ERROR_AND_RET("unable to parse compiler args: expected '=' after '-O', got '{}'", *index);
					}
				}
				ERROR_AND_RET("unable to parse compiler args: expected '=' after '-O'");
			}
			case 'b':
			{
				if (*++index == 'r')
				{
					argResults.buildAndRun = true;
					argIndex++;
					break;
				}

				ERROR_AND_RET("unable to parse compiler args: expected a valid argument at arg index {}. got '{}'", argIndex - 1, arg);
			}
			default:
			{
				error = FormatString("unable to parse compiler args: invalid argument '{}'", arg);
			}
		}
	}

#undef ERROR_AND_RET

	return argResults;
}