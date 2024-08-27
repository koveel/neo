#include "pch.h"

#include "PlatformUtils.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

inline void Win32LaunchProcess(LPWSTR argv)
{
	PROFILE_FUNCTION();

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
	PROFILE_FUNCTION();

#ifdef _WIN32
	size_t argLength = strlen(args) + 1;
	
	wchar_t* wArg = new wchar_t[argLength];
	mbstowcs(wArg, args, argLength);

	Win32LaunchProcess(wArg);

	delete[] wArg;
#endif
}

void SetConsoleColor(int c)
{
	SetConsoleTextAttribute(hConsole, c);
}

void ResetConsoleColor()
{
	SetConsoleTextAttribute(hConsole, 15);
}
