
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
	if(texDataFreeFun != nullptr) {
		texDataFreeFun( (void*)texData, texDataFreeCookie );
	}
	name.clear();
	fileType = dataFormat = 0;
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
		dataFormat = GL_RGBA8; // TODO
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
};

struct FormatInfo {
	uint32_t ddsFourCC;
	int dxgiFormat;
	uint32_t glFormat;
	int32_t pitchTypeOrBitsPPixel; // PitchType or for "other" formats their bits per pixel
	const char* name;
	uint32_t pfFlags; // usually 0, DDPF_ALPHAPIXELS for DX1A
	uint8_t dx10misc2; // maybe DDS_ALPHA_MODE_PREMULTIPLIED or DDS_ALPHA_MODE_OPAQUE (only uses first 3 bits)
	uint8_t ourFlags;
};

enum { DX10 = PIXEL_FMT_FOURCC('D', 'X', '1', '0') }; // shorter than DX10fourcc, for following table

const FormatInfo comprFormatTable[] = {
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

const FormatInfo unComprFormatTable[] = {
	// TODO fourcc: PIXEL_FMT_R8G8B8, PIXEL_FMT_L8, PIXEL_FMT_A8, PIXEL_FMT_A8L8, PIXEL_FMT_A8R8G8B8
	// TODO fourcc DX10 with all those formats.. https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format
};

static FormatInfo FindFormat(uint32_t fourcc, int dxgiFmt, uint32_t pixelFormatFlags, uint8_t dx10misc2)
{
	FormatInfo ret = {};
	for(const FormatInfo& fi : comprFormatTable) {
		if( fi.ddsFourCC == fourcc && fi.dxgiFormat == dxgiFmt
			&& (fi.pfFlags & pixelFormatFlags) == fi.pfFlags
			// dx10misc2 is most probably not set (legacy D3DX 10 and D3DX 11 libs
			// fail to load DDS files where it's not 0)
			// so only if it's set in both cases make sure they match
			&& (dx10misc2 == 0 || fi.dx10misc2 == 0 || dx10misc2 == fi.dx10misc2))
		{
			ret = fi;
		}
	}
	if(ret.glFormat != 0) {
		ret.ourFlags |= OF_COMPRESSED;
	} else {
		for(const FormatInfo& fi : unComprFormatTable) {
			if( fi.ddsFourCC == fourcc && fi.dxgiFormat == dxgiFmt
				&& (fi.pfFlags & pixelFormatFlags) == fi.pfFlags
				// dx10misc2 is most probably not set (legacy D3DX 10 and D3DX 11 libs
				// fail to load DDS files where it's not 0)
				// so only if it's set in both cases make sure they match
				&& (dx10misc2 == 0 || fi.dx10misc2 == 0 || dx10misc2 == fi.dx10misc2))
			{
				ret = fi;
			}
		}
	}

	if(ret.dx10misc2 == DDS_DX10MISC2_ALPHA_PREMULTIPLIED)
		ret.ourFlags |= OF_PREMUL_ALPHA;

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
	int dxgiFmt = 0;
	uint8_t dx10misc2 = 0;
	if(fourcc == DX10fourcc) {
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
	FormatInfo fmtInfo = {};
	ASTCInfo astcInfo = {};
	bool isASTC = false;
	if( (fourcc == DX10fourcc && (unsigned)dxgiFmt >= DXGI_FORMAT_ASTC_4X4_TYPELESS
	                          && (unsigned)dxgiFmt <= DXGI_FORMAT_ASTC_12X12_UNORM_SRGB)
	   || (fourcc & PIXEL_FMT_FOURCC('A', 'S', 0, 0)) == PIXEL_FMT_FOURCC('A', 'S', 0, 0) ) // all ASTC fourccs start with 'AS'
	{
		astcInfo = FindASTCFormat(fourcc, (fourcc == DX10fourcc) ? dxgiFmt: 0);
		if(astcInfo.glFormat != 0) {
			isASTC = true;
			dataFormat = astcInfo.glFormat;
			formatName = astcInfo.name;
			formatIsCompressed = true;
		} else if(fourcc == DX10fourcc) {
			errprintf("Couldn't detect data format of '%s' - its dxgiFormat (%d) is in the ASTC-range, but apparently didn't match any actual format\n",
			          filename, dxgiFmt);
			return false;
		} // otherwise it was the "fourcc starts with 'AS'" case, for that also try the regular format table

	}
	if(!isASTC) {
		fmtInfo = FindFormat(fourcc, dxgiFmt, header->ddpfPixelFormat.dwFlags, dx10misc2);
		if(fmtInfo.glFormat == 0) {
			char fccstr[5] = { char(fourcc & 0xff), char((fourcc >> 8) & 0xff),
			                   char((fourcc >> 16) & 0xff), char((fourcc >> 24) & 0xff), 0 };
			errprintf( "Couldn't detect data format of '%s' - FourCC: 0x%x ('%s') dxgiFormat: %d\n", filename, fourcc, fccstr, dxgiFmt );
			return false;
		}
		dataFormat = fmtInfo.glFormat;
		formatName = fmtInfo.name;
		formatIsCompressed = (fmtInfo.ourFlags & OF_COMPRESSED) != 0;
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
			mipSize = CalcSize(mipW, mipH, fmtInfo.pitchTypeOrBitsPPixel);
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
