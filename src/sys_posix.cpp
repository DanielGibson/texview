/*
 * Copyright (C) 2025 Daniel Gibson
 *
 * Released under MIT License, see Licenses.txt
 */
#include "texview.h"

#include <fcntl.h> // open()
#include <sys/stat.h>
#include <sys/mman.h> // mmap()
#include <unistd.h> // close()
#include <limits.h> // PATH_MAX

#include <stdio.h>
#include <string.h>

#ifdef __APPLE__
extern "C" const char* getAppSupportDirectory(const char* subdir);
#endif

namespace texview {

std::string ToAbsolutePath(const char* path)
{
	std::string ret;
	if(path[0] == '/') {
		// already absolute
		ret = path;
		return ret;
	}
#ifdef __APPLE__
	// according to their manpage, macOS is stuck in the 90s
	// and doesn't support realpath(path, NULL)
	// see https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/realpath.3.html
	// and https://github.com/fish-shell/fish-shell/issues/4433
	// (maybe they have fixed this since and just haven't documented it)
	char apBuf[PATH_MAX] = {0};
	const char* absPath = realpath(path, apBuf);
#else
	char* absPath = realpath(path, nullptr);
#endif
	if(absPath == nullptr) {
		errprintf("realpath(%s, NULL) failed?!\n", path);
		ret = path;
	} else {
		ret = absPath;
#ifndef __APPLE__
		free(absPath);
#endif
	}
	return ret;
}

MemMappedFile* LoadMemMappedFile(const char* filename)
{
	int fd = open(filename, O_RDONLY);
	if(fd == -1) {
		errprintf("Couldn't open '%s': %d - %s\n", filename, errno, strerror(errno));
		return nullptr;
	}
	struct stat st = {};
	int e = fstat(fd, &st);
	if(e < 0) {
		errprintf("Couldn't get size of '%s': %d - %s\n", filename, errno, strerror(errno));
		close(fd);
		return nullptr;
	}
	if(!S_ISREG(st.st_mode)) {
		errprintf("Can't load '%s', it's not a regular file! mode: 0x%x\n", filename, st.st_mode & S_IFMT);
		close(fd);
		return nullptr;
	}
	if(st.st_size <= 0) {
		errprintf("Can't load '%s', stat reports invalid size %ld!\n", filename, (long)st.st_size);
		close(fd);
		return nullptr;
	}

	void* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if(data == MAP_FAILED) {
		errprintf("Can't mmap() '%s': %d - %s\n", filename, errno, strerror(errno));
		close(fd);
		return nullptr;
	}

	MemMappedFile* ret = new MemMappedFile;

	ret->data = data;
	ret->length = st.st_size;
	ret->fd = fd;

	return ret;
}

void UnloadMemMappedFile(MemMappedFile* mmf)
{
	if(mmf->data != nullptr) {
		munmap((void*)mmf->data, mmf->length);
	}
	if(mmf->fd >= 0) {
		close(mmf->fd);
	}
	delete mmf;
}

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

bool CreatePathRecursive(char* path)
{
	bool dirExists = false;
	struct stat buf = {};
	if(stat(path, &buf) == 0) {
		dirExists = (buf.st_mode & S_IFMT) == S_IFDIR;
	}

	if(!dirExists) {
		char* lastDirSep = strrchr(path, '/');
		if(lastDirSep != NULL) {
			*lastDirSep = '\0'; // cut off last part of the path and try first with parent directory
			bool ok = CreatePathRecursive(path);
			*lastDirSep = '/'; // restore path
			// if parent dir was successfully created (or already existed), create this dir
			if(ok && mkdir(path, 0755) == 0) {
				return true;
			}
		}
		return false;
	}
	return true;
}

const char* GetSettingsDir()
{
	static char path[PATH_MAX] = {0};
	if(path[0] != '\0') {
		return path;
	}

#ifdef __APPLE__
	const char* sd = getAppSupportDirectory("texview");
	if(sd != nullptr) {
		size_t l = strlen(sd);
		if(l < PATH_MAX) {
			memcpy(path, sd, l+1);
		}
	}
#else
	const char* xdg_cfg = getenv("XDG_CONFIG_HOME");
	if(xdg_cfg != nullptr) {
		snprintf(path, sizeof(path), "%s/texview", xdg_cfg);
	} else {
		snprintf(path, sizeof(path), "%s/.config/texview", getenv("HOME"));
	}
#endif
	return path;
}

} //namespace texview
