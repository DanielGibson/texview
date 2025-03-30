/*
 * Copyright (C) 2025 Daniel Gibson
 *
 * Released under MIT License, see Licenses.txt
 */

#define STBI_NO_STDIO
#include "libs/stb_image.h"
#include <glad/gl.h>

#include "libs/dg_libktx_extra.h"

#include <ktx.h>

#include "texview.h"

#include "dds_defs.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
	#define strcasecmp _stricmp
#endif

namespace texview {

void Texture::Clear()
{
	formatName.clear();
	elements.clear();
	if(glTextureHandle > 0) {
		glDeleteTextures(1, &glTextureHandle);
		glTextureHandle = 0;
	}
	if(texDataFreeFun != nullptr) {
		texDataFreeFun( (void*)texData, texDataFreeCookie );
		texDataFreeFun = nullptr;
		texDataFreeCookie = 0;
	}
	glFormat = glType = glTarget = 0;
	texData = nullptr;

	name.clear();
	fileType = FT_NONE;
	textureFlags = 0;
	dataFormat = 0;
}

static const char* getGLerrorString(GLenum e)
{
	const char* ret = "unknown enum";
	switch(e) {
	#define MY_CASE(X) case X: ret = #X; break;

		MY_CASE( GL_NO_ERROR )
		MY_CASE( GL_INVALID_ENUM )
		MY_CASE( GL_INVALID_VALUE )
		MY_CASE( GL_INVALID_OPERATION )
		MY_CASE( GL_INVALID_FRAMEBUFFER_OPERATION )
		MY_CASE( GL_OUT_OF_MEMORY )
		MY_CASE( GL_STACK_UNDERFLOW )
		MY_CASE( GL_STACK_OVERFLOW )

	#undef MY_CASE
	}

	return ret;
}

bool Texture::UploadTexture2D(uint32_t target, int internalFormat, int level,
                              bool isCompressed, const Texture::MipLevel& mipLevel)
{
	bool anySuccess = false;
	if(isCompressed) {
		glCompressedTexImage2D(target, level, internalFormat,
							   mipLevel.width, mipLevel.height,
							   0, mipLevel.size, mipLevel.data);
		GLenum e = glGetError();
		if(e != GL_NO_ERROR) {
			errprintf("Sending data from '%s' for mipmap level %d to the GPU with glCompressedTexImage2D() failed. "
					  "Probably your GPU/driver doesn't support '%s' compression (glGetError() says '%s')\n",
					  name.c_str(), level, formatName.c_str(), getGLerrorString(e));
		} else { // probably better than nothing if at least *some* mipmap level has been loaded
			anySuccess = true;
		}
	} else {
		glTexImage2D(target, level, internalFormat, mipLevel.width,
					 mipLevel.height, 0, glFormat, glType,
					 mipLevel.data);
		GLenum e = glGetError();
		if(e != GL_NO_ERROR) {
			errprintf("Sending data from '%s' for mipmap level %d to the GPU with glTexImage2D() failed. "
					  "glGetError() says '%s'\n", name.c_str(), level, getGLerrorString(e));
		} else {
			anySuccess = true;
		}
	}
	return anySuccess;
}

bool Texture::CreateOpenGLtexture()
{
	if(glTextureHandle != 0) {
		glDeleteTextures(1, &glTextureHandle);
		glTextureHandle = 0;
	}

	if(elements.empty())
		return false;

	if(ktxTex != nullptr) {
		GLenum target = 0;
		GLenum glErr = 0;
		KTX_error_code res = ktxTexture_GLUpload(ktxTex, &glTextureHandle, &target, &glErr);
		if(res != KTX_SUCCESS) {
			glTextureHandle = 0;
			errprintf("Sending data from '%s' to the GPU with ktxTexture_GLUpload() failed. "
			          "KTX error: %s OpenGL error: %s\n", name.c_str(), ktxErrorString(res), getGLerrorString(glErr));
			return false;
		}
		glTarget = target;
		GLint intFmt = 0;
		GLenum baseFmt = 0;
		ktxTexture_GetOpenGLFormat(ktxTex, &intFmt, &baseFmt, NULL, NULL);
		printf("created texture  '%s' with internal format 0x%X base format 0x%X\n", name.c_str(), intFmt, baseFmt);
		return true;
	}

	glGenTextures(1, &glTextureHandle);
	glBindTexture(glTarget, glTextureHandle);

	GLenum internalFormat = dataFormat;
	int numMips = GetNumMips();

	glGetError();
	bool anySuccess = false;

	const bool isArray = IsArray();
	const bool isCubemap = IsCubemap();
	const bool isCompressed = (textureFlags & TF_COMPRESSED) != 0;
	if(!isArray) {
		if(isCubemap) {
			int elemIdx = 0;
			for(int cf=0; cf < 6; ++cf) {
				if(textureFlags & (TF_CUBEMAP_XPOS << cf)) {
					GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + cf;
					std::vector<MipLevel>& mipLevels = elements[elemIdx];
					for(int i=0; i<numMips; ++i) {
						if(UploadTexture2D(target, internalFormat, i, isCompressed, mipLevels[i])) {
							anySuccess = true;
						}
					}
					++elemIdx;
				}
			}
		} else { // Texture2D
			std::vector<MipLevel>& mipLevels = elements[0];
			for(int i=0; i<numMips; ++i) {
				if(UploadTexture2D(glTarget, internalFormat, i, isCompressed, mipLevels[i])) {
					anySuccess = true;
				}
			}
		}
	} else { // it's an array
		//glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels)
		// TODO: do this for arrays, somehow.. https://www.khronos.org/opengl/wiki/Cubemap_Texture#Cubemap_array_textures
		// maybe something with glTexImage3D(...., NULL) and then glTexSubImage() to set the data?
		// might make sense to make the mips iteration the outer loop and the elements interation the inner loop?
	}

	return anySuccess;
}

Texture::~Texture() {
	if(texDataFreeFun != nullptr) {
		texDataFreeFun( (void*)texData, texDataFreeCookie );
	}
	if(glTextureHandle > 0) {
		glDeleteTextures(1, &glTextureHandle);
	}
}

bool Texture::Load(const char* filename)
{
	Clear();

	std::string fname( ToAbsolutePath(filename) );
	filename = fname.c_str(); // from here on filename has an absolute path.

	MemMappedFile* mmf = LoadMemMappedFile(filename);
	if(mmf == nullptr) {
		return false;
	}
	if(mmf->length < 4) {
		errprintf("File '%s' is too small (%d) to contain useful image data!\n",
		          filename, (int)mmf->length);
	}

	if(memcmp(mmf->data, "DDS ", 4) == 0) {
		return LoadDDS(mmf, filename);
	}

	static const unsigned char ktx1identifier[] = {
		/*'«'*/0xAB, 'K', 'T', 'X', ' ', '1', '1', /*'»'*/0xBB, '\r', '\n', '\x1A', '\n'
	};
	static const unsigned char ktx2identifier[] = {
		/*'«'*/0xAB, 'K', 'T', 'X', ' ', '2', '0', /*'»'*/0xBB, '\r', '\n', '\x1A', '\n'
	};

	if( mmf->length > 12 && (memcmp(mmf->data, ktx1identifier, 12) == 0
	                         || memcmp(mmf->data, ktx2identifier, 12) == 0) )
	{
		return LoadKTX(mmf, filename);
	}

	// some other kind of file, try throwing it at stb_image
	if(mmf->length > INT_MAX) {
		errprintf("File '%s' is too big to load with stb_image\n", filename);
		UnloadMemMappedFile(mmf);
		return false;
	}
	const unsigned char* data = (const unsigned char*)mmf->data;
	int len = mmf->length;
	int w, h, comp;
	void* pix = nullptr;
	if(!stbi_info_from_memory(data, len, &w, &h, &comp)) {
		errprintf("Couldn't get info about '%s', maybe the filetype is unsupported?\n", filename);
		UnloadMemMappedFile(mmf);
		return false;
	}
	// we want either 8 or 16, 32, 64 or 96 bit pixels, not 24 or 48 (I think?)
	int numChans = comp < 3 ? comp : 4;
	if(stbi_is_hdr_from_memory(data, len)) {
		numChans = comp; // for float32 channels RGB (96bit) is also fine, I think?
		pix = stbi_loadf_from_memory(data, len, &w, &h, &comp, numChans);
		// TODO: should HDR be rendered with sRGB framebuffer enabled?
		// TODO: TF_HDR flag?
		formatName = "STB HDR (F32) ";
		glType = GL_FLOAT;
	} else if(stbi_is_16_bit_from_memory(data, len)) {
		pix = stbi_load_16_from_memory(data, len, &w, &h, &comp, numChans);
		formatName = "STB UNORM16 ";
		glType = GL_UNSIGNED_SHORT;
	} else {
		pix = stbi_load_from_memory(data, len, &w, &h, &comp, numChans);
		formatName = "STB UNORM8 ";
		glType = GL_UNSIGNED_BYTE;
	}

	if(pix != nullptr) {
		// mmf is not needed anymore, decoded image data is in pix
		UnloadMemMappedFile(mmf);
		mmf = nullptr;

		name = filename;
		fileType = FT_STB;
		glTarget = GL_TEXTURE_2D;

		switch(numChans) {
			case 4:
				formatName += (comp == 3) ? "RGB(X)" : "RGBA";
				dataFormat = GL_RGBA;
				glFormat = GL_RGBA;
				break;
			case 3:
				formatName += "RGB";
				dataFormat = GL_RGB;
				glFormat = GL_RGB;
				break;
			case 2:
				formatName += "Luminance+Alpha";
				dataFormat = GL_LUMINANCE_ALPHA;
				glFormat = GL_LUMINANCE_ALPHA;
				break;
			case 1:
				formatName += "Luminance";
				dataFormat = GL_LUMINANCE;
				glFormat = GL_LUMINANCE;
				break;
		}

		if(comp == STBI_rgb_alpha || comp == STBI_grey_alpha)
			textureFlags |= TF_HAS_ALPHA;
		texData = pix;
		texDataFreeFun = [](void* texData, intptr_t) -> void { stbi_image_free(texData); };

		elements.push_back( std::vector<MipLevel>() );
		elements[0].push_back( Texture::MipLevel(w, h, pix) );

		return true;
	} else {
		formatName = nullptr;
		glType = 0;
	}

	// TODO: anything else to try?

	errprintf("Couldn't load '%s', maybe the filetype is unsupported?\n", filename);
	UnloadMemMappedFile(mmf);
	return false;
}

bool Texture::LoadKTX(MemMappedFile* mmf, const char* filename)
{
	ktxTexture* ktxTex = nullptr;
	const unsigned char* data = (const unsigned char*)mmf->data;
	ktx_error_code_e res;

	res = ktxTexture_CreateFromMemory(data, mmf->length,
						   KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTex);

	if(res != KTX_SUCCESS) {
		errprintf("libktx couldn't load '%s': %s (%d)\n", filename, ktxErrorString(res), res);
		UnloadMemMappedFile(mmf);
		return false;
	}

	ktxTexture2* ktxTex2 = nullptr;
	if(ktxTex->classId == ktxTexture2_c) {
		ktxTex2 = (ktxTexture2*)ktxTex;
	}

	if(ktxTexture_NeedsTranscoding(ktxTex)) {
		res = ktxTexture2_TranscodeBasis(ktxTex2, KTX_TTF_BC7_RGBA, 0);
		if(res != KTX_SUCCESS) {
			errprintf("libktx couldn't transcode '%s': %s (%d)\n", filename, ktxErrorString(res), res);
			ktxTexture_Destroy(ktxTex);
			UnloadMemMappedFile(mmf);
			return false;
		}
	}
	name = filename;
	// TODO: would be nicer maybe to prepend "KTX" and maybe to use GL-like names like the DDS loader does
	//   for that https://github.com/KhronosGroup/KTX-Specification/blob/main/formats.json could help
	formatName = (ktxTex->classId == ktxTexture2_c) ? "KTX2 " : "KTX ";
	formatName += ktxTexture_GetFormatName(ktxTex);

	this->ktxTex = ktxTex;
	fileType = FT_KTX;
	if(ktxTex->isCompressed)
		textureFlags |= TF_COMPRESSED;
	if(ktxTexture_FormatHasAlpha(ktxTex))
		textureFlags |= TF_HAS_ALPHA;
	else if(ktxTex2 != nullptr && ktxTexture2_GetPremultipliedAlpha(ktxTex2))
		textureFlags |= TF_PREMUL_ALPHA;
	if(ktxTexture_FormatIsSRGB(ktxTex))
		textureFlags |= TF_SRGB;

	int numMips = ktxTex->numLevels;
	int numElements = 1;
	int numCubeFaces = 0;
	if(ktxTex->isArray && ktxTex->numLayers > 1) {
		numElements = ktxTex->numLayers;
		textureFlags |= TF_IS_ARRAY;
	}
	if(ktxTex->isCubemap) {
		numCubeFaces = ktxTex->numFaces;
		numElements *= numCubeFaces;
		if(numCubeFaces == 6) {
			textureFlags |= TF_CUBEMAP_MASK;
		} else {
			for(int i=0; i<numCubeFaces; ++i) {
				textureFlags |= uint32_t(TF_CUBEMAP_XPOS) << i;
			}
		}
	}

	elements.resize(numElements);
	for( int e=0; e < numElements; ++e) {
		std::vector<MipLevel>& mipLevels = elements[e];
		mipLevels.reserve(numMips);
		int w = ktxTex->baseWidth;
		int h = ktxTex->baseHeight;
		for(int i=0; i < numMips; ++i) {
			// just use dummy miplevels for easy access to the mip sizes
			mipLevels.push_back(MipLevel(w, h));
			w = std::max(w/2, 1);
			h = std::max(h/2, 1);
		}
	}

	texData = mmf;
	texDataFreeCookie = (intptr_t)ktxTex;
	texDataFreeFun = [](void* texData, intptr_t cookie) -> void {
		MemMappedFile* mmf = (MemMappedFile*)texData;
		ktxTexture* ktxTex = (ktxTexture*)(void*)cookie;
		ktxTexture_Destroy(ktxTex);
		UnloadMemMappedFile(mmf);
	};

	return true;
}


  /**********************************
   * Rest of the file: DDS loading  *
   *                                */

static_assert(sizeof(DDS_HEADER) == 124, "DDS header somehow has wrong size");
static_assert(sizeof(DDS_PIXELFORMAT) == 32, "DDS_PIXELFORMAT has wrong size");
static_assert(sizeof(DDS_HEADER_DXT10) == 20, "DDS_HEADER_DXT10 has wrong size");

enum PitchType {
	UNKNOWN = 0,
	BLOCK8 = -1,  // DXT1, BC1, BC4
	BLOCK16 = -2, // other block-compressed formats (like BC2/3/5/6/7)
	WEIRD_LEGACY = -3 // R8G8_B8G8, G8R8_G8B8, legacy UYVY-packed, and legacy YUY2-packed formats
};

struct ComprFormatInfo {
	uint32_t ddsFourCC;
	int dxgiFormat;
	uint32_t glFormat;
	int32_t pitchTypeOrBitsPPixel; // PitchType or for "other" formats their bits per pixel
	const char* name;
	uint32_t pfFlags; // usually 0, DDPF_ALPHAPIXELS for DX1A
	uint8_t dx10misc2; // maybe DDS_ALPHA_MODE_PREMULTIPLIED or DDS_ALPHA_MODE_OPAQUE (only uses first 3 bits)
	uint8_t ourFlags;
};

enum { DX10 = PIXEL_FMT_DX10 }; // shorter alias for following tables

const ComprFormatInfo comprFormatTable[] = {
	// DXT1-5 set with classic FourCC
	{ PIXEL_FMT_DXT1,      0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, BLOCK8,  "DXT1 (BC1) w/ alpha", DDPF_ALPHAPIXELS },
	{ PIXEL_FMT_DXT1,      0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT,  BLOCK8,  "DXT1 (BC1)" },
	{ PIXEL_FMT_DXT3,      0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, BLOCK16, "DXT3 (BC2)" },
	{ PIXEL_FMT_DXT2,      0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, BLOCK16, "DXT2 (BC2 alpha premul)", 0, 0, TF_PREMUL_ALPHA }, // but alpha premultiplied
	{ PIXEL_FMT_DXT5,      0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT5 (BC3)" },
	{ PIXEL_FMT_DXT4,      0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT4 (BC3 alpha premul)", 0, 0, TF_PREMUL_ALPHA }, // but alpha premultiplied
	// unofficial DXT5 derivative (R and A chan swapped, or real A even set to 0)
	// Doom3 checks for it 'RXGB' in fourcc and uses this (only!) for normalmaps
	{ PIXEL_FMT_DXT5_RXGB, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT5 (BC3) RXGB (xGBR)" },

	// other unofficial derivatives of DXT1-5 - TODO: or are those put in dwRGBBitCount? (see comment in header about compressonator)
	// FIXME: unsure if 'DX1A' really exists/is used (except in crunch internals) - OTOH, this check doesn't hurt anyone
	{ PIXEL_FMT_DXT1A,     0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, BLOCK8,  "DXT1A (BC1 w/ alpha)" },
	// FIXME: crunch checks for the following PIXEL_FMT_DXT5_* constants in desc.ddpfPixelFormat.dwRGBBitCount,
	//        if fourcc is PIXEL_FMT_DXT4/5 (apparently compressonator puts them there)
#if 0
	{ PIXEL_FMT_DXT5_CCxY, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT5 (BC3) CCxY" },
	{ PIXEL_FMT_DXT5_xGxR, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT5 (BC3) xGxR" },
	{ PIXEL_FMT_DXT5_xGBR, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT5 (BC3) xGBR" },
	{ PIXEL_FMT_DXT5_AGBR, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT5 (BC3) AGBR" },
#endif

	// formats based on DXT (BC4/5 aka 3Dc/+) https://en.wikipedia.org/wiki/3Dc https://aras-p.info/texts/D3D9GPUHacks.html#3dc
	// - "ATI1n" aka 3Dc+ aka BC4: 'ATI1', 'BC4U' = BC4_UNORM, 'BC4S' = BC4_SNORM
	//   => single channel 4bits/pixel, basically DXT5 alpha block. GL_COMPRESSED_RED_RGTC1
	// TODO: could also use LATC (see below)
	{ PIXEL_FMT_DXT5A,     0, GL_COMPRESSED_RED_RGTC1_EXT,      BLOCK8,  "ATI1n aka 3Dc+ (BC4/RGTC1)" },
	{ PIXEL_FMT_BC4U,      0, GL_COMPRESSED_RED_RGTC1_EXT,      BLOCK8,  "BC4U (ATI1n/3Dc+/RGTC1)" },
	{ PIXEL_FMT_BC4S,    0, GL_COMPRESSED_SIGNED_RED_RGTC1_EXT, BLOCK8,  "BC4S (ATI1n/3Dc+/RGTC1)" },
	// - "ATI2n" aka 3Dc aka BC5 (basically): 'ATI2' = BC5_UNORM, 'BC5S' = BC5_SNORM
	//   'BC5U' and 'A2XY' has same channel layout as BC5, 'ATI2' has channels swapped (first Y then X)
	//   => two channel, 8 bits per pixel, basically two DXT5 alpha blocks.
	// TODO: could also use LATC (see below)
	{ PIXEL_FMT_BC5U,      0, GL_COMPRESSED_RED_GREEN_RGTC2_EXT,    BLOCK16, "BC5U aka 3Dc (BC5/RGTC2 XY)" },
	// FIXME: crunch checks for PIXEL_FMT_DXN in dwRGBBitCount, if fourcc is PIXEL_FMT_3DC
	{ PIXEL_FMT_DXN,       0, GL_COMPRESSED_RED_GREEN_RGTC2_EXT,    BLOCK16, "ATI2n aka 3Dc (BC5/RGTC2 XY)" },
	{ PIXEL_FMT_3DC,       0, GL_COMPRESSED_RED_GREEN_RGTC2_EXT,    BLOCK16, "ATI2n aka 3Dc (BC5/RGTC2 YX)" },
	{ PIXEL_FMT_BC5S,  0, GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT, BLOCK16, "BC5S (ATI2n/3Dc/RGTC2)" },
	// NOTE: GL_NV_texture_compression_vtc also exists, but it's just DXT for 3D textures

	// now the DXT1-5 and BC4/5 formats as BC1-5 from DXGI_FORMAT
	{ DX10, DXGI_FORMAT_BC1_UNORM,      GL_COMPRESSED_RGB_S3TC_DXT1_EXT,  BLOCK8,  "BC1 (DXT1) opaque", 0, DDS_DX10MISC2_ALPHA_OPAQUE },
	{ DX10, DXGI_FORMAT_BC1_UNORM,      GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, BLOCK8,  "BC1 (DXT1)" },
	{ DX10, DXGI_FORMAT_BC1_UNORM_SRGB, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, BLOCK8,  "BC1 (DXT1) sRGB opaque", 0, DDS_DX10MISC2_ALPHA_OPAQUE, TF_SRGB },
	{ DX10, DXGI_FORMAT_BC1_UNORM_SRGB, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, BLOCK8,  "BC1 (DXT1) sRGB" },
	// TODO: what are those typeless formats good for? are they used in files or only for buffers?
	{ DX10, DXGI_FORMAT_BC1_TYPELESS,   GL_COMPRESSED_RGB_S3TC_DXT1_EXT,  BLOCK8,  "BC1 (DXT1) typeless opaque", 0, DDS_DX10MISC2_ALPHA_OPAQUE, TF_TYPELESS },
	{ DX10, DXGI_FORMAT_BC1_TYPELESS,   GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, BLOCK8,  "BC1 (DXT1) typeless" },

	{ DX10, DXGI_FORMAT_BC2_UNORM,      GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,       BLOCK16, "BC2 (DXT3)" },
	{ DX10, DXGI_FORMAT_BC2_UNORM_SRGB, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, BLOCK16, "BC2 (DXT3) sRGB", 0, 0, TF_SRGB },
	{ DX10, DXGI_FORMAT_BC2_TYPELESS,   GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,       BLOCK16, "BC2 (DXT3) typeless", 0, 0, TF_TYPELESS },

	{ DX10, DXGI_FORMAT_BC3_UNORM,      GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,       BLOCK16, "BC3 (DXT5)" },
	{ DX10, DXGI_FORMAT_BC3_UNORM_SRGB, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, BLOCK16, "BC3 (DXT5) sRGB", 0, 0, TF_SRGB },
	{ DX10, DXGI_FORMAT_BC3_TYPELESS,   GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,       BLOCK16, "BC3 (DXT5) typeless", 0, 0, TF_TYPELESS },

	// TODO: could also use GL_COMPRESSED_LUMINANCE_LATC1_EXT and GL_COMPRESSED_SIGNED_LUMINANCE_LATC1_EXT
	{ DX10, DXGI_FORMAT_BC4_UNORM,    GL_COMPRESSED_RED_RGTC1_EXT,        BLOCK8, "BC4U (ATI1n/3Dc+/RGTC1)" },
	{ DX10, DXGI_FORMAT_BC4_SNORM,    GL_COMPRESSED_SIGNED_RED_RGTC1_EXT, BLOCK8, "BC4S (ATI1n/3Dc+/RGTC1)" },
	{ DX10, DXGI_FORMAT_BC4_TYPELESS, GL_COMPRESSED_RED_RGTC1_EXT,        BLOCK8, "BC4  (ATI1n/3Dc+/RGTC1) typeless", 0, 0, TF_TYPELESS },
	// TODO: could also use GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT and GL_COMPRESSED_SIGNED_LUMINANCE_ALPHA_LATC2_EXT
	{ DX10, DXGI_FORMAT_BC5_UNORM,    GL_COMPRESSED_RED_GREEN_RGTC2_EXT,     BLOCK16, "BC5U (ATI1n/3Dc+/RGTC2)" },
	{ DX10, DXGI_FORMAT_BC5_SNORM, GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT, BLOCK16, "BC5S (ATI1n/3Dc+)" },
	{ DX10, DXGI_FORMAT_BC5_TYPELESS, GL_COMPRESSED_RED_GREEN_RGTC2_EXT,     BLOCK16, "BC5  (ATI1n/3Dc+) typeless" },

	// BC6, BC7
	{ DX10, DXGI_FORMAT_BC6H_SF16,      GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB,   BLOCK16, "BC6S (BPTC HDR)" },
	{ DX10, DXGI_FORMAT_BC6H_UF16,      GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB, BLOCK16, "BC6U (BPTC HDR)" },
	{ DX10, DXGI_FORMAT_BC6H_TYPELESS,  GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB, BLOCK16, "BC6  (BPTC HDR) typeless", 0, 0, TF_TYPELESS },
	{ PIXEL_FMT_BC6H,   0,              GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB, BLOCK16, "BC6U (BPTC HDR)" },

	{ DX10, DXGI_FORMAT_BC7_UNORM,      GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,         BLOCK16, "BC7 (BPTC)" },
	{ DX10, DXGI_FORMAT_BC7_UNORM_SRGB, GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB,   BLOCK16, "BC7 SRGB (BPTC)" },
	{ DX10, DXGI_FORMAT_BC7_TYPELESS,   GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,         BLOCK16, "BC7 (BPTC) typeless", 0, 0, TF_TYPELESS },
	{ PIXEL_FMT_BC7L,   0,              GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,         BLOCK16, "BC7 (BPTC)" },
	{ PIXEL_FMT_BC7,    0,              GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,         BLOCK16, "BC7 (BPTC)" },

	// ETC1/2
	// https://registry.khronos.org/OpenGL-Refpages/es3.0/html/glCompressedTexImage2D.xhtm documents blocksizes
	{ PIXEL_FMT_ETC1,    0, GL_COMPRESSED_RGB8_ETC2,        BLOCK8,   "ETC1" }, // ETC1 can be loaded as ETC2 RGB
	{ PIXEL_FMT_ETC,     0, GL_COMPRESSED_RGB8_ETC2,        BLOCK8,   "ETC1" },
	{ PIXEL_FMT_ETC2,    0, GL_COMPRESSED_RGB8_ETC2,        BLOCK8,   "ETC2" },
	// TODO: the previous formats with DDPF_ALPHAPIXELS for GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 ?
	{ PIXEL_FMT_ETC2A,   0, GL_COMPRESSED_RGBA8_ETC2_EAC,   BLOCK16,  "ETC2 with Alpha" },

	{ PIXEL_FMT_EACR11,  0, GL_COMPRESSED_R11_EAC,          BLOCK8,   "EAC R11" },
	{ PIXEL_FMT_EACRG11, 0, GL_COMPRESSED_RG11_EAC,         BLOCK16,  "EAC RG11" },
};

struct ASTCInfo {
	uint32_t ddsFourCC;
	int dxgiFormat;
	uint32_t glFormat;
	// ASTC 12x10 has blockW 12 and blockH 10
	uint8_t blockW;
	uint8_t blockH;
	uint8_t ourFlags; // TF_SRGB, TF_TYPELESS
	const char* name;
};

#define ASTC_SIZES  \
	ASTC_SIZE(4, 4)   \
	ASTC_SIZE(5, 4)   \
	ASTC_SIZE(5, 5)   \
	ASTC_SIZE(6, 5)   \
	ASTC_SIZE(6, 6)   \
	ASTC_SIZE(8, 5)   \
	ASTC_SIZE(8, 6)   \
	ASTC_SIZE(8, 8)   \
	ASTC_SIZE(10, 5)  \
	ASTC_SIZE(10, 6)  \
	ASTC_SIZE(10, 8)  \
	ASTC_SIZE(10, 10) \
	ASTC_SIZE(12, 10) \
	ASTC_SIZE(12, 12)

const ASTCInfo astcFormatTable[] = {

#define ASTC_SIZE(W, H) \
	{ DX10, DXGI_FORMAT_ASTC_ ## W ## X ## H ## _TYPELESS, GL_COMPRESSED_RGBA_ASTC_ ## W ## x ## H ## _KHR, \
		W, H, TF_TYPELESS, "ASTC " #W "x" #H " typeless" }, \
	{ DX10, DXGI_FORMAT_ASTC_ ## W ## X ## H ## _UNORM, GL_COMPRESSED_RGBA_ASTC_ ## W ## x ## H ## _KHR, \
		W, H, 0, "ASTC " #W "x" #H " UNORM" }, \
	{ PIXEL_FMT_ASTC_ ## W ## x ## H, 0, GL_COMPRESSED_RGBA_ASTC_ ## W ## x ## H ## _KHR, \
		W, H, 0, "ASTC " #W "x" #H " UNORM" }, \
	{ DX10, DXGI_FORMAT_ASTC_ ## W ## X ## H ## _UNORM_SRGB, GL_COMPRESSED_SRGB8_ALPHA8_ASTC_ ## W ## x ## H ## _KHR, \
		W, H, TF_SRGB, "ASTC " #W "x" #H " UNORM SRGB" },

	// expand all ASTC_SIZE() entries in the ASTC_SIZES table
	// with the ASTC_SIZE(W, H) definition from the previous lines
	ASTC_SIZES

#undef ASTC_SIZE

	// special case: alternative FOURCCs (from BGFX BIMG) for w or h >= 10
#define ALT_ASTC_ENTRY(W, H) \
	{ PIXEL_FMT_ASTC_ ## W ## x ## H ## _ALT, 0, GL_COMPRESSED_RGBA_ASTC_ ## W ## x ## H ## _KHR, \
		W, H, 0, "ASTC " #W "x" #H " UNORM" }

	ALT_ASTC_ENTRY(10, 5),
	ALT_ASTC_ENTRY(10, 6),
	ALT_ASTC_ENTRY(10, 8),
	ALT_ASTC_ENTRY(10, 10),
	ALT_ASTC_ENTRY(12, 10),
	ALT_ASTC_ENTRY(12, 12),

#undef ALT_ASTC_ENTRY
};

struct UncomprFormatInfo {
	uint32_t ddsD3Dfmt;
	int dxgiFormat;

	uint32_t glIntFormat;
	uint32_t glFormat;
	uint32_t glType;
	uint32_t bitsPerPixel;

	const char* name;

	uint8_t ourFlags;
};

#ifndef GL_RGB10_A2UI // GL3.3+ - I don't wanna bump the min GL version for one obscure format...
  #define GL_RGB10_A2UI   0x906F
#endif

// uncompressed formats that can be identified based on FOURCC or DGXI format
// NOTE: in this table, ddsD3Dfmt and dxgiFormat are alternatives, so match only one of them
//       (in both cases 0 means "format doesn't exist here")
const UncomprFormatInfo uncomprFormatTable[] = {

	// first D3DFMT_ formats that don't have a dxgi equivalent
	{ D3DFMT_A2R10G10B10, 0,  GL_RGBA,    GL_RGBA,    GL_UNSIGNED_INT_10_10_10_2,      32, "BGR10A2 UNORM ??" },
	// ok, the following *does* have a dxgi equivalent, but is known to be wrongly
	// encoded in many DDS files so I have this "duplicate" entry that has a ? in the name
	// (and if the dds contains DXGI_FORMAT_R10G10B10A2_UNORM it gets a name without '?')
	{ D3DFMT_A2B10G10R10, 0,  GL_RGBA,    GL_RGBA,    GL_UNSIGNED_INT_2_10_10_10_REV,  32, "RGB10A2 UNORM ?" },
	{ D3DFMT_X1R5G5B5, 0,     GL_RGBA,    GL_BGRA,    GL_UNSIGNED_SHORT_1_5_5_5_REV,    8, "RGB5X1 UNORM", _TF_NOALPHA },
	{ D3DFMT_X8B8G8R8, 0,     GL_RGBA,    GL_RGBA,    GL_UNSIGNED_BYTE,                32, "RGBX8 UNORM", _TF_NOALPHA },
	{ D3DFMT_R8G8B8,   0,     GL_BGR,     GL_BGR,     GL_UNSIGNED_BYTE,                24, "BGR8 UNORM" },
	// I added D3DFMT_B8G8R8, it's non-standard. we use 220 for it (and so does Gimp), dxwrapper uses 19
	// (no idea if anyone else uses those values and if they're actually written to any files, but why not try to support them..)
	{ D3DFMT_B8G8R8,   0,     GL_RGB,     GL_RGB,     GL_UNSIGNED_BYTE,                24, "RGB8 UNORM" }, // gimp variant
	{ 19,              0,     GL_RGB,     GL_RGB,     GL_UNSIGNED_BYTE,                24, "RGB8 UNORM" }, // dxwrapper variant
	{ D3DFMT_X4R4G4B4, 0,     GL_RGBA,    GL_RGBA,    GL_UNSIGNED_SHORT_4_4_4_4,       16, "RGBX4 UNORM", _TF_NOALPHA },
	{ D3DFMT_A8L8, 0,  GL_LUMINANCE_ALPHA,  GL_LUMINANCE_ALPHA,  GL_UNSIGNED_BYTE,     16, "Luminance8 Alpha8" },
	{ D3DFMT_L16,      0, GL_LUMINANCE, GL_LUMINANCE, GL_UNSIGNED_SHORT,               16, "Luminance16" },
	{ D3DFMT_L8,       0, GL_LUMINANCE, GL_LUMINANCE, GL_UNSIGNED_BYTE,                 8, "Luminance8" },


	// all the DXGI formats... https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format
	// mappings from D3DFMT: https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-legacy-formats
	// extremely helpful: https://github.khronos.org/KTX-Specification/ktxspec.v2.html#formatMapping
	// also helpful: https://gist.github.com/Kos/4739337
	{ 0, DXGI_FORMAT_R32G32B32A32_TYPELESS, GL_RGBA32UI,   GL_RGBA_INTEGER, GL_UNSIGNED_INT,   128, "RGBA32 typeless", TF_TYPELESS }, // TODO: what type for typeless?
	{ D3DFMT_A32B32G32R32F,
	     DXGI_FORMAT_R32G32B32A32_FLOAT,    GL_RGBA,       GL_RGBA,         GL_FLOAT,          128, "RGBA32 FLOAT" },
	{ 0, DXGI_FORMAT_R32G32B32A32_UINT,     GL_RGBA32UI,   GL_RGBA_INTEGER, GL_UNSIGNED_INT,   128, "RGBA32 UINT" },
	{ 0, DXGI_FORMAT_R32G32B32A32_SINT,     GL_RGBA32I,    GL_RGBA_INTEGER, GL_INT,            128, "RGBA32 SINT" },

	{ 0, DXGI_FORMAT_R32G32B32_TYPELESS,    GL_RGB32UI,    GL_RGB_INTEGER,  GL_UNSIGNED_INT,    96, "RGB32 typeless", TF_TYPELESS },
	{ 0, DXGI_FORMAT_R32G32B32_FLOAT,       GL_RGB,        GL_RGB,          GL_FLOAT,           96, "RGB32 FLOAT" },
	{ 0, DXGI_FORMAT_R32G32B32_UINT,        GL_RGB32UI,    GL_RGB_INTEGER,  GL_UNSIGNED_INT,    96, "RGB32 UINT" },
	{ 0, DXGI_FORMAT_R32G32B32_SINT,        GL_RGB32I,     GL_RGB_INTEGER,  GL_INT,             96, "RGB32 SINT" },

	{ 0, DXGI_FORMAT_R16G16B16A16_TYPELESS, GL_RGBA16UI,   GL_RGBA_INTEGER, GL_UNSIGNED_SHORT,  64, "RGBA16 typeless", TF_TYPELESS },
	{ D3DFMT_A16B16G16R16F,
	     DXGI_FORMAT_R16G16B16A16_FLOAT,    GL_RGBA,       GL_RGBA,         GL_HALF_FLOAT,      64, "RGBA16 FLOAT" },
	{ D3DFMT_A16B16G16R16,
	     DXGI_FORMAT_R16G16B16A16_UNORM,    GL_RGBA,       GL_RGBA,         GL_UNSIGNED_SHORT,  64, "RGBA16 UNORM" },
	{ 0, DXGI_FORMAT_R16G16B16A16_UINT,     GL_RGBA16UI,   GL_RGBA_INTEGER, GL_UNSIGNED_SHORT,  64, "RGBA16 UINT" },
	{ D3DFMT_Q16W16V16U16,
	     DXGI_FORMAT_R16G16B16A16_SNORM,    GL_RGBA,       GL_RGBA,         GL_SHORT,           64, "RGBA16 SNORM" },
	{ 0, DXGI_FORMAT_R16G16B16A16_SINT,     GL_RGBA16I,    GL_RGBA_INTEGER, GL_SHORT,           64, "RGBA16 SINT" },

	{ 0, DXGI_FORMAT_R32G32_TYPELESS,       GL_RG32UI,     GL_RG_INTEGER,   GL_UNSIGNED_INT,    64, "RG32 typeless", TF_TYPELESS },
	{ D3DFMT_G32R32F,
	     DXGI_FORMAT_R32G32_FLOAT,          GL_RG,         GL_RG,           GL_FLOAT,           64, "RG32 FLOAT" },
	{ 0, DXGI_FORMAT_R32G32_UINT,           GL_RG32UI,     GL_RG_INTEGER,   GL_UNSIGNED_INT,    64, "RG32 UINT" },
	{ 0, DXGI_FORMAT_R32G32_SINT,           GL_RG32I,      GL_RG_INTEGER,   GL_INT,             64, "RG32 SINT" },

	// the next one is the only useful out of the following four, I guess
	{ 0, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,     GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64, "Depth32 FLOAT Stencil8 UINT" },
	{ 0, DXGI_FORMAT_R32G8X24_TYPELESS,        GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64, "R32G8X24_TYPELESS", TF_TYPELESS },
	{ 0, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64, "R32_FLOAT_X8X24_TYPELESS", TF_TYPELESS },
	{ 0, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,  GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64, "X32_TYPELESS_G8X24_UINT", TF_TYPELESS },

	// R in least significant bits, little endian order
	// ktx2 spec says GL_RGBA and ..._REV is correct
	{ 0, DXGI_FORMAT_R10G10B10A2_TYPELESS,  GL_RGB10_A2UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV,  32, "RGB10A2 typeless", TF_TYPELESS },
	{ 0, DXGI_FORMAT_R10G10B10A2_UNORM,     GL_RGBA,       GL_RGBA,         GL_UNSIGNED_INT_2_10_10_10_REV,  32, "RGB10A2 UNORM" },
	{ 0, DXGI_FORMAT_R10G10B10A2_UINT,      GL_RGB10_A2UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV,  32, "RGB10A2 UINT" },

	{ 0, DXGI_FORMAT_R11G11B10_FLOAT,       GL_RGB,        GL_RGB,          GL_UNSIGNED_INT_10F_11F_11F_REV, 32, "RG11B10 FLOAT" }, //GL_R11F_G11F_B10F

	{ 0, DXGI_FORMAT_R8G8B8A8_TYPELESS,     GL_RGBA8UI,    GL_RGBA_INTEGER, GL_UNSIGNED_BYTE,   32, "RGBA8 typeless", TF_TYPELESS },
	{ D3DFMT_A8B8G8R8,
	     DXGI_FORMAT_R8G8B8A8_UNORM,        GL_RGBA,       GL_RGBA,         GL_UNSIGNED_BYTE,   32, "RGBA8 UNORM" },
	{ 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,   GL_SRGB_ALPHA, GL_RGBA,         GL_UNSIGNED_BYTE,   32, "RGBA8 UNORM SRGB", TF_SRGB },
	{ 0, DXGI_FORMAT_R8G8B8A8_UINT,         GL_RGBA8UI,    GL_RGBA_INTEGER, GL_UNSIGNED_BYTE,   32, "RGBA8 UINT" },
	{ D3DFMT_Q8W8V8U8,
	     DXGI_FORMAT_R8G8B8A8_SNORM,        GL_RGBA,       GL_RGBA,         GL_BYTE,            32, "RGBA8 SNORM" },
	{ 0, DXGI_FORMAT_R8G8B8A8_SINT,         GL_RGBA8I,     GL_RGBA_INTEGER, GL_BYTE,            32, "RGBA8 SINT" },

	{ 0, DXGI_FORMAT_R16G16_TYPELESS,       GL_RG16UI,     GL_RG_INTEGER,   GL_UNSIGNED_SHORT,  32, "RG16 typeless", TF_TYPELESS },
	{ D3DFMT_G16R16F,
	     DXGI_FORMAT_R16G16_FLOAT,          GL_RG,         GL_RG,           GL_HALF_FLOAT,      32, "RG16 FLOAT" },
	{ D3DFMT_G16R16,
	     DXGI_FORMAT_R16G16_UNORM,          GL_RG,         GL_RG,           GL_UNSIGNED_SHORT,  32, "RG16 UNORM" },
	{ 0, DXGI_FORMAT_R16G16_UINT,           GL_RG16UI,     GL_RG_INTEGER,   GL_UNSIGNED_SHORT,  32, "RG16 UINT" },
	{ 0, DXGI_FORMAT_R16G16_SNORM,          GL_RG,         GL_RG,           GL_SHORT,           32, "RG16 SNORM" },
	{ 0, DXGI_FORMAT_R16G16_SINT,           GL_RG16I,      GL_RG_INTEGER,   GL_SHORT,           32, "RG16 UINT" },

	{ 0, DXGI_FORMAT_R32_TYPELESS,          GL_R32UI,      GL_RED_INTEGER,  GL_UNSIGNED_INT,    32, "Red32 typeless", TF_TYPELESS },
	{ D3DFMT_D32F_LOCKABLE,
	     DXGI_FORMAT_D32_FLOAT,      GL_DEPTH_COMPONENT,  GL_DEPTH_COMPONENT,  GL_FLOAT,        32, "Depth32 FLOAT" },
	{ D3DFMT_R32F,
	     DXGI_FORMAT_R32_FLOAT,             GL_RED,        GL_RED,          GL_FLOAT,           32, "Red32 FLOAT" },
	{ D3DFMT_INDEX32,
	     DXGI_FORMAT_R32_UINT,              GL_R32UI,      GL_RED_INTEGER,  GL_UNSIGNED_INT,    32, "Red32 UINT" },
	{ 0, DXGI_FORMAT_R32_SINT,              GL_R32I,       GL_RED_INTEGER,  GL_INT,             32, "Red32 SINT" },

	// the next one is probably again the only useful one of the next 4
	// TODO: not sure about D3DFMT_D24S8, seems like it should be called D3DFMT_S8D24 if it's equivalent to DXGI_FORMAT_D24_UNORM_S8_UINT ?
	//       https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-legacy-formats
	//       even says that D3DFMT_S8D24 is the equivalent, but that format doesn't exist -_-
	{ D3DFMT_D24S8,
	     DXGI_FORMAT_D24_UNORM_S8_UINT,     GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32, "Depth24 UNORM Stencil8 UINT" },
	{ 0, DXGI_FORMAT_R24G8_TYPELESS,        GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32, "R24G8_TYPELESS", TF_TYPELESS },
	{ 0, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32, "R24_UNORM_X8_TYPELESS", TF_TYPELESS },
	{ 0, DXGI_FORMAT_X24_TYPELESS_G8_UINT,  GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32, "X24_TYPELESS_G8_UINT", TF_TYPELESS },

	{ 0, DXGI_FORMAT_R8G8_TYPELESS,         GL_RG8UI,       GL_RG_INTEGER,  GL_UNSIGNED_BYTE,   16, "RG8 typeless", TF_TYPELESS },
	{ 0, DXGI_FORMAT_R8G8_UNORM,            GL_RG,          GL_RG,          GL_UNSIGNED_BYTE,   16, "RG8 UNORM" },
	{ 0, DXGI_FORMAT_R8G8_UINT,             GL_RG8UI,       GL_RG_INTEGER,  GL_UNSIGNED_BYTE,   16, "RG8 UINT" },
	{ D3DFMT_V8U8,
	     DXGI_FORMAT_R8G8_SNORM,            GL_RG,          GL_RG,          GL_BYTE,            16, "RG8 SNORM" },
	{ 0, DXGI_FORMAT_R8G8_SINT,             GL_RG8I,        GL_RG_INTEGER,  GL_BYTE,            16, "RG8 SINT" },

	{ D3DFMT_R16F,
	     DXGI_FORMAT_R16_FLOAT,             GL_RED,         GL_RED,         GL_HALF_FLOAT,      16, "Red16 FLOAT" },
	{ D3DFMT_D16,
	     DXGI_FORMAT_D16_UNORM,     GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,  16, "Depth16 UNORM" },
	{ 0, DXGI_FORMAT_R16_UNORM,             GL_RED,        GL_RED,          GL_UNSIGNED_SHORT,  16, "Red16 UNORM" },
	// TODO: gli/data/kueken7_r16_unorm.dds (which is really UINT) is black
	// FIXME: apparently for (all?) _INTEGER formats one needs to use a sized type as internal format
	{ D3DFMT_INDEX16,
	     DXGI_FORMAT_R16_UINT,              GL_R16UI,      GL_RED_INTEGER,  GL_UNSIGNED_SHORT,  16, "Red16 UINT" },
	{ 0, DXGI_FORMAT_R16_SNORM,             GL_RED,        GL_RED,          GL_SHORT,           16, "Red16 SNORM" },
	{ 0, DXGI_FORMAT_R16_SINT,              GL_R16I,       GL_RED_INTEGER,  GL_SHORT,           16, "Red16 SINT" },

	{ 0, DXGI_FORMAT_R8_TYPELESS,           GL_R8UI,       GL_RED_INTEGER,  GL_UNSIGNED_BYTE,    8, "Red8 typeless", TF_TYPELESS },
	{ 0, DXGI_FORMAT_R8_UNORM,              GL_RED,        GL_RED,          GL_UNSIGNED_BYTE,    8, "Red8 UNORM" },
	// FIXME: looks like I have to use the sized internal formats after all? at least for some formats?
	// FIXME gli/data/kueken7_r8_uint.dds and kueken7_r8_sint.dds do load, but are black
	{ 0, DXGI_FORMAT_R8_UINT,               GL_R8UI,       GL_RED_INTEGER,  GL_UNSIGNED_BYTE,    8, "Red8 UINT" },
	{ 0, DXGI_FORMAT_R8_SNORM,              GL_RED,        GL_RED,          GL_BYTE,             8, "Red8 SNORM" },
	{ 0, DXGI_FORMAT_R8_SINT,               GL_R8I,        GL_RED_INTEGER,  GL_BYTE,             8, "Red8 SINT" },
	{ D3DFMT_A8,
	     DXGI_FORMAT_A8_UNORM,              GL_ALPHA,      GL_ALPHA,        GL_UNSIGNED_BYTE,    8, "Alpha8 UNORM" },

	{ 0, DXGI_FORMAT_R9G9B9E5_SHAREDEXP,    GL_RGB,        GL_RGB,   GL_UNSIGNED_INT_5_9_9_9_REV, 32, "RGB9 E5 shared exp float" },

	// NOTE: the compressed BC1-BC5 formats are handled in comprFormatTable[]

	{ D3FMT_R5G6B5,
	     DXGI_FORMAT_B5G6R5_UNORM,          GL_RGB,        GL_RGB,     GL_UNSIGNED_SHORT_5_6_5, 16, "RGB565 UNORM" },
	{ D3DFMT_A1R5G5B5,
	     DXGI_FORMAT_B5G5R5A1_UNORM,        GL_RGBA,       GL_BGRA,    GL_UNSIGNED_SHORT_1_5_5_5_REV, 16, "RGB5A1 UNORM" },

	// TODO: { 0, DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, GL_RGBA, GL_BGRA,   GL_UNSIGNED_INT_2_10_10_10_REV, 32, "BGRA" }, ???

	{ D3DFMT_A8R8G8B8,
	     DXGI_FORMAT_B8G8R8A8_UNORM,        GL_RGBA,       GL_BGRA,         GL_UNSIGNED_BYTE,   32, "BGRA8 UNORM" },
	{ D3DFMT_X8R8G8B8,
	     DXGI_FORMAT_B8G8R8X8_UNORM,        GL_RGBA,       GL_BGRA,         GL_UNSIGNED_BYTE,   32, "BGRX8 UNORM", _TF_NOALPHA },
	{ 0, DXGI_FORMAT_B8G8R8A8_TYPELESS,     GL_RGBA,       GL_BGRA,         GL_UNSIGNED_BYTE,   32, "BGRA typeless (as UNORM)", TF_TYPELESS },
	{ 0, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,   GL_SRGB_ALPHA, GL_BGRA,         GL_UNSIGNED_BYTE,   32, "BGRA8 SRGB UNORM", TF_SRGB },
	{ 0, DXGI_FORMAT_B8G8R8X8_TYPELESS,     GL_RGBA,       GL_BGRA,         GL_UNSIGNED_BYTE,   32, "BGRX typeless (as UNORM)", TF_TYPELESS | _TF_NOALPHA },
	// FIXME: kueken7_bgr8_srgb.dds doesn't load - but VS2022 doesn't load it either (at all)
	{ 0, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,   GL_SRGB_ALPHA, GL_BGRA,         GL_UNSIGNED_BYTE,   32, "BGRX8 SRGB UNORM", _TF_NOALPHA | TF_SRGB },

	// NOTE: the compressed BC6 and BC7 formats are handled in comprFormatTable[]

	// the following works with kueken7_rgba4_unorm.dds, but that test image is most probably wrong..
	//{ D3DFMT_A4R4G4B4, DXGI_FORMAT_B4G4R4A4_UNORM,  GL_RGBA,  GL_RGBA,  GL_UNSIGNED_SHORT_4_4_4_4,  16, "BGRA4" },
	// this is what it should be according to the KTX2 spec:
	{ D3DFMT_A4R4G4B4,
	     DXGI_FORMAT_B4G4R4A4_UNORM,        GL_RGBA4,      GL_BGRA,  GL_UNSIGNED_SHORT_4_4_4_4_REV, 16, "BGRA4" },

	// TODO: DXGI_FORMAT_R1_UNORM = 66, (1bit format?! I don't think OpenGL supports that?)
	// TODO: DXGI_FORMAT_R8G8_B8G8_UNORM (and the identical PIXEL_FMT_R8G8_B8G8)
	// TODO: DXGI_FORMAT_G8R8_G8B8_UNORM (and the identical PIXEL_FMT_G8R8_G8B8)

	// TODO: the remaining formats (DXGI_FORMAT_AYUV until DXGI_FORMAT_V408, except DXGI_FORMAT_B4G4R4A4_UNORM),
	//       which are all YUV and similar formats for video, I think?
};

// shortcut for RGBA masks
enum { DDPF_RGBA = DDPF_RGB | DDPF_ALPHAPIXELS };

struct MaskToDxFormat {
	uint32_t pfFlags; // pixelformat flags that must be set

	uint32_t bitsPerPixel;
	uint32_t rMask;
	uint32_t gMask;
	uint32_t bMask;
	uint32_t aMask;

	uint32_t pixelFmt; // D3DFMT_* / PIXEL_FMT_*
	int dxgiFormat;
};

// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide#common-dds-file-resource-formats-and-associated-header-content
const MaskToDxFormat maskToDxFormatTable[] = {
	// flags    bits,  red,         green,      blue,       alpha,        D3DFMT (PIXEL_FMT, "fourcc"), DXGI format
	{ DDPF_RGBA, 32,   0xff,        0xff00,     0xff0000,   0xff000000,   D3DFMT_A8B8G8R8,     DXGI_FORMAT_R8G8B8A8_UNORM },
	{ DDPF_RGBA, 32,   0xffff,      0xffff0000, 0,          0,            D3DFMT_G16R16,       DXGI_FORMAT_R16G16_UNORM },
	// yes, same as previous but without alpha bit (no idea, that's what it's like in microsofts table..)
	{ DDPF_RGB,  32,   0xffff,      0xffff0000, 0,          0,            D3DFMT_G16R16,       DXGI_FORMAT_R16G16_UNORM },

	// https://walbourn.github.io/dds-update-and-1010102-problems/
	// the next (alpha) mask looks especially wrong, but is from that DDS guide linked above
	{ DDPF_RGBA, 32,   0x3ff,       0xffc00,    0x3ff00000, 0,            D3DFMT_A2B10G10R10,  DXGI_FORMAT_R10G10B10A2_UNORM },
	{ DDPF_RGBA, 32,   0x3ff,       0xffc00,    0x3ff00000, 0xc0000000,   D3DFMT_A2B10G10R10,  DXGI_FORMAT_R10G10B10A2_UNORM },
	// the following would be correct, but apparently broken writers (MS D3DX) used those masks for D3DFMT_A2B10G10R10
	//{ DDPF_RGBA, 32, 0x3ff00000,  0xffc00,  0x3ff,      0xc0000000,   D3DFMT_A2R10G10B10, 0 },
	{ DDPF_RGBA, 32,   0x3ff00000,  0xffc00,    0x3ff,      0xc0000000,   D3DFMT_A2B10G10R10,  DXGI_FORMAT_R10G10B10A2_UNORM }, // also treated as DXGI_FORMAT_R10G10B10A2_UNORM

	{ DDPF_RGBA, 16,   0x7c00,      0x3e0,      0x1f,       0x8000,       D3DFMT_A1R5G5B5,     DXGI_FORMAT_B5G5R5A1_UNORM },
	{ DDPF_RGB,  16,   0x7c00,      0x3e0,      0x1f,       0,            D3DFMT_X1R5G5B5,     0 },
	{ DDPF_RGB,  16,   0xf800,      0x7e0,      0x1f,       0,            D3FMT_R5G6B5,        DXGI_FORMAT_B5G6R5_UNORM },
	{ DDPF_ALPHA, 8,   0,           0,          0,          0xff,         D3DFMT_A8,           DXGI_FORMAT_A8_UNORM },
	{ DDPF_RGBA, 32,   0xff0000,    0xff00,     0xff,       0xff000000,   D3DFMT_A8R8G8B8,     DXGI_FORMAT_B8G8R8A8_UNORM },
	{ DDPF_RGB,  32,   0xff0000,    0xff00,     0xff,       0,            D3DFMT_X8R8G8B8,     DXGI_FORMAT_B8G8R8X8_UNORM },
	{ DDPF_RGB,  32,   0xff,        0xff00,     0xff0000,   0,            D3DFMT_X8B8G8R8,     0 },
	{ DDPF_RGB,  24,   0xff0000,    0xff00,     0xff,       0,            D3DFMT_R8G8B8,       0 },
	// DG: I added the following
	{ DDPF_RGB,  24,   0xff,        0xff00,     0xff0000,   0,            D3DFMT_B8G8R8,       0 }, // D3DFMT_B8G8R8 is non-standard!
	{ DDPF_RGBA, 16,   0xf00,       0xf0,       0xf,        0xf000,       D3DFMT_A4R4G4B4,     0 },
	{ DDPF_RGBA, 16,   0xf00,       0xf0,       0xf,        0,            D3DFMT_X4R4G4B4,     0 },

	{ DDPF_RGBA, 16,   0xe0,        0x1c,       0x3,        0xff00,       D3DFMT_A8R3G3B2,  0 }, // TODO: no opengl equivalent

	{ DDPF_LUMINANCE, 16, 0xff,     0,          0,          0xff00,       D3DFMT_A8L8,         0 },
	{ DDPF_LUMINANCE, 16, 0xffff,   0,          0,          0,            D3DFMT_L16,          0 },
	// TODO: gli/data/kueken7_l8_unorm.dds doesn't load - but it *is* incomplete, even if VS2022 can display it.
	{ DDPF_LUMINANCE,  8, 0xff,     0,          0,          0,            D3DFMT_L8,           0 },

	{ DDPF_LUMINANCE,  8, 0x0f,     0,          0,          0xf0,         D3DFMT_A4L4,  0 }, // TODO: no opengl equivalent

	// TODO: D3DFMT_CxV8U8, whatever *that* is
	// TODO: what about PIXEL_FMT_R8G8_B8G8 and PIXEL_FMT_G8R8_G8B8 ?
	// TODO: what about PIXEL_FMT_UYVY and PIXEL_FMT_YUY2 ?
};


static ComprFormatInfo FindComprFormat(uint32_t fourcc, int dxgiFmt, uint32_t pixelFormatFlags, uint8_t dx10misc2)
{
	ComprFormatInfo ret = {};
	for(const ComprFormatInfo& fi : comprFormatTable) {
		if( fi.ddsFourCC == fourcc && fi.dxgiFormat == dxgiFmt
			&& (fi.pfFlags & pixelFormatFlags) == fi.pfFlags
			// dx10misc2 is most probably not set (legacy D3DX 10 and D3DX 11 libs
			// fail to load DDS files where it's not 0)
			// so only if it's set in both cases make sure they match
			&& (dx10misc2 == 0 || fi.dx10misc2 == 0 || dx10misc2 == fi.dx10misc2))
		{
			ret = fi;
			ret.ourFlags |= TF_COMPRESSED;
			break;
		}
	}

	return ret;
}

static UncomprFormatInfo FindUncomprFourCCFormat(uint32_t fourcc, int dxgiFmt)
{
	if(fourcc == DX10) // this table doesn't match that.
		fourcc = 0;
	UncomprFormatInfo ret = {};
	for(const UncomprFormatInfo& fi : uncomprFormatTable) {
		if( (dxgiFmt != 0 && fi.dxgiFormat == dxgiFmt)
		   || (fourcc != 0 && fi.ddsD3Dfmt == fourcc) ){
			ret = fi;
			break;
		}
	}

	return ret;
}

static UncomprFormatInfo FindUncomprFormat(const DDS_PIXELFORMAT& pf)
{
	uint32_t fourcc = 0;
	int dxgiFmt = 0;
	for(const MaskToDxFormat& mtd : maskToDxFormatTable) {
		static const uint32_t flagsToCheck = DDPF_ALPHA|DDPF_ALPHAPIXELS|DDPF_RGB|DDPF_LUMINANCE;
		if(pf.dwRGBBitCount == mtd.bitsPerPixel && (pf.dwFlags & flagsToCheck) == mtd.pfFlags) {
			if((mtd.pfFlags & (DDPF_ALPHAPIXELS|DDPF_ALPHA)) && mtd.aMask != pf.dwRGBAlphaBitMask)
				continue;
			if((mtd.pfFlags & DDPF_LUMINANCE) && mtd.rMask != pf.dwRBitMask)
				continue;
			if(mtd.pfFlags & DDPF_RGB) {
				if(mtd.rMask != pf.dwRBitMask || mtd.gMask != pf.dwGBitMask
				   || mtd.bMask != pf.dwBBitMask) {
					continue;
				}
			}
			// if we got this far, the masks must have matched
			fourcc = mtd.pixelFmt;
			dxgiFmt = mtd.dxgiFormat;
			break;
		}
	}
	if(fourcc != 0 || dxgiFmt != 0) {
		return FindUncomprFourCCFormat(fourcc, dxgiFmt);
	}
	UncomprFormatInfo ret = {0};
	return ret;
}

static uint32_t CalcSize(uint32_t w, uint32_t h, int32_t pitchTypeOrBitsPPixel)
{
	uint32_t size = 0;
	if( pitchTypeOrBitsPPixel > 0) {
		// if it's a positive value, it's one of the other formats
		// (the values in enum PitchType are <= 0)
		size = ((w * pitchTypeOrBitsPPixel + 7) / 8) * h; // TODO: really * h ?
	} else {
		switch(pitchTypeOrBitsPPixel) {
			case UNKNOWN:
				assert(0 && "why is no pitchType set?!");
				break;
			case BLOCK8: // DXT1, BC1, BC4
				size = std::max(1u, (w+3)/4) * std::max(1u, (h+3)/4) * 8;
				break;
			case BLOCK16: // other block-compressed formats
				size = std::max(1u, (w+3)/4) * std::max(1u, (h+3)/4) * 16;
				break;
			case WEIRD_LEGACY:
				// R8G8_B8G8, G8R8_G8B8, legacy UYVY-packed, and legacy YUY2-packed formats
				size = ((w+1) >> 1) * 4 * h; // TODO: really *h?
				break;
		}
	}
	assert(size > 0 && "calulated pitch is <= 0?!");
	return size;
}

// pass dxgiFmt = 0 if fourcc != DX10 !
static ASTCInfo FindASTCFormat(uint32_t fourcc, int dxgiFmt)
{
	ASTCInfo ret = {};
	for(const ASTCInfo& ai : astcFormatTable) {
		if(ai.ddsFourCC == fourcc && ai.dxgiFormat == dxgiFmt) {
			ret = ai;
			ret.ourFlags |= TF_COMPRESSED;
			break;
		}
	}
	return ret;
}

static uint32_t CalcASTCmipSize(uint32_t w, uint32_t h, uint32_t blockW, uint32_t blockH)
{
	// "ASTC textures are compressed using a fixed block size of 128 bits [16 bytes],
	//  but with a variable block footprint ranging from 4×4 texels up to 12×12 texels."
	return std::max(1u, (w+blockW-1)/blockW) * std::max(1u, (h+blockH-1)/blockH) * 16;
}

bool Texture::LoadDDS(MemMappedFile* mmf, const char* filename)
{
	const unsigned char* data = (const unsigned char*)mmf->data;
	const size_t len = mmf->length;
	const unsigned char* dataEnd = data + len;
	size_t dataOffset = 4 + sizeof(DDS_HEADER);

	const DDS_HEADER* header = (const DDS_HEADER*)(data+4); // skip magic number ("DDF ")
	const DDS_HEADER_DXT10* dx10header = nullptr;
	int w = header->dwWidth;
	int h = header->dwHeight;
	int numMips = header->dwMipMapCount;
	if(numMips <= 0)
		numMips = 1;
	uint32_t fourcc = header->ddpfPixelFormat.dwFourCC;
	uint32_t ourFlags = 0;
	int dxgiFmt = 0;
	uint8_t dx10misc2 = 0;
	bool foundFormat = false;
	if(fourcc == PIXEL_FMT_DX10) {
		if(len < 148) {
			errprintf("Invalid DDS file `%s`, says it has DX10 header but is only %d bytes!\n", filename, (int)len);
			return false;
		}
		dx10header = (const DDS_HEADER_DXT10*)(data + dataOffset);
		dataOffset += sizeof(DDS_HEADER_DXT10);
		dxgiFmt = dx10header->dxgiFormat;
		dx10misc2 = (dx10header->miscFlags2 & 7); // lowest 3 bits
	}
	// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide#dds-file-layout
	// says that pitch should be computed like:
	// for block-compressed formats: max( 1, ((width+3)/4) ) * block-size:
	//    (block-size = 8 for DXT1, BC1, BC4 formats, else 16)
	// for R8G8_B8G8, G8R8_G8B8, legacy UYVY-packed, and legacy YUY2-packed formats:
	//    ((width+1) >> 1) * 4
	// for other formats:
	//    ( width * bits-per-pixel + 7 ) / 8

	// TODO: fourcc == 0 for uncompressed textures specified by dwRGBBitCount and the masks
	// => might be easiest to decode them in code?
	// maybe I could even do the same for the uncompressed dxgi formats, if I build a table with masks etc for them?
	// would need sample-data though, to make sure I get the byte order right..
	// maybe helpful generally: https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#formatMapping
	ComprFormatInfo fmtInfo = {};
	ASTCInfo astcInfo = {};
	UncomprFormatInfo uncomprInfo = {};
	int32_t pitchTypeOrBitsPerPixel = 0; // set for everything except ASTC, see enum PitchType above
	textureFlags = 0;
	bool isASTC = false;
	if( (fourcc == PIXEL_FMT_DX10 && (unsigned)dxgiFmt >= DXGI_FORMAT_ASTC_4X4_TYPELESS
	                          && (unsigned)dxgiFmt <= DXGI_FORMAT_ASTC_12X12_UNORM_SRGB)
	   || (fourcc & PIXEL_FMT_FOURCC('A', 'S', 0, 0)) == PIXEL_FMT_FOURCC('A', 'S', 0, 0) ) // all ASTC fourccs start with 'AS'
	{
		astcInfo = FindASTCFormat(fourcc, dxgiFmt);
		if(astcInfo.glFormat != 0) {
			foundFormat = true;
			isASTC = true;
			dataFormat = astcInfo.glFormat;
			formatName = astcInfo.name;
			ourFlags = astcInfo.ourFlags;
		} else if(fourcc == PIXEL_FMT_DX10) {
			errprintf("Couldn't detect data format of '%s' - its dxgiFormat (%d) is in the ASTC-range, but apparently didn't match any actual format\n",
			          filename, dxgiFmt);
			return false;
		} // otherwise it was the "fourcc starts with 'AS'" case, for that also try the regular format table

	}
	if(!foundFormat) {
		fmtInfo = FindComprFormat(fourcc, dxgiFmt, header->ddpfPixelFormat.dwFlags, dx10misc2);
		// TODO: store fmtInfo in texture so it can be displayed? (REMEMBER MOVE ASSIGNMENT ETC!)

		if(fmtInfo.glFormat != 0) {
			foundFormat = true;
			dataFormat = fmtInfo.glFormat;
			formatName = fmtInfo.name;
			pitchTypeOrBitsPerPixel = fmtInfo.pitchTypeOrBitsPPixel;
			ourFlags = fmtInfo.ourFlags;
		}
	}
	if(!foundFormat) {
		// still not found? try uncompressed formats...
		if(fourcc != 0 && (fourcc > 0x01000000 || (header->ddpfPixelFormat.dwFlags & DDPF_FOURCC))) {
			// if fourcc is set, search the uncomprFourCC table.
			// I try to check for set flags as little as possible (because not all
			// writers set them correctly), but at least if a D3D_FMT number (the supported ones are <= 117)
			// is stored in the fourcc, that flag should be set (I guess the chance that
			// random garbage in header->ddpfPixelFormat.dwFourCC is a valid FourCC is way lower
			// than it being a number?)
			uncomprInfo = FindUncomprFourCCFormat(fourcc, dxgiFmt);
		}
		if( uncomprInfo.glFormat == 0
		   && (header->ddpfPixelFormat.dwFlags & (DDPF_ALPHA|DDPF_ALPHAPIXELS|DDPF_RGB|DDPF_LUMINANCE)) != 0 )
		{
			uncomprInfo = FindUncomprFormat(header->ddpfPixelFormat);
		}

		if(uncomprInfo.glFormat != 0) {
			foundFormat = true;
			dataFormat = uncomprInfo.glIntFormat;
			glFormat = uncomprInfo.glFormat;
			glType = uncomprInfo.glType;
			formatName = uncomprInfo.name;
			pitchTypeOrBitsPerPixel = uncomprInfo.bitsPerPixel;
			ourFlags = uncomprInfo.ourFlags;
		}

		if(header->dwFlags & DDSD_PITCH) {
			// TODO: use header->lPitch ? (how) does it work with mipmaps?
		}
	}
	if(!foundFormat) {
		char fccstr[5] = { char(fourcc & 0xff), char((fourcc >> 8) & 0xff),
		                   char((fourcc >> 16) & 0xff), char((fourcc >> 24) & 0xff), 0 };
		errprintf( "Couldn't detect data format of '%s' - FourCC: 0x%x ('%s' %d) dxgiFormat: %d\n",
		           filename, fourcc, fccstr, fourcc, dxgiFmt );
		return false;
	}
	formatName.insert(0, "DDS ");

	if(dx10misc2 != 0) {
		if(dx10misc2 == DDS_DX10MISC2_ALPHA_OPAQUE)
			ourFlags |= _TF_NOALPHA;
		else if( (ourFlags & _TF_NOALPHA) == 0
		        && dx10misc2 == DDS_DX10MISC2_ALPHA_PREMULTIPLIED )
			ourFlags |= TF_PREMUL_ALPHA;
	}
	// FIXME: also use DDPF_ALPHA(PIXELS) ?
	if((ourFlags & _TF_NOALPHA) == 0 && dg_glInternalFormatHasAlpha(dataFormat)) {
		ourFlags |= TF_HAS_ALPHA;
	}
	ourFlags &= ~_TF_NOALPHA; // clear that flag, it's only for the tables

	bool isCubemap = (header->dwCaps2 & DDSCAPS2_CUBEMAP_MASK) != 0;
	int numCubeFaces = 0;
	if(isCubemap) {
		// DDSCAPS2_CUBEMAP_POSITIVEX = 0x400 (1<<10), TF_CUBEMAP_XPOS = 1u << 26
		// (and the order of the bits for the other faces is identical)
		// => leftshift the DDS mask by 16 to translate
		ourFlags |= uint32_t(header->dwCaps2 & DDSCAPS2_CUBEMAP_MASK) << 16;
		numCubeFaces = NumBitsSet(header->dwCaps2 & DDSCAPS2_CUBEMAP_MASK);
	} else if(dx10header != nullptr && (dx10header->miscFlag & DDS_DX10MISC_TEXTURECUBE)) {
		// if I understand correctly, DX10 DDS files with cubemaps always have all 6 faces
		isCubemap = true;
		numCubeFaces = 6;
		ourFlags |= TF_CUBEMAP_MASK;
	}

	int numElements = 1;
	if(dx10header != nullptr && (dx10header->arraySize > 1)) {
		numElements = dx10header->arraySize;
		ourFlags |= TF_IS_ARRAY;
		glTarget = isCubemap ? GL_TEXTURE_CUBE_MAP_ARRAY : GL_TEXTURE_2D_ARRAY;
	} else {
		glTarget = isCubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
	}
	if(numCubeFaces > 1) {
		numElements *= numCubeFaces;
	}

	textureFlags = ourFlags;

	name = filename;
	fileType = FT_DDS;
	texData = mmf;
	texDataFreeFun = [](void* texData, intptr_t) -> void { UnloadMemMappedFile( (MemMappedFile*)texData ); };

	const unsigned char* dataCur = data + dataOffset;
	elements.resize(numElements);
	for(int e=0; e < numElements; ++e) {
		std::vector<MipLevel>& mipLevels = elements[e];
		mipLevels.reserve(numMips);

		int mipW = w;
		int mipH = h;
		for(int i=0; i < numMips; ++i) {
			uint32_t mipSize;
			if(!isASTC) {
				mipSize = CalcSize(mipW, mipH, pitchTypeOrBitsPerPixel);
			} else {
				mipSize = CalcASTCmipSize(mipW, mipH, astcInfo.blockW, astcInfo.blockH);
			}
			const unsigned char* dataNext = dataCur + mipSize;
			if(dataNext > dataEnd) {
				errprintf("MipMap level %d for '%s' is incomplete (file too small, %u bytes left, are at %u bytes from start) mipSize: %u w: %d h: %d!\n",
						i, filename, unsigned(dataEnd - dataCur), unsigned(dataCur-data), mipSize, mipW, mipH);
				return (i > 0); // if we loaded at least one mipmap we can display the file despite the error
			}
			mipLevels.push_back( MipLevel(mipW, mipH, dataCur, mipSize) );
			if(mipW == 1 && mipH == 1 && i < numMips-1) {
				errprintf( "Texture '%s' claimed to have %d MipMap levels, but we're already done after %d levels\n", filename, numMips, i+1 );
				// don't break, I think - because for texture arrays it's important
				// to read (skip forward) as much data as all specified mips need
				// so the next mip level 0 starts at the right position in data
				//break;
			}
			dataCur = dataNext;
			mipW = std::max( mipW / 2, 1 );
			mipH = std::max( mipH / 2, 1 );
		}
	}

	return true;
}

} //namespace texview
