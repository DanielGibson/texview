
#ifndef _TEXVIEW_H
#define _TEXVIEW_H

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

// TODO: could have an error printing function that also shows message through ImGui (if that is running)
#define errprintf(...) fprintf(stderr, __VA_ARGS__)

namespace texview {

struct MemMappedFile {
	const void* data = nullptr;
	size_t length = 0;
#ifdef _WIN32
	// using void* instead of HANDLE to avoid dragging in windows.h
	// (HANDLE is just a void* anyway)
	void* fileHandle = (void*)(intptr_t)-1; // INVALID_HANDLE
	void* mappingObjectHandle = nullptr;
#else
	int fd = -1;
#endif
};

extern MemMappedFile* LoadMemMappedFile(const char* filename);

extern void UnloadMemMappedFile(MemMappedFile* mmf);

struct Texture {

	struct MipLevel {
		uint32_t width = 0;
		uint32_t height = 0;
		const void* data = nullptr; // owned by Texture
		uint32_t size = 0;

		MipLevel(uint32_t w, uint32_t h, const void* data_) : width(w), height(h), data(data_)
		{
			size = width * height * 4;
		}

		MipLevel(uint32_t w, uint32_t h, const void* data_, uint32_t size_)
			: width(w), height(h), data(data_), size(size_)
		{}
	};

	// function to free texData, set appropriately on load
	typedef void(*TexDataFreeFun)(void* texData, intptr_t texFreeCookie);

	std::string name;
	const char* formatName = nullptr;
	std::vector<MipLevel> mipLevels;
	uint32_t fileType = 0; // TODO: some custom enum
	uint32_t dataFormat = 0; // OpenGL internal format, like GL_COMPRESSED_RGBA_BPTC_UNORM
	bool formatIsCompressed = false;

	// texData is freed with texDataFreeFun
	// it's const because it should generally not be modified (might be read-only mmap)
	const void* texData = nullptr;
	intptr_t texDataFreeCookie = 0;
	TexDataFreeFun texDataFreeFun = nullptr;

	Texture() = default;

	Texture(const Texture& other) = delete; // if needed we'll need reference counting or similar for texData

	Texture(Texture&& other) : name(std::move(other.name)), formatName(other.formatName),
		mipLevels(std::move(other.mipLevels)), fileType(other.fileType),
		dataFormat(other.dataFormat), formatIsCompressed(other.formatIsCompressed),
		texData(other.texData), texDataFreeCookie(other.texDataFreeCookie),
		texDataFreeFun(other.texDataFreeFun)
	{
		other.Clear();
	}

	~Texture() {
		if(texDataFreeFun != nullptr) {
			texDataFreeFun( (void*)texData, texDataFreeCookie );
		}
	}

	Texture& operator=(Texture&& other) {
		Clear();
		name = std::move(other.name);
		formatName = other.formatName;
		other.formatName = nullptr;
		mipLevels = std::move(other.mipLevels);
		fileType = other.fileType;
		dataFormat = other.dataFormat;
		other.fileType = other.dataFormat = 0;
		formatIsCompressed = other.formatIsCompressed;
		other.formatIsCompressed = false,
		texData = other.texData;
		other.texData = nullptr;
		texDataFreeCookie = other.texDataFreeCookie;
		other.texDataFreeCookie = 0;
		texDataFreeFun = other.texDataFreeFun;
		other.texDataFreeFun = nullptr;

		return *this;
	}

	bool Load(const char* filename);

	void Clear();

	void GetSize(float* w, float* h){
		float w_ = 0, h_ = 0;
		if(mipLevels.size() > 0) {
			w_ = mipLevels[0].width;
			h_ = mipLevels[0].height;
		}
		if(w)
			*w = w_;
		if(h)
			*h = h_;
	}

private:
	bool LoadDDS(MemMappedFile* mmf, const char* filename);
};

} //namespace texview

#endif // _TEXVIEW_H
