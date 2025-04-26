/*
 * Copyright (C) 2025 Daniel Gibson
 *
 * Released under MIT License, see Licenses.txt
 *
 * This also includes some code from SDL (https://libsdl.org) at
 * the end of the file (the WinMain() implementation) that is under
 * zlib license, see below and/or Licenses.txt
 */

#include <windows.h>
#include <Shlobj.h>

#include "texview.h"

namespace texview {

// remember to free() the returned buffer!
static wchar_t* Utf8ToUtf16(const char* str)
{
	int wLen = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
	if (wLen <= 0) {
		errprintf("Can't convert '%s' to wchar - maybe invalid UTF-8? GetLastError(): %d\n", str, GetLastError());
		return nullptr;
	}
	wchar_t* wStr = (wchar_t*)malloc(wLen * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, str, -1, wStr, wLen);
	return wStr;
}

// remember to free() the returned buffer!
static char* Utf16ToUtf8(const wchar_t* wstr)
{
	int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	if (utf8Len <= 0) {
		errprintf("Error in Utf16ToUtf8()! GetLastError(): %d\n", GetLastError());
		return nullptr;
	}
	char* ret = (char*)malloc(utf8Len);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, ret, utf8Len, NULL, NULL);
	return ret;
}

std::string ToAbsolutePath(const char* path)
{
	std::string ret;
	// on windows absolute paths start with C:\ (or D:\ or whatever)
	// or with \\bla for network shares
	if ( path[1] == ':' || (path[0] == '\\' && path[1] == '\\') ) {
		// already absolute => just copy and return
		ret = path;
		return ret;
	}
	// all those allocations are ugly, but this isn't called much so who cares
	wchar_t* pathW = Utf8ToUtf16(path);
	// last argument should be ignored (because first is NULL), but
	// the docs also say that 0 is an invalid value, so I pass 1...
	wchar_t* absPathW = _wfullpath(nullptr, pathW, 1);
	if ( absPathW == nullptr ) {
		errprintf("ToAbsolutePath(%s): _wfullpath() failed! errno: %d\n", path, errno);
		ret = path;
		free(pathW);
		return ret;
	}
	free(pathW);
	pathW = nullptr;
	char* absPath = Utf16ToUtf8(absPathW);
	free(absPathW);
	ret = absPath;
	free(absPath);

	return ret;
}


MemMappedFile* LoadMemMappedFile(const char* filename)
{
	HANDLE fileHandle = INVALID_HANDLE_VALUE;
	// convert filename to WCHAR and try to open the file
	{
		WCHAR* wFilename = Utf8ToUtf16(filename);
		fileHandle = CreateFileW(wFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
		if (fileHandle == INVALID_HANDLE_VALUE) {
			errprintf("Couldn't open '%s'! GetLastError(): %d\n", filename, GetLastError());
			free(wFilename);
			return nullptr;
		}
		free(wFilename);
	}

	LARGE_INTEGER size = { 0 };
	if (!GetFileSizeEx(fileHandle, &size)) {
		errprintf("Couldn't get size of file '%s'!\n", filename);
		CloseHandle(fileHandle);
		return nullptr;
	}
	if (size.QuadPart > MAXSIZE_T) {
		errprintf("File '%s' is too long for size_t!\n", filename);
		CloseHandle(fileHandle);
		return nullptr;
	}

	// create file mapping object
	HANDLE fileMapping = CreateFileMappingW(fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
	if (fileMapping == NULL) {
		errprintf("Couldn't create file mapping for '%s'! GetLastError(): %d\n", filename, GetLastError());
		CloseHandle(fileHandle);
		return nullptr;
	}

	// map view to get pointer to data, finally
	void* data = MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);
	if (data == NULL) {
		errprintf("Couldn't map a view of file '%s'! GetLastError(): %d\n", filename, GetLastError());
		CloseHandle(fileMapping);
		CloseHandle(fileHandle);
		return nullptr;
	}

	MemMappedFile* ret = new MemMappedFile;
	ret->data = data;
	ret->length = size.QuadPart;
	ret->fileHandle = fileHandle;
	ret->mappingObjectHandle = fileMapping;

	return ret;
}

void UnloadMemMappedFile(MemMappedFile* mmf)
{
	if (mmf != nullptr) {
		HANDLE fh = (HANDLE)mmf->fileHandle;
		HANDLE moh = (HANDLE)mmf->mappingObjectHandle;
		if (mmf->data != nullptr) {
			UnmapViewOfFile(mmf->data);
		}
		if (moh != NULL) {
			CloseHandle(moh);
		}
		if (fh != INVALID_HANDLE_VALUE) {
			CloseHandle(fh);
		}
		delete mmf;
	}
}

// returns something like C:\Users\Horst\AppData\Roaming\texview (in UTF-8)
const char* GetSettingsDir()
{
	static char settingsDir[1024] = {0}; // much more than MAX_PATH (260), but UTF8 may need more chars..
	if (settingsDir[0] != '\0')
		return settingsDir;

	WCHAR appdataPath[MAX_PATH] = { 0 };
	// get path of users AppData/ dir (e.g. C:\Users\Horst\AppData\Roaming)
	// TODO: Roaming or Local? Local should be CSIDL_LOCAL_APPDATA
	HRESULT res = SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE,
	                               nullptr, SHGFP_TYPE_CURRENT, appdataPath);
	if (res != S_OK) {
		LogError("Couldn't get your AppData directory - will save settings in the current directory!\n");
		// TODO: could get more info with FormatMessage()
		settingsDir[0] = '.';
		return settingsDir;
	}

	// convert to UTF-8, because we (and ImGui) use UTF-8 everywhere
	char* adpu8 = Utf16ToUtf8(appdataPath);
	snprintf(settingsDir, sizeof(settingsDir), "%s\\texview", adpu8);
	free(adpu8);

	return settingsDir;
}

static WCHAR* findLastDirSep(WCHAR* str)
{
	WCHAR* lastBS = wcsrchr(str, L'\\');
	WCHAR* lastSlash = wcsrchr(str, L'/');
	if (lastBS == nullptr) {
		return lastSlash;
	}
	return (lastSlash == nullptr || lastSlash < lastBS) ? lastBS : lastSlash;
}

static bool CreatePathRecursiveW(WCHAR* path)
{
	bool dirExists = false;
	struct _stat buf = {};
	if (_wstat(path, &buf) == 0) {
		dirExists = (buf.st_mode & S_IFMT) == S_IFDIR;
	}
	if (!dirExists) {
		WCHAR* lastDirSep = findLastDirSep(path);
		if (lastDirSep != nullptr) {
			WCHAR dsbk = *lastDirSep;
			*lastDirSep = 0; // cut off last part of the path and try first with parent directory
			bool ok = CreatePathRecursiveW(path);
			*lastDirSep = dsbk; // restore path
			if (ok && CreateDirectoryW(path, NULL)) {
				return true;
			}
		}
		return false;
	}
	return true;
}

bool CreatePathRecursive(char* path)
{
	WCHAR* pathW = Utf8ToUtf16(path);
	bool ret = CreatePathRecursiveW(pathW);
	free(pathW);
	return ret;
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
