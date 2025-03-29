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

#include <stdio.h>
#include <string.h>

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
		errprintf("Can't load '%s', stat reports invalid size %ld!\n", filename, st.st_size);
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

} //namespace texview
