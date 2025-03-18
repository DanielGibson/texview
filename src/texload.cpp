
#include "libs/stb_image.h"
#include <glad/gl.h>

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
	formatName = nullptr;
	mipLevels.clear();
	if(glTextureHandle > 0) {
		glDeleteTextures(1, &glTextureHandle);
		glTextureHandle = 0;
	}
	if(texDataFreeFun != nullptr) {
		texDataFreeFun( (void*)texData, texDataFreeCookie );
		texDataFreeFun = nullptr;
		texDataFreeCookie = 0;
	}
	glFormat = glType = 0;
	texData = nullptr;

	name.clear();
	fileType = dataFormat = 0;
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

bool Texture::CreateOpenGLtexture()
{
	if(glTextureHandle != 0) {
		glDeleteTextures(1, &glTextureHandle);
		glTextureHandle = 0;
	}
	glGenTextures(1, &glTextureHandle);
	glBindTexture(GL_TEXTURE_2D, glTextureHandle);

	GLenum internalFormat = dataFormat;
	int numMips = (int)mipLevels.size();

	glGetError();
	bool anySuccess = false;

	for(int i=0; i<numMips; ++i) {
		if(formatIsCompressed) {
			glCompressedTexImage2D(GL_TEXTURE_2D, i, internalFormat,
			                       mipLevels[i].width, mipLevels[i].height,
			                       0, mipLevels[i].size, mipLevels[i].data);
			GLenum e = glGetError();
			if(e != GL_NO_ERROR) {
				errprintf("Sending data from '%s' for mipmap level %d to the GPU with glCompressedTexImage2D() failed. "
				          "Probably your GPU/driver doesn't support '%s' compression (glGetError() says '%s')\n",
				          name.c_str(), i, formatName, getGLerrorString(e));
			} else { // probably better than nothing if at least *some* mipmap level has been loaded
				anySuccess = true;
			}
		} else {
			// FIXME: for other non-compressed formats we'll need internalFormat *and* externalFormat
			//        and maybe also type (GL_UNSIGNED_BYTE or whatever)!
			GLenum format = glFormat;
			GLenum type = glType;
			glTexImage2D(GL_TEXTURE_2D, i, internalFormat, mipLevels[i].width,
			              mipLevels[i].height, 0, format, type,
			              mipLevels[i].data);
			GLenum e = glGetError();
			if(e != GL_NO_ERROR) {
				errprintf("Sending data from '%s' for mipmap level %d to the GPU with glTexImage2D() failed. "
				          "glGetError() says '%s'\n", name.c_str(), i, getGLerrorString(e));
			} else {
				anySuccess = true;
			}
		}
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

	// some other kind of file, try throwing it at stb_image
	if(mmf->length > INT_MAX) {
		errprintf("File '%s' is too big to load with stb_image\n", filename);
		UnloadMemMappedFile(mmf);
		return false;
	}
	int w, h, comp;
	unsigned char* pix;
	pix = stbi_load_from_memory((const unsigned char*)mmf->data,
								(int)mmf->length, &w, &h, &comp, STBI_rgb_alpha);

	if(pix != nullptr) {
		// mmf is not needed anymore, decoded image data is in pix
		UnloadMemMappedFile(mmf);
		mmf = nullptr;

		name = filename;
		fileType = 0; // TODO
		dataFormat = GL_RGBA;
		glFormat = GL_RGBA;
		glType = GL_UNSIGNED_BYTE;
		formatName = "STB"; // TODO
		texData = pix;
		texDataFreeFun = [](void* texData, intptr_t) -> void { stbi_image_free(texData); };

		mipLevels.push_back( Texture::MipLevel(w, h, pix) );

		return true;
	}

	// TODO: anything else to try?

	errprintf("Couldn't load '%s', maybe the filetype is unsupported?\n", filename);
	UnloadMemMappedFile(mmf);
	return false;
}

static_assert(sizeof(DDS_HEADER) == 124, "DDS header somehow has wrong size");
static_assert(sizeof(DDS_PIXELFORMAT) == 32, "DDS_PIXELFORMAT has wrong size");
static_assert(sizeof(DDS_HEADER_DXT10) == 20, "DDS_HEADER_DXT10 has wrong size");

enum PitchType {
	UNKNOWN = 0,
	BLOCK8 = -1,  // DXT1, BC1, BC4
	BLOCK16 = -2, // other block-compressed formats (like BC2/3/5/6/7)
	WEIRD_LEGACY = -3 // R8G8_B8G8, G8R8_G8B8, legacy UYVY-packed, and legacy YUY2-packed formats
};

enum OurFlags : uint8_t {
	OF_NONE = 0,
	OF_SRGB = 1,
	OF_TYPELESS = 2,
	OF_PREMUL_ALPHA = 4,
	OF_COMPRESSED = 8, // not set in the table but based on which table it's in
	OF_NOALPHA = 16, // formats that use GL_RGBA or similar, but are RGBX (or similar)
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
	{ PIXEL_FMT_DXT2,      0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, BLOCK16, "DXT2 (BC2 alpha premul)", 0, 0, OF_PREMUL_ALPHA }, // but alpha premultiplied
	{ PIXEL_FMT_DXT5,      0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT5 (BC3)" },
	{ PIXEL_FMT_DXT4,      0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, BLOCK16, "DXT4 (BC3 alpha premul)", 0, 0, OF_PREMUL_ALPHA }, // but alpha premultiplied
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
	{ DX10, DXGI_FORMAT_BC1_UNORM_SRGB, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, BLOCK8,  "BC1 (DXT1) sRGB opaque", 0, DDS_DX10MISC2_ALPHA_OPAQUE, OF_SRGB },
	{ DX10, DXGI_FORMAT_BC1_UNORM_SRGB, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, BLOCK8,  "BC1 (DXT1) sRGB" },
	// TODO: what are those typeless formats good for? are they used in files or only for buffers?
	{ DX10, DXGI_FORMAT_BC1_TYPELESS,   GL_COMPRESSED_RGB_S3TC_DXT1_EXT,  BLOCK8,  "BC1 (DXT1) typeless opaque", 0, DDS_DX10MISC2_ALPHA_OPAQUE, OF_TYPELESS },
	{ DX10, DXGI_FORMAT_BC1_TYPELESS,   GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, BLOCK8,  "BC1 (DXT1) typeless" },

	{ DX10, DXGI_FORMAT_BC2_UNORM,      GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,       BLOCK16, "BC2 (DXT3)" },
	{ DX10, DXGI_FORMAT_BC2_UNORM_SRGB, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, BLOCK16, "BC2 (DXT3) sRGB", 0, 0, OF_SRGB },
	{ DX10, DXGI_FORMAT_BC2_TYPELESS,   GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,       BLOCK16, "BC2 (DXT3) typeless", 0, 0, OF_TYPELESS },

	{ DX10, DXGI_FORMAT_BC3_UNORM,      GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,       BLOCK16, "BC3 (DXT5)" },
	{ DX10, DXGI_FORMAT_BC3_UNORM_SRGB, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, BLOCK16, "BC3 (DXT5) sRGB", 0, 0, OF_SRGB },
	{ DX10, DXGI_FORMAT_BC3_TYPELESS,   GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,       BLOCK16, "BC3 (DXT5) typeless", 0, 0, OF_TYPELESS },

	// TODO: could also use GL_COMPRESSED_LUMINANCE_LATC1_EXT and GL_COMPRESSED_SIGNED_LUMINANCE_LATC1_EXT
	{ DX10, DXGI_FORMAT_BC4_UNORM,    GL_COMPRESSED_RED_RGTC1_EXT,        BLOCK8, "BC4U (ATI1n/3Dc+/RGTC1)" },
	{ DX10, DXGI_FORMAT_BC4_SNORM,    GL_COMPRESSED_SIGNED_RED_RGTC1_EXT, BLOCK8, "BC4S (ATI1n/3Dc+/RGTC1)" },
	{ DX10, DXGI_FORMAT_BC4_TYPELESS, GL_COMPRESSED_RED_RGTC1_EXT,        BLOCK8, "BC4  (ATI1n/3Dc+/RGTC1) typeless", 0, 0, OF_TYPELESS },
	// TODO: could also use GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT and GL_COMPRESSED_SIGNED_LUMINANCE_ALPHA_LATC2_EXT
	{ DX10, DXGI_FORMAT_BC5_UNORM,    GL_COMPRESSED_RED_GREEN_RGTC2_EXT,     BLOCK16, "BC5U (ATI1n/3Dc+/RGTC2)" },
	{ DX10, DXGI_FORMAT_BC5_SNORM, GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT, BLOCK16, "BC5S (ATI1n/3Dc+)" },
	{ DX10, DXGI_FORMAT_BC5_TYPELESS, GL_COMPRESSED_RED_GREEN_RGTC2_EXT,     BLOCK16, "BC5  (ATI1n/3Dc+) typeless" },

	// BC6, BC7
	{ DX10, DXGI_FORMAT_BC6H_SF16,      GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB,   BLOCK16, "BC6S (BPTC HDR)" },
	{ DX10, DXGI_FORMAT_BC6H_UF16,      GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB, BLOCK16, "BC6U (BPTC HDR)" },
	{ DX10, DXGI_FORMAT_BC6H_TYPELESS,  GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB, BLOCK16, "BC6  (BPTC HDR) typeless", 0, 0, OF_TYPELESS },
	{ PIXEL_FMT_BC6H,   0,              GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB, BLOCK16, "BC6U (BPTC HDR)" },

	{ DX10, DXGI_FORMAT_BC7_UNORM,      GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,         BLOCK16, "BC7 (BPTC)" },
	{ DX10, DXGI_FORMAT_BC7_UNORM_SRGB, GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB,   BLOCK16, "BC7 SRGB (BPTC)" },
	{ DX10, DXGI_FORMAT_BC7_TYPELESS,   GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,         BLOCK16, "BC7 (BPTC) typeless", 0, 0, OF_TYPELESS },
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
	uint8_t ourFlags; // OF_SRGB, OF_TYPELESS
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
		W, H, OF_TYPELESS, "ASTC " #W "x" #H " typeless" }, \
	{ DX10, DXGI_FORMAT_ASTC_ ## W ## X ## H ## _UNORM, GL_COMPRESSED_RGBA_ASTC_ ## W ## x ## H ## _KHR, \
		W, H, 0, "ASTC " #W "x" #H " UNORM" }, \
	{ PIXEL_FMT_ASTC_ ## W ## x ## H, 0, GL_COMPRESSED_RGBA_ASTC_ ## W ## x ## H ## _KHR, \
		W, H, 0, "ASTC " #W "x" #H " UNORM" }, \
	{ DX10, DXGI_FORMAT_ASTC_ ## W ## X ## H ## _UNORM_SRGB, GL_COMPRESSED_SRGB8_ALPHA8_ASTC_ ## W ## x ## H ## _KHR, \
		W, H, OF_SRGB, "ASTC " #W "x" #H " UNORM SRGB" },

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

struct UncomprFourCCFormatInfo {
	uint32_t ddsFourCC;
	int dxgiFormat;

	uint32_t glIntFormat;
	uint32_t glFormat;
	uint32_t glType;
	uint32_t bitsPerPixel;

	const char* name;

	uint8_t ourFlags;
};

// uncompressed formats that can be identified based on FOURCC or DGXI format
const UncomprFourCCFormatInfo uncomprFourccTable[] = {
	// TODO: what about PIXEL_FMT_R8G8_B8G8 and PIXEL_FMT_G8R8_G8B8 ?
	// TODO: what about PIXEL_FMT_UYVY and PIXEL_FMT_YUY2 ?

	// using the generic internal format for now instead of the ones exactly matching the format
	{ PIXEL_FMT_A16B16G16R16,  0, GL_RGBA/*16*/,       GL_RGBA, GL_UNSIGNED_SHORT, 64,  "RGBA16 UNORM" }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ PIXEL_FMT_Q16W16V16U16,  0, GL_RGBA/*16_SNORM*/, GL_RGBA, GL_SHORT,          64,  "RGBA16 SNORM" }, // DXGI_FORMAT_R16G16B16A16_SNORM
	{ PIXEL_FMT_R16F,          0, GL_RED/*R16F*/,      GL_RED,  GL_HALF_FLOAT,     16,  "Red16 FLOAT" }, // DXGI_FORMAT_R16_FLOAT - TODO: GL_HALF_FLOAT really ok as type?
	{ PIXEL_FMT_G16R16F,       0, GL_RG,               GL_RG,   GL_HALF_FLOAT,     32,  "RG16 FLOAT" }, // DXGI_FORMAT_R16G16_FLOAT
	{ PIXEL_FMT_A16B16G16R16F, 0, GL_RGBA,             GL_RGBA, GL_HALF_FLOAT,     64,  "RGBA16 FLOAT" }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ PIXEL_FMT_R32F,          0, GL_RED/*R32F*/,      GL_RED,  GL_FLOAT,          32,  "Red32 FLOAT" }, // DXGI_FORMAT_R32_FLOAT
	{ PIXEL_FMT_G32R32F,       0, GL_RG,               GL_RG,   GL_FLOAT,          64,  "RG32 FLOAT" }, // DXGI_FORMAT_R32G32_FLOAT
	{ PIXEL_FMT_A32B32G32R32F, 0, GL_RGBA,             GL_RGBA, GL_FLOAT,          128, "RGBA32 FLOAT" }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	// TODO: PIXEL_FMT_CxV8U8, whatever *that* is

	// all the DXGI formats... https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format
	// extremely helpful: https://github.khronos.org/KTX-Specification/ktxspec.v2.html#formatMapping
	// probably also  helpful: https://gist.github.com/Kos/4739337

	{ DX10, DXGI_FORMAT_R32G32B32A32_TYPELESS, GL_RGBA, GL_RGBA_INTEGER, GL_UNSIGNED_INT,  128, "RGBA32 typeless", OF_TYPELESS }, // TODO: what type for typeless?
	{ DX10, DXGI_FORMAT_R32G32B32A32_FLOAT,    GL_RGBA, GL_RGBA,         GL_FLOAT,         128, "RGBA32 FLOAT" },
	{ DX10, DXGI_FORMAT_R32G32B32A32_UINT,     GL_RGBA, GL_RGBA_INTEGER, GL_UNSIGNED_INT,  128, "RGBA32 UINT" },
	{ DX10, DXGI_FORMAT_R32G32B32A32_SINT,     GL_RGBA, GL_RGBA_INTEGER, GL_INT,           128, "RGBA32 SINT" },

	{ DX10, DXGI_FORMAT_R32G32B32_TYPELESS,    GL_RGB,  GL_RGB_INTEGER,  GL_UNSIGNED_INT,   96, "RGB32 typeless", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R32G32B32_FLOAT,       GL_RGB,  GL_RGB,          GL_FLOAT,          96, "RGB32 FLOAT" },
	{ DX10, DXGI_FORMAT_R32G32B32_UINT,        GL_RGB,  GL_RGB_INTEGER,  GL_UNSIGNED_INT,   96, "RGB32 UINT" },
	{ DX10, DXGI_FORMAT_R32G32B32_SINT,        GL_RGB,  GL_RGB_INTEGER,  GL_INT,            96, "RGB32 SINT" },

	{ DX10, DXGI_FORMAT_R16G16B16A16_TYPELESS, GL_RGBA, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 64, "RGBA16 typeless", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R16G16B16A16_FLOAT,    GL_RGBA, GL_RGBA,         GL_HALF_FLOAT,     64, "RGBA16 FLOAT" },
	{ DX10, DXGI_FORMAT_R16G16B16A16_UNORM,    GL_RGBA, GL_RGBA,         GL_UNSIGNED_SHORT, 64, "RGBA16 UNORM" },
	{ DX10, DXGI_FORMAT_R16G16B16A16_UINT,     GL_RGBA, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 64, "RGBA16 UINT" },
	{ DX10, DXGI_FORMAT_R16G16B16A16_SNORM,    GL_RGBA, GL_RGBA,         GL_SHORT,          64, "RGBA16 SNORM" },
	{ DX10, DXGI_FORMAT_R16G16B16A16_SINT,     GL_RGBA, GL_RGBA_INTEGER, GL_SHORT,          64, "RGBA16 SINT" },

	{ DX10, DXGI_FORMAT_R32G32_TYPELESS,       GL_RG,   GL_RG_INTEGER,   GL_UNSIGNED_INT,   64, "RG32 typeless", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R32G32_FLOAT,          GL_RG,   GL_RG,           GL_FLOAT,          64, "RG32 FLOAT" },
	{ DX10, DXGI_FORMAT_R32G32_UINT,           GL_RG,   GL_RG_INTEGER,   GL_UNSIGNED_INT,   64, "RG32 UINT" },
	{ DX10, DXGI_FORMAT_R32G32_SINT,           GL_RG,   GL_RG_INTEGER,   GL_INT,            64, "RG32 SINT" },

	// the next one is the only useful out of the following four, I guess
	{ DX10, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,     GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64, "Depth32 FLOAT Stencil8 UINT" },
	{ DX10, DXGI_FORMAT_R32G8X24_TYPELESS,        GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64, "R32G8X24_TYPELESS", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64, "R32_FLOAT_X8X24_TYPELESS", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,  GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64, "X32_TYPELESS_G8X24_UINT", OF_TYPELESS },

	// R in least significant bits, little endian order
	// ktx2 spec says GL_RGBA and ..._REV is correct
	{ DX10, DXGI_FORMAT_R10G10B10A2_TYPELESS, GL_RGBA, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV,  32, "RGB10A2 typeless", OF_TYPELESS },
	// FIXME: gli/data/kueken7_rgb10a2_unorm.dds looks wrong, and it works with GL_UNSIGNED_INT_10_10_10_2,
	//        but according to the KTX2 spec GL_UNSIGNED_INT_2_10_10_10_REV should be correct, so the image might be wrong?
	//        => yes, also looks wrong in visual studio (same as the other gli 1010102 image)
	{ DX10, DXGI_FORMAT_R10G10B10A2_UNORM,    GL_RGBA, GL_RGBA,         GL_UNSIGNED_INT_2_10_10_10_REV,  32, "RGB10A2 UNORM" },
	{ DX10, DXGI_FORMAT_R10G10B10A2_UINT,     GL_RGBA, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV,  32, "RGB10A2 UINT" },

	{ DX10, DXGI_FORMAT_R11G11B10_FLOAT,      GL_RGB,  GL_RGB,          GL_UNSIGNED_INT_10F_11F_11F_REV, 32, "RG11B10 FLOAT" }, //GL_R11F_G11F_B10F

	{ DX10, DXGI_FORMAT_R8G8B8A8_TYPELESS,    GL_RGBA,       GL_RGBA_INTEGER, GL_UNSIGNED_BYTE,   32, "RGBA8 typeless", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R8G8B8A8_UNORM,       GL_RGBA,       GL_RGBA,         GL_UNSIGNED_BYTE,   32, "RGBA8 UNORM" },
	{ DX10, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,  GL_SRGB_ALPHA, GL_RGBA,         GL_UNSIGNED_BYTE,   32, "RGBA8 UNORM SRGB", OF_SRGB },
	{ DX10, DXGI_FORMAT_R8G8B8A8_UINT,        GL_RGBA,       GL_RGBA_INTEGER, GL_UNSIGNED_BYTE,   32, "RGBA8 UINT" },
	{ DX10, DXGI_FORMAT_R8G8B8A8_SNORM,       GL_RGBA,       GL_RGBA,         GL_BYTE,            32, "RGBA8 SNORM" },
	{ DX10, DXGI_FORMAT_R8G8B8A8_SINT,        GL_RGBA,       GL_RGBA_INTEGER, GL_BYTE,            32, "RGBA8 SINT" },

	{ DX10, DXGI_FORMAT_R16G16_TYPELESS,      GL_RG,         GL_RG_INTEGER,   GL_UNSIGNED_SHORT,  32, "RG16 typeless", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R16G16_FLOAT,         GL_RG,         GL_RG,           GL_HALF_FLOAT,      32, "RG16 FLOAT" },
	{ DX10, DXGI_FORMAT_R16G16_UNORM,         GL_RG,         GL_RG,           GL_UNSIGNED_SHORT,  32, "RG16 UNORM" },
	{ DX10, DXGI_FORMAT_R16G16_UINT,          GL_RG,         GL_RG_INTEGER,   GL_UNSIGNED_SHORT,  32, "RG16 UINT" },
	{ DX10, DXGI_FORMAT_R16G16_SNORM,         GL_RG,         GL_RG,           GL_SHORT,           32, "RG16 SNORM" },
	{ DX10, DXGI_FORMAT_R16G16_SINT,          GL_RG,         GL_RG_INTEGER,   GL_SHORT,           32, "RG16 UINT" },

	{ DX10, DXGI_FORMAT_R32_TYPELESS,         GL_RED,        GL_RED_INTEGER,  GL_UNSIGNED_INT,    32, "Red32 typeless", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_D32_FLOAT,     GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_FLOAT,          32, "Depth32 FLOAT" },
	{ DX10, DXGI_FORMAT_R32_FLOAT,            GL_RED,        GL_RED,          GL_FLOAT,           32, "Red32 FLOAT" },
	{ DX10, DXGI_FORMAT_R32_UINT,             GL_RED,        GL_RED_INTEGER,  GL_UNSIGNED_INT,    32, "Red32 UINT" },
	{ DX10, DXGI_FORMAT_R32_SINT,             GL_RED,        GL_RED_INTEGER,  GL_INT,             32, "Red32 SINT" },

	// the next one is probably again the only useful one of the next 4
	{ DX10, DXGI_FORMAT_D24_UNORM_S8_UINT,     GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32, "Depth24 UNORM Stencil8 UINT" },
	{ DX10, DXGI_FORMAT_R24G8_TYPELESS,        GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32, "R24G8_TYPELESS", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32, "R24_UNORM_X8_TYPELESS", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_X24_TYPELESS_G8_UINT,  GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32, "X24_TYPELESS_G8_UINT", OF_TYPELESS },

	{ DX10, DXGI_FORMAT_R8G8_TYPELESS,        GL_RG,         GL_RG_INTEGER,   GL_UNSIGNED_SHORT,  16, "RG8 typeless", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R8G8_UNORM,           GL_RG,         GL_RG,           GL_UNSIGNED_SHORT,  16, "RG8 UNORM" },
	{ DX10, DXGI_FORMAT_R8G8_UINT,            GL_RG,         GL_RG_INTEGER,   GL_UNSIGNED_SHORT,  16, "RG8 UINT" },
	{ DX10, DXGI_FORMAT_R8G8_SNORM,           GL_RG,         GL_RG,           GL_SHORT,           16, "RG8 SNORM" },
	{ DX10, DXGI_FORMAT_R8G8_SINT,            GL_RG,         GL_RG_INTEGER,   GL_SHORT,           16, "RG8 SINT" },

	{ DX10, DXGI_FORMAT_R16_FLOAT,            GL_RED,        GL_RED,          GL_HALF_FLOAT,      16, "Red16 FLOAT" },
	{ DX10, DXGI_FORMAT_D16_UNORM,    GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,  16, "Depth16 UNORM" },
	{ DX10, DXGI_FORMAT_R16_UNORM,           GL_RED,         GL_RED,          GL_UNSIGNED_SHORT,  16, "Red16 UNORM" },
	// TODO: gli/data/kueken7_r16_unorm.dds (which is really UINT) is black
	// FIXME: apparently for (all?) _INTEGER formats one needs to use a sized type as internal format
	{ DX10, DXGI_FORMAT_R16_UINT,            GL_R16UI,       GL_RED_INTEGER,  GL_UNSIGNED_SHORT,  16, "Red16 UINT" },
	{ DX10, DXGI_FORMAT_R16_SNORM,           GL_RED,         GL_RED,          GL_SHORT,           16, "Red16 SNORM" },
	{ DX10, DXGI_FORMAT_R16_SINT,            GL_RED,         GL_RED_INTEGER,  GL_SHORT,           16, "Red16 SINT" },

	{ DX10, DXGI_FORMAT_R8_TYPELESS,         GL_RED,        GL_RED_INTEGER,   GL_UNSIGNED_BYTE,    8, "Red8 typeless", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_R8_UNORM,            GL_RED,        GL_RED,           GL_UNSIGNED_BYTE,    8, "Red8 UNORM" },
	// FIXME: looks like I have to use the sized internal formats after all? at least for some formats?
	// FIXME gli/data/kueken7_r8_uint.dds doesn't load
	{ DX10, DXGI_FORMAT_R8_UINT,             GL_R8UI /*GL_RED*/,        GL_RED_INTEGER,   GL_UNSIGNED_BYTE,    8, "Red8 UINT" },
	{ DX10, DXGI_FORMAT_R8_SNORM,            GL_RED,        GL_RED,           GL_BYTE,             8, "Red8 SNORM" },
	{ DX10, DXGI_FORMAT_R8_SINT,             GL_RED,        GL_RED_INTEGER,   GL_BYTE,             8, "Red8 SINT" }, // FIXME: gli/data/kueken7_r8_sint.dds doesn't load
	{ DX10, DXGI_FORMAT_A8_UNORM,            GL_ALPHA,      GL_ALPHA,         GL_UNSIGNED_BYTE,    8, "Alpha8 UNORM" },

	// TODO: DXGI_FORMAT_R1_UNORM = 66, (1bit format?! I don't think OpenGL supports that?)

	{ DX10, DXGI_FORMAT_R9G9B9E5_SHAREDEXP,  GL_RGB,        GL_RGB,   GL_UNSIGNED_INT_5_9_9_9_REV, 32, "RGB9 E5 shared exp float" },

	// TODO: DXGI_FORMAT_R8G8_B8G8_UNORM (and the identical PIXEL_FMT_R8G8_B8G8)
	// TODO: DXGI_FORMAT_G8R8_G8B8_UNORM (and the identical PIXEL_FMT_G8R8_G8B8)

	// NOTE: the compressed BC1-BC5 formats are handled in comprFormatTable[]

	{ DX10, DXGI_FORMAT_B5G6R5_UNORM,        GL_RGB,        GL_RGB,     GL_UNSIGNED_SHORT_5_6_5, 16, "RGB565 UNORM" },
	{ DX10, DXGI_FORMAT_B5G5R5A1_UNORM,      GL_RGBA,       GL_BGRA,    GL_UNSIGNED_SHORT_1_5_5_5_REV, 16, "RGB5A1 UNORM" },

	// TODO: { DX10, DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, GL_RGBA, GL_BGRA,   GL_UNSIGNED_INT_2_10_10_10_REV, 32, "BGRA" }, ???

	{ DX10, DXGI_FORMAT_B8G8R8A8_UNORM,      GL_RGBA,       GL_BGRA,    GL_UNSIGNED_BYTE, 32, "BGRA8 UNORM" },
	{ DX10, DXGI_FORMAT_B8G8R8X8_UNORM,      GL_RGBA,       GL_BGRA,    GL_UNSIGNED_BYTE, 32, "BGRX8 UNORM", OF_NOALPHA },
	{ DX10, DXGI_FORMAT_B8G8R8A8_TYPELESS,   GL_RGBA,       GL_BGRA,    GL_UNSIGNED_BYTE, 32, "BGRA typeless (as UNORM)", OF_TYPELESS },
	{ DX10, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, GL_SRGB_ALPHA, GL_BGRA,    GL_UNSIGNED_BYTE, 32, "BGRA8 SRGB UNORM", OF_SRGB },
	{ DX10, DXGI_FORMAT_B8G8R8X8_TYPELESS,   GL_RGBA,       GL_BGRA,    GL_UNSIGNED_BYTE, 32, "BGRX typeless (as UNORM)", OF_TYPELESS | OF_NOALPHA },
	// FIXME: kueken7_bgr8_srgb.dds doesn't load - but VS2022 doesn't load it either (at all)
	{ DX10, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, GL_SRGB_ALPHA, GL_BGRA,    GL_UNSIGNED_BYTE, 32, "BGRX8 SRGB UNORM", OF_NOALPHA | OF_SRGB },

	// NOTE: the compressed BC6 and BC7 formats are handled in comprFormatTable[]

	{ DX10, DXGI_FORMAT_B4G4R4A4_UNORM,  GL_RGBA/*4*/,  GL_BGRA,  GL_UNSIGNED_SHORT_4_4_4_4_REV,  16, "BGRA4" },

	// TODO: the remaining formats (DXGI_FORMAT_AYUV until DXGI_FORMAT_V408, except DXGI_FORMAT_B4G4R4A4_UNORM),
	//       which are all YUV and similar formats for video, I think?
};

struct UncomprFormatInfo {
	uint32_t pfFlags; // pixelformat flags that must be set

	uint32_t bitsPerPixel;
	uint32_t rMask;
	uint32_t gMask;
	uint32_t bMask;
	uint32_t aMask;

	uint32_t glIntFormat;
	uint32_t glFormat;
	uint32_t glType;

	const char* name;

	uint8_t ourFlags;
};

// shortcut for RGBA masks
enum { DDPF_RGBA = DDPF_RGB | DDPF_ALPHAPIXELS };

// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide#common-dds-file-resource-formats-and-associated-header-content
const UncomprFormatInfo uncomprMaskTable[] = {
	// flags    bits, red,        green,      blue,       alpha
	{ DDPF_RGBA, 32, 0xff,        0xff00,     0xff0000,   0xff000000,   GL_RGBA,  GL_RGBA,  GL_UNSIGNED_BYTE,  "RGBA8 UNORM" },
	{ DDPF_RGBA, 32, 0xffff,      0xffff0000, 0,          0,            GL_RG,    GL_RG,    GL_UNSIGNED_SHORT, "RG16 UNORM" },
	{ DDPF_RGB,  32, 0xffff,      0xffff0000, 0,          0,            GL_RG,    GL_RG,    GL_UNSIGNED_SHORT, "RG16 UNORM" },
	// https://walbourn.github.io/dds-update-and-1010102-problems/
	{ DDPF_RGBA, 32, 0x3ff,       0xffc00,    0x3ff00000, 0,            GL_RGBA,  GL_RGBA,  GL_UNSIGNED_INT_2_10_10_10_REV, "RGB10A2 UNORM ???" }, // D3DFMT_A2B10G10R10 = DXGI_FORMAT_R10G10B10A2_UNORM
	{ DDPF_RGBA, 32, 0x3ff,       0xffc00,    0x3ff00000, 0xc0000000,   GL_RGBA,  GL_RGBA,  GL_UNSIGNED_INT_2_10_10_10_REV, "RGB10A2 UNORM ?" },
	// the following would be correct, but apparently broken writers (MS D3DX) used those masks for D3DFMT_A2B10G10R10
	//{ DDPF_RGBA, 32, 0x3ff00000,  0xffc00,  0x3ff,      0xc0000000,   GL_RGBA,  GL_RGBA,  GL_UNSIGNED_INT_10_10_10_2,     "BGR10A2 UNORM ???" }, // D3DFMT_A2R10G10B10
	{ DDPF_RGBA, 32, 0x3ff00000,  0xffc00,    0x3ff,      0xc0000000,   GL_RGBA,  GL_RGBA,  GL_UNSIGNED_INT_2_10_10_10_REV, "RGB10A2 UNORM ?" },
	{ DDPF_RGBA, 16, 0x7c00,      0x3e0,      0x1f,       0x8000,       GL_RGBA,  GL_BGRA,  GL_UNSIGNED_SHORT_1_5_5_5_REV,  "RGB5A1 UNORM" },
	{ DDPF_RGB,  16, 0x7c00,      0x3e0,      0x1f,       0,            GL_RGBA,  GL_BGRA,  GL_UNSIGNED_SHORT_1_5_5_5_REV,  "RGB5X1 UNORM", OF_NOALPHA },
	{ DDPF_RGB,  16, 0xf800,      0x7e0,      0x1f,       0,            GL_RGB,   GL_RGB,   GL_UNSIGNED_SHORT_5_6_5,        "RGB565 UNORM" },
	{ DDPF_ALPHA, 8, 0,           0,          0,          0xff,         GL_ALPHA, GL_ALPHA, GL_UNSIGNED_BYTE, "Alpha8 UNORM" },
	{ DDPF_RGBA, 32, 0xff0000,    0xff00,     0xff,       0xff000000,   GL_BGRA,  GL_BGRA,  GL_UNSIGNED_BYTE, "BGRA8 UNORM" },
	{ DDPF_RGB,  32, 0xff0000,    0xff00,     0xff,       0,            GL_BGRA,  GL_BGRA,  GL_UNSIGNED_BYTE, "BGRX8 UNORM", OF_NOALPHA },
	{ DDPF_RGB,  32, 0xff,        0xff00,     0xff0000,   0,            GL_RGBA,  GL_RGBA,  GL_UNSIGNED_BYTE, "RGBX8 UNORM", OF_NOALPHA },
	{ DDPF_RGB,  24, 0xff0000,    0xff00,     0xff,       0,            GL_BGR,   GL_BGR,   GL_UNSIGNED_BYTE, "BGR8 UNORM" },
	{ DDPF_RGB,  24, 0xff,        0xff00,     0xff0000,   0,            GL_RGB,   GL_RGB,   GL_UNSIGNED_BYTE, "RGB8 UNORM" }, // DG: I added this one
	{ DDPF_RGBA, 16, 0xf00,       0xf0,       0xf,        0xf000,       GL_RGBA,  GL_RGBA,  GL_UNSIGNED_SHORT_4_4_4_4, "RGBA4 UNORM" },
	{ DDPF_RGBA, 16, 0xf00,       0xf0,       0xf,        0,            GL_RGBA,  GL_RGBA,  GL_UNSIGNED_SHORT_4_4_4_4, "RGBX4 UNORM", OF_NOALPHA },
	// TODO: D3DFMT_A8R3G3B2 16 bits; no OpenGL equivalent - DDPF_RGBA, 16, 0xe0, 0x1c, 0x3, 0xff00

	{ DDPF_LUMINANCE, 16, 0xff,   0,          0,          0xff00,  GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, "Luminance8 Alpha8" },
	{ DDPF_LUMINANCE, 16, 0xffff, 0,          0,          0,       GL_LUMINANCE,       GL_LUMINANCE,       GL_UNSIGNED_SHORT, "Luminance16" },
	// FIXME: gli/data/kueken7_l8_unorm.dds doesn't load
	{ DDPF_LUMINANCE,  8, 0xff,   0,          0,          0,       GL_LUMINANCE,       GL_LUMINANCE,       GL_UNSIGNED_BYTE, "Luminance8" },
	// TODO: D3DFMT_A4L4; no OpenGL equivalent (GL_UNSIGNED_BYTE_4_4 doesn't exist)
	//{ DDPF_LUMINANCE,  8, 0x0f,   0,          0,          0xf0,    GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE_4_4, "Luminance8 Alpha8" },
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
			ret.ourFlags |= OF_COMPRESSED;
			break;
		}
	}

	if(ret.dx10misc2 == DDS_DX10MISC2_ALPHA_PREMULTIPLIED)
		ret.ourFlags |= OF_PREMUL_ALPHA;

	return ret;
}

static UncomprFourCCFormatInfo FindUncomprFourCCFormat(uint32_t fourcc, int dxgiFmt)
{
	UncomprFourCCFormatInfo ret = {};
	for(const UncomprFourCCFormatInfo& fi : uncomprFourccTable) {
		if(fi.ddsFourCC == fourcc && fi.dxgiFormat == dxgiFmt) {
			ret = fi;
			break;
		}
	}

	return ret;
}

static UncomprFormatInfo FindUncomprFormat(const DDS_PIXELFORMAT& pf)
{
	UncomprFormatInfo ret = {};
	for(const UncomprFormatInfo& fi : uncomprMaskTable) {
		// FIXME: kueken7_rgba4_unorm.dds uses fourcc 26 (D3DFMT_A4R4G4B4)
		// FIXME: kueken7_r5g6b5_unorm.dds uses fourcc 0x17 (VS and DxTex don't load it either)
		// => add d3dFMT number to the table? or more entries to uncomprFourccTable?
		static const uint32_t flagsToCheck = DDPF_ALPHA|DDPF_ALPHAPIXELS|DDPF_RGB|DDPF_LUMINANCE;
		if(pf.dwRGBBitCount == fi.bitsPerPixel && (pf.dwFlags & flagsToCheck) == fi.pfFlags) {
			if((fi.pfFlags & (DDPF_ALPHAPIXELS|DDPF_ALPHA)) && fi.aMask != pf.dwRGBAlphaBitMask)
				continue;
			if((fi.pfFlags & DDPF_LUMINANCE) && fi.rMask != pf.dwRBitMask)
				continue;
			if(fi.pfFlags & DDPF_RGB) {
				if(fi.rMask != pf.dwRBitMask || fi.gMask != pf.dwGBitMask
				   || fi.bMask != pf.dwBBitMask) {
					continue;
				}
			}
			// if we got this far, the masks must have matched
			ret = fi;
			break;
		}
	}

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
			ret.ourFlags |= OF_COMPRESSED;
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
	UncomprFourCCFormatInfo uncomprInfo = {};
	int32_t pitchTypeOrBitsPerPixel = 0; // set for everything except ASTC, see enum PitchType above
	formatIsCompressed = false;
	bool isASTC = false;
	if( (fourcc == PIXEL_FMT_DX10 && (unsigned)dxgiFmt >= DXGI_FORMAT_ASTC_4X4_TYPELESS
	                          && (unsigned)dxgiFmt <= DXGI_FORMAT_ASTC_12X12_UNORM_SRGB)
	   || (fourcc & PIXEL_FMT_FOURCC('A', 'S', 0, 0)) == PIXEL_FMT_FOURCC('A', 'S', 0, 0) ) // all ASTC fourccs start with 'AS'
	{
		astcInfo = FindASTCFormat(fourcc, dxgiFmt);
		if(astcInfo.glFormat != 0) {
			isASTC = true;
			dataFormat = astcInfo.glFormat;
			formatName = astcInfo.name;
			formatIsCompressed = true;
			ourFlags = astcInfo.ourFlags;
		} else if(fourcc == PIXEL_FMT_DX10) {
			errprintf("Couldn't detect data format of '%s' - its dxgiFormat (%d) is in the ASTC-range, but apparently didn't match any actual format\n",
			          filename, dxgiFmt);
			return false;
		} // otherwise it was the "fourcc starts with 'AS'" case, for that also try the regular format table

	}
	if(!isASTC) {
		fmtInfo = FindComprFormat(fourcc, dxgiFmt, header->ddpfPixelFormat.dwFlags, dx10misc2);
#if 0
		if(fmtInfo.glFormat == 0) {
			char fccstr[5] = { char(fourcc & 0xff), char((fourcc >> 8) & 0xff),
			                   char((fourcc >> 16) & 0xff), char((fourcc >> 24) & 0xff), 0 };
			errprintf( "Couldn't detect data format of '%s' - FourCC: 0x%x ('%s') dxgiFormat: %d\n", filename, fourcc, fccstr, dxgiFmt );
			return false;
		}
#endif
		if(fmtInfo.glFormat != 0) {
			dataFormat = fmtInfo.glFormat;
			formatName = fmtInfo.name;
			formatIsCompressed = true;
			pitchTypeOrBitsPerPixel = fmtInfo.pitchTypeOrBitsPPixel;
			ourFlags = fmtInfo.ourFlags;
		}
	}
	if(!formatIsCompressed) {
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
		if(uncomprInfo.glFormat == 0 && (header->ddpfPixelFormat.dwFlags & DDPF_FOURCC) == 0) {
			UncomprFormatInfo uncomprMaskInfo = FindUncomprFormat(header->ddpfPixelFormat);
			if(uncomprMaskInfo.glFormat != 0) {
				// store the relevant parts of the result in uncomprInfo so it can be used for both cases
				// (though if it turns out we don't use uncomprInfo any further, just assign directly..)
				uncomprInfo.glFormat = uncomprMaskInfo.glFormat;
				uncomprInfo.bitsPerPixel = uncomprMaskInfo.bitsPerPixel;
				uncomprInfo.glIntFormat = uncomprMaskInfo.glIntFormat;
				uncomprInfo.glType = uncomprMaskInfo.glType;
				uncomprInfo.name = uncomprMaskInfo.name;
				uncomprInfo.ourFlags = uncomprMaskInfo.ourFlags;
			}
		}

		if(uncomprInfo.glFormat != 0) {
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
	if(dataFormat == 0) {
		char fccstr[5] = { char(fourcc & 0xff), char((fourcc >> 8) & 0xff),
		                   char((fourcc >> 16) & 0xff), char((fourcc >> 24) & 0xff), 0 };
		errprintf( "Couldn't detect data format of '%s' - FourCC: 0x%x ('%s') dxgiFormat: %d\n", filename, fourcc, fccstr, dxgiFmt );
		return false;
	} else {
		char fccstr[5] = { char(fourcc & 0xff), char((fourcc >> 8) & 0xff),
		                   char((fourcc >> 16) & 0xff), char((fourcc >> 24) & 0xff), 0 };
		errprintf( "Loading '%s' - FourCC: 0x%x (%d - '%s') dxgiFormat: %d\n", filename, fourcc, fourcc, fccstr, dxgiFmt );
	}

	name = filename;
	fileType = 0; // TODO
	texData = mmf;
	texDataFreeFun = [](void* texData, intptr_t) -> void { UnloadMemMappedFile( (MemMappedFile*)texData ); };

	int mipW = w;
	int mipH = h;
	const unsigned char* dataCur = data + dataOffset;
	for(int i=0; i<numMips; ++i) {
		uint32_t mipSize;
		if(!isASTC) {
			mipSize = CalcSize(mipW, mipH, pitchTypeOrBitsPerPixel);
		} else {
			mipSize = CalcASTCmipSize(mipW, mipH, astcInfo.blockW, astcInfo.blockH);
		}
		const unsigned char* dataNext = dataCur + mipSize;
		if(dataNext > dataEnd) {
			errprintf("MipMap level %d for '%s' is incomplete (file too small) mipSize: %d w: %d h: %d!\n",
					i, filename, mipSize, mipW, mipH);
			return (i > 0); // if we loaded at least one mipmap we can display the file despite the error
		}
		mipLevels.push_back( MipLevel(mipW, mipH, dataCur, mipSize) );
		if(mipW == 1 && mipH == 1 && i < numMips-1) {
			errprintf( "Texture '%s' claimed to have %d MipMap levels, but we're already done after %d levels\n", filename, numMips, i+1 );
			break;
		}
		dataCur = dataNext;
		mipW = std::max( mipW / 2, 1 );
		mipH = std::max( mipH / 2, 1 );
	}

	return true;
}

} //namespace texview
