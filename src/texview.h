
#ifndef _TEXVIEW_H
#define _TEXVIEW_H

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace texview {

struct Texture {

	struct MipLevel {
		uint32_t width = 0;
		uint32_t height = 0;
		const void* data = nullptr; // owned by Texture

		MipLevel(uint32_t w, uint32_t h, const void* data_) : width(w), height(h), data(data_)
		{}
	};

	// function to free texData, set appropriately on load
	typedef void(*TexDataFreeFun)(void* texData, intptr_t texFreeCookie);

	std::string name;
	std::vector<MipLevel> mipLevels;
	uint32_t fileType = 0; // TODO: some custom enum
	uint32_t dataType = 0; // TODO: what to use here? custom enum or sth like GL_COMPRESSED_RGBA_BPTC_UNORM ?

	// texData is freed with texDataFreeFun
	// it's const because it should generally not be modified (might be read-only mmap)
	const void* texData = nullptr;
	intptr_t texDataFreeCookie = 0;
	TexDataFreeFun texDataFreeFun = nullptr;

	Texture() = default;
	Texture(const Texture& other) = delete; // if needed we'll need reference counting or similar for texData
	Texture(Texture&& other) : name(std::move(other.name)), mipLevels(std::move(other.mipLevels)),
		fileType(other.fileType), dataType(other.dataType), texData(other.texData),
		texDataFreeCookie(other.texDataFreeCookie), texDataFreeFun(other.texDataFreeFun)
	{
		other.fileType = other.dataType = 0;
		other.texData = nullptr;
		other.texDataFreeCookie = 0;
		other.texDataFreeFun = nullptr;
	}

	~Texture() {
		if(texDataFreeFun != nullptr) {
			texDataFreeFun( (void*)texData, texDataFreeCookie );
		}
	}

	bool Load(const char* filename);

	void Clear();
};

} //namespace texview

#endif // _TEXVIEW_H
