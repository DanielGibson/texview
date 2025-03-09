
#include "windows.h"

#include "texview.h"

namespace texview {

MemMappedFile* LoadMemMappedFile(const char* filename)
{
	// TODO:
	//- convert filename from UTF-8 to wchar
	//- CreateFile() to open file
	//- CreateFileMapping() to create file mapping object(?!)
	//- MapViewOfFile() for actually mapping it


	return nullptr;
}

void UnloadMemMappedFile(MemMappedFile* mmf)
{
	// TODO
	//- UnmapViewOfFile() to unmap view
	//- CloseHandle() to close file mapping object
	//- CloseHandle() to close the file
}

} //namespace texview

// For WinMain() I stole some code from SDL_main/SDL_RunApp() to convert
// argv[] to UTF-8 and then call a C-like main with that (my_main() from
// main.cpp in this case)
// (adjusted it a bit to work without SDL, of course)

/*
Simple DirectMedia Layer
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

extern int my_main(int argc, char** argv);

/* Pop up an out of memory message, returns to Windows */
static BOOL OutOfMemory(void)
{
	MessageBoxA(NULL, "Out of memory - aborting", "Fatal Error", MB_ICONERROR);
	return -1;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
	/* Gets the arguments with GetCommandLine, converts them to argc and argv
	and calls SDL_main */

	LPWSTR *argvw;
	char **argv;
	int i, argc, result;

	argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argvw) {
		return OutOfMemory();
	}

	// Parse it into argv and argc
	argv = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (argc + 1) * sizeof(*argv));
	if (!argv) {
		return OutOfMemory();
	}
	for (i = 0; i < argc; ++i) {
		const int utf8size = WideCharToMultiByte(CP_UTF8, 0, argvw[i], -1, NULL, 0, NULL, NULL);
		if (!utf8size) {  // uhoh?
			MessageBoxA(NULL, "Error processing command line arguments", "Fatal Error", MB_ICONERROR);
			return -1;
		}

		argv[i] = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, utf8size);  // this size includes the null-terminator character.
		if (!argv[i]) {
			return OutOfMemory();
		}

		if (WideCharToMultiByte(CP_UTF8, 0, argvw[i], -1, argv[i], utf8size, NULL, NULL) == 0) {  // failed? uhoh!
			MessageBoxA(NULL, "Error converting command line arguments", "Fatal Error", MB_ICONERROR);
			return -1;
		}
	}
	argv[i] = NULL;
	LocalFree(argvw);

	// Run the application main() code
	result = my_main(argc, argv);

	// Free argv, to avoid memory leak
	for (i = 0; i < argc; ++i) {
		HeapFree(GetProcessHeap(), 0, argv[i]);
	}
	HeapFree(GetProcessHeap(), 0, argv);

	return result;
}
