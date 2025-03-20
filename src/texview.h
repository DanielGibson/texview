
#ifndef _TEXVIEW_H
#define _TEXVIEW_H

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

// TODO: could have an error printing function that also shows message through ImGui (if that is running)
#define errprintf(...) fprintf(stderr, __VA_ARGS__)

struct ktxTexture;

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

enum TextureFlags {
	TF_NONE         = 0,
	TF_SRGB         = 1,
	TF_TYPELESS     = 2,
	TF_PREMUL_ALPHA = 4,
	TF_COMPRESSED   = 8,
	TF_NOALPHA      = 16, // formats that use GL_RGBA or similar, but are RGBX (or similar)
};

struct Texture {

	enum FileType {
		FT_NONE = 0,
		FT_DDS,
		FT_KTX, // TODO: extra case for KTX2?
		FT_STB  // TODO: try to get actual type from stb_image
	};

	struct MipLevel {
		uint32_t width = 0;
		uint32_t height = 0;
		const void* data = nullptr; // owned by Texture
		uint32_t size = 0;

		MipLevel(uint32_t w, uint32_t h, const void* data_ = nullptr)
			: width(w), height(h), data(data_)
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
private:
	std::vector<MipLevel> mipLevels;
public:
	FileType fileType = FT_NONE;

	uint32_t textureFlags = 0; // or-ed TextureFlag constants

	// dataFormat is the textures OpenGL *internal* format.
	// For compressed textures it's something like GL_COMPRESSED_RGBA_BPTC_UNORM
	//  or GL_COMPRESSED_RGB_S3TC_DXT1_EXT
	// For uncompressed formats it's either an unsized internal format
	//  (GL_RED, GL_RG, GL_RGB, GL_RGBA, GL_DEPTH_STENCIL, GL_DEPTH_COMPONENT)
	//  or a sized internal format like GL_R8, GLR8_SNORM, GL_RGB8, etc
	//  see https://docs.gl/gl4/glTexImage2D#idp812160 (table 2)
	//  or GL_ALPHA or GL_LUMINANCE or GL_LUMINANCE_ALPHA (though those are deprecated)
	uint32_t dataFormat = 0;
	// the following two are only used for uncompressed texture formats.
	// glFormat is one of GL_RED, GL_RG, GL_RGB, GL_BGR, GL_RGBA or GL_BGRA,
	//  all the former with _INTEGER suffix or GL_STENCIL_INDEX, GL_DEPTH_COMPONENT, GL_DEPTH_STENCIL
	// or GL_ALPHA or GL_LUMINANCE or GL_LUMINANCE_ALPHA (though those are deprecated)
	uint32_t glFormat = 0;
	uint32_t glType = 0; // GL_UNSIGNED_BYTE, GL_BYTE, GL_UNSIGNED_SHORT, GL_FLOAT etc
	// the glTexImage2D() manpage doesn't mention it, but allegedly GL_HALF_FLOAT is also accepted:
	// https://community.khronos.org/t/loading-gl-rgba16f-to-glteximage2d/70296/4

	unsigned int glTextureHandle = 0;

	// texData is freed with texDataFreeFun
	// it's const because it should generally not be modified (might be read-only mmap)
	const void* texData = nullptr;
	intptr_t texDataFreeCookie = 0;
	TexDataFreeFun texDataFreeFun = nullptr;
	ktxTexture* ktxTex = nullptr;

	Texture() = default;

	Texture(const Texture& other) = delete; // if needed we'll need reference counting or similar for texData

	Texture(Texture&& other) : name(std::move(other.name)), formatName(other.formatName),
		mipLevels(std::move(other.mipLevels)), fileType(other.fileType),
		textureFlags(other.textureFlags), dataFormat(other.dataFormat),
		glFormat(other.glFormat), glType(other.glType), glTextureHandle(other.glTextureHandle),
		texData(other.texData), texDataFreeCookie(other.texDataFreeCookie),
		texDataFreeFun(other.texDataFreeFun), ktxTex(other.ktxTex)
	{
		other.texDataFreeFun = nullptr;
		other.glTextureHandle = 0;
		other.ktxTex = nullptr;
		other.Clear();
	}

	~Texture();

	Texture& operator=(Texture&& other) {
		Clear();
		name = std::move(other.name);
		formatName = other.formatName;
		other.formatName = nullptr;
		mipLevels = std::move(other.mipLevels);
		fileType = other.fileType;
		dataFormat = other.dataFormat;
		other.fileType = FT_NONE;
		other.dataFormat = 0;
		textureFlags = other.textureFlags;
		other.textureFlags = 0;
		glFormat = other.glFormat;
		glType = other.glType;
		other.glFormat = other.glType = 0;
		glTextureHandle = other.glTextureHandle;
		other.glTextureHandle = 0;
		texData = other.texData;
		other.texData = nullptr;
		texDataFreeCookie = other.texDataFreeCookie;
		other.texDataFreeCookie = 0;
		texDataFreeFun = other.texDataFreeFun;
		other.texDataFreeFun = nullptr;
		ktxTex = other.ktxTex;
		other.ktxTex = nullptr;

		return *this;
	}

	bool Load(const char* filename);

	bool CreateOpenGLtexture();

	void Clear();

	int GetNumMips() const {
		return int(mipLevels.size());
	}

	void GetSize(float* w, float* h) const {
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

	void GetMipSize(int mipLevel, float* w, float* h) const {
		float w_ = 0, h_ = 0;
		int numMips = GetNumMips();
		if(numMips > 0 && mipLevel >= 0 && mipLevel < numMips) {
			w_ = mipLevels[mipLevel].width;
			h_ = mipLevels[mipLevel].height;
		}
		if(w)
			*w = w_;
		if(h)
			*h = h_;
	}

private:
	bool LoadDDS(MemMappedFile* mmf, const char* filename);
	bool LoadKTX(MemMappedFile* mmf, const char* filename);
};

} //namespace texview

#endif // _TEXVIEW_H
