#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

inline void Win32LaunchProcess(LPWSTR argv)
{
	// additional information
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	// set the size of the structures
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	// start the program up
	CreateProcess(NULL,   // the path
		argv,        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi             // Pointer to PROCESS_INFORMATION structure (removed extra parentheses)
	);

	// Close process and thread handles. 
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}
#endif

void LaunchProcess(const char* args)
{
#ifdef _WIN32
	size_t argLength = strlen(args) + 1;

	wchar_t* wArg = new wchar_t[argLength];
	mbstowcs(wArg, args, argLength);

	Win32LaunchProcess(wArg);

	delete[] wArg;
#endif
}

int main(int argc, const char* argv[])
{
#if NEO_RELEASE
	if (argc == 1)
	{
		fprintf(stderr, "Usage: neo <filepath>\n");
		return 1;
	}

	std::string input = argv[1];
#elif NEO_DEBUG
	std::string input = "main.ll";
#endif
	std::string processCommand = "lli " + input;

	LaunchProcess(processCommand.c_str());
}