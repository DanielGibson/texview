/*
 * Copyright (C) 2025 Daniel Gibson
 *
 * Released under MIT License, see Licenses.txt
 */

#ifndef _TEXVIEW_H
#define _TEXVIEW_H

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#ifdef _MSC_VER
	#include <intrin.h>
#endif

// TODO: could have an error printing function that also shows message through ImGui (if that is running)
#define errprintf(...) fprintf(stderr, __VA_ARGS__)

struct ktxTexture;

namespace texview {

inline int NumBitsSet(uint32_t x) {
#ifdef _MSC_VER
	return __popcnt(x);
#elif defined __GNUC__
	return __builtin_popcount(x);
#else
	#error "unknown/unsupported compiler, adjust this function"
#endif
}

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

extern std::string ToAbsolutePath(const char* path);

extern MemMappedFile* LoadMemMappedFile(const char* filename);

extern void UnloadMemMappedFile(MemMappedFile* mmf);

enum TextureFlags : uint32_t {
	TF_NONE         = 0,
	TF_SRGB         = 1,
	TF_TYPELESS     = 2,
	TF_HAS_ALPHA    = 4, // texture has an alpha channel that might be used (e.g. RGBA, not RGBX)
	TF_PREMUL_ALPHA = 8,
	TF_COMPRESSED   = 1 << 4,

	_TF_NOALPHA     = 1 << 7, // formats that use GL_RGBA or similar, but are RGBX (or similar) - just for the format tables!

	TF_IS_ARRAY     = 1 << 8,

	// at least DDS allows cubemaps with missing faces...
	// so here's a separate flag for every cubemap face
	TF_CUBEMAP_XPOS = 1u << 26,
	TF_CUBEMAP_XNEG = 1u << 27,
	TF_CUBEMAP_YPOS = 1u << 28,
	TF_CUBEMAP_YNEG = 1u << 29,
	TF_CUBEMAP_ZPOS = 1u << 30,
	TF_CUBEMAP_ZNEG = 1u << 31,

	TF_CUBEMAP_MASK = TF_CUBEMAP_XPOS | TF_CUBEMAP_XNEG
	                  | TF_CUBEMAP_YPOS | TF_CUBEMAP_YNEG
	                  | TF_CUBEMAP_ZPOS | TF_CUBEMAP_ZNEG,
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
	std::string formatName;
private:
	// elements of texture array or cubemap. if it's just one texture, it's just one element.
	// if it's a cubemap, it's the (up to) 6 images of the cubemap (according to textureFlags)
	// if it's an array of N cubemaps, there are (up to) 6 * N elements.
	std::vector<std::vector<MipLevel> > elements;
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

	uint32_t glTarget = 0; // GL_TEXTURE_2D, GL_CUBE_MAP, array variations of that

	unsigned int glTextureHandle = 0;

	// texData is freed with texDataFreeFun
	// it's const because it should generally not be modified (might be read-only mmap)
	const void* texData = nullptr;
	intptr_t texDataFreeCookie = 0;
	TexDataFreeFun texDataFreeFun = nullptr;
	ktxTexture* ktxTex = nullptr;

	Texture() = default;

	Texture(const Texture& other) = delete; // if needed we'll need reference counting or similar for texData

	Texture(Texture&& other) : name(std::move(other.name)),
		formatName(std::move(other.formatName)),
		elements(std::move(other.elements)), fileType(other.fileType),
		textureFlags(other.textureFlags), dataFormat(other.dataFormat),
		glFormat(other.glFormat), glType(other.glType), glTarget(other.glTarget),
		glTextureHandle(other.glTextureHandle),
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
		formatName = std::move(other.formatName);
		elements = std::move(other.elements);
		fileType = other.fileType;
		dataFormat = other.dataFormat;
		other.fileType = FT_NONE;
		other.dataFormat = 0;
		textureFlags = other.textureFlags;
		other.textureFlags = 0;
		glFormat = other.glFormat;
		glType = other.glType;
		glTarget = other.glTarget;
		other.glFormat = other.glType = other.glTarget = 0;
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
		return elements.empty() ? 0 : int(elements[0].size());
	}

	// number of texture array elements (1 if it's just a regular texture)
	// if this is a cubemap texture (array), one cubemap counts as one
	// even if internally it's saved as GetNumElements() * GetNumCubemapFaces() elements
	int GetNumElements() const {
		int ret = int(elements.size());
		if(IsCubemap()) {
			ret /= GetNumCubemapFaces();
		}
		return ret;
	}

	int GetNumCubemapFaces() const {
		return NumBitsSet(textureFlags & TF_CUBEMAP_MASK);
	}

	bool IsCubemap() const {
		return (textureFlags & TF_CUBEMAP_MASK) != 0;
	}

	bool IsArray() const {
		return (textureFlags & TF_IS_ARRAY) != 0;
	}

	void GetSize(float* w, float* h) const {
		float w_ = 0, h_ = 0;
		if(!elements.empty() && elements[0].size() > 0) {
			w_ = elements[0][0].width;
			h_ = elements[0][0].height;
		}
		if(w)
			*w = w_;
		if(h)
			*h = h_;
	}

	void GetMipSize(int mipLevel, float* w, float* h) const {
		float w_ = 0, h_ = 0;
		int numMips = GetNumMips();
		if(!elements.empty() && numMips > 0 && mipLevel >= 0 && mipLevel < numMips) {
			// all elements in a texture array have the same sizes
			w_ = elements[0][mipLevel].width;
			h_ = elements[0][mipLevel].height;
		}
		if(w)
			*w = w_;
		if(h)
			*h = h_;
	}

	// returns NULL if not an _INTEGER texture
	// otherwise it returns a string with the divisor to normalize the components in GLSL
	const char* GetIntTexInfo(bool& isUnsigned);

private:
	bool LoadDDS(MemMappedFile* mmf, const char* filename);
	bool LoadKTX(MemMappedFile* mmf, const char* filename);

	bool UploadTexture2D(uint32_t target, int internalFormat, int level, bool isCompressed, const Texture::MipLevel& mipLevel);
};

} //namespace texview

#endif // _TEXVIEW_H
