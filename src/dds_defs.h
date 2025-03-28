// taken from https://github.com/richgel999/bc7enc_rdo, where it's put into
// the Public Domain (or alternatively MIT License)
//
// I adjusted some names to better match the Microsoft documentation
// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header
// and added more (inofficial) PIXEL_FMT_* and DXGI_FORMAT_* entries
// All changes to this file are also put into the Public Domain
// or alternatively MIT license.
//
// File: dds_defs.h
// DX9/10 .DDS file header definitions.
#pragma once

#include <stdint.h>

#define PIXEL_FMT_FOURCC(a, b, c, d) ((a) | ((b) << 8U) | ((c) << 16U) | ((d) << 24U))

enum pixel_format
{
	PIXEL_FMT_INVALID = 0,

	PIXEL_FMT_DXT1 = PIXEL_FMT_FOURCC('D', 'X', 'T', '1'), // BC1
	PIXEL_FMT_DXT2 = PIXEL_FMT_FOURCC('D', 'X', 'T', '2'), // BC2 with premultiplied alpha
	PIXEL_FMT_DXT3 = PIXEL_FMT_FOURCC('D', 'X', 'T', '3'), // BC2
	PIXEL_FMT_DXT4 = PIXEL_FMT_FOURCC('D', 'X', 'T', '4'), // BC3 with premultiplied alpha
	PIXEL_FMT_DXT5 = PIXEL_FMT_FOURCC('D', 'X', 'T', '5'), // BC3
	PIXEL_FMT_3DC = PIXEL_FMT_FOURCC('A', 'T', 'I', '2'), // DXN_YX - BC5 with swapped channels
	PIXEL_FMT_DXN = PIXEL_FMT_FOURCC('A', '2', 'X', 'Y'), // DXN_XY - BC5
	PIXEL_FMT_BC5S = PIXEL_FMT_FOURCC('B', 'C', '5', 'S'), // BC5 SNORM
	PIXEL_FMT_BC5U = PIXEL_FMT_FOURCC('B', 'C', '5', 'U'), // BC5 UNORM
	PIXEL_FMT_DXT5A = PIXEL_FMT_FOURCC('A', 'T', 'I', '1'), // BC4 aka ATI1N, http://developer.amd.com/media/gpu_assets/Radeon_X1x00_Programming_Guide.pdf
	PIXEL_FMT_BC4U = PIXEL_FMT_FOURCC('B', 'C', '4', 'U'), // BC4 UNORM
	PIXEL_FMT_BC4S = PIXEL_FMT_FOURCC('B', 'C', '4', 'S'), // BC4 SNORM

	// the following are not standard, I think, but I've seen them in some header..
	PIXEL_FMT_BC6H = PIXEL_FMT_FOURCC('B', 'C', '6', 'H'), // UF16
	PIXEL_FMT_BC7L = PIXEL_FMT_FOURCC('B', 'C', '7', 'L'),
	PIXEL_FMT_BC7 = PIXEL_FMT_FOURCC('B', 'C', '7', '0'),

	// Non-standard formats (some of these are supported by ATI's Compressonator)
	// ! their fourcc is set in dwRGBBitCount (fourcc is that of the base type) !
	PIXEL_FMT_DXT5_CCxY = PIXEL_FMT_FOURCC('C', 'C', 'x', 'Y'),
	PIXEL_FMT_DXT5_xGxR = PIXEL_FMT_FOURCC('x', 'G', 'x', 'R'),
	PIXEL_FMT_DXT5_xGBR = PIXEL_FMT_FOURCC('x', 'G', 'B', 'R'),
	PIXEL_FMT_DXT5_AGBR = PIXEL_FMT_FOURCC('A', 'G', 'B', 'R'),
	// used for normalmaps in Doom3, same as xGBR (misnamed)
	// in this case, that fourcc is in dwFourCC
	PIXEL_FMT_DXT5_RXGB = PIXEL_FMT_FOURCC('R', 'X', 'G', 'B'),

	PIXEL_FMT_DXT1A = PIXEL_FMT_FOURCC('D', 'X', '1', 'A'), // BC1 with alpha?

	// most of these ETC FourCCs can be found in dds-ktx.h, for example
	PIXEL_FMT_ETC1 = PIXEL_FMT_FOURCC('E', 'T', 'C', '1'),
	PIXEL_FMT_ETC = PIXEL_FMT_FOURCC('E', 'T', 'C', ' '), // GLI defines this
	PIXEL_FMT_ETC2 = PIXEL_FMT_FOURCC('E', 'T', 'C', '2'),
	PIXEL_FMT_ETC2A = PIXEL_FMT_FOURCC('E', 'C', '2', 'A'),
	PIXEL_FMT_EACR11 = PIXEL_FMT_FOURCC('E', 'A', 'R', ' '), // cryengine/lumberyard defines this one
	PIXEL_FMT_EACRG11 = PIXEL_FMT_FOURCC('E', 'A', 'R', 'G'), // .. and this one as well

	// I found most of these ASTC FourCCs in a cryengine/lumberyard header
	// https://github.com/aws/lumberyard/blob/413ecaf24d7a534801cac64f50272fe3191d278f/dev/Code/CryEngine/CryCommon/ImageExtensionHelper.h#L1487C86-L1505
	// some of them were also in dds-ktx.h and/or BGFX BIMG image.cpp (esp. the _ALT ones)
	PIXEL_FMT_ASTC_4x4 = PIXEL_FMT_FOURCC('A', 'S', '4', '4'),
	PIXEL_FMT_ASTC_5x4 = PIXEL_FMT_FOURCC('A', 'S', '5', '4'),
	PIXEL_FMT_ASTC_5x5 = PIXEL_FMT_FOURCC('A', 'S', '5', '5'),
	PIXEL_FMT_ASTC_6x5 = PIXEL_FMT_FOURCC('A', 'S', '6', '5'),
	PIXEL_FMT_ASTC_6x6 = PIXEL_FMT_FOURCC('A', 'S', '6', '6'),
	PIXEL_FMT_ASTC_8x5 = PIXEL_FMT_FOURCC('A', 'S', '8', '5'),
	PIXEL_FMT_ASTC_8x6 = PIXEL_FMT_FOURCC('A', 'S', '8', '6'),
	PIXEL_FMT_ASTC_8x8 = PIXEL_FMT_FOURCC('A', 'S', '8', '8'),
	PIXEL_FMT_ASTC_10x5 = PIXEL_FMT_FOURCC('A', 'S', 'A', '5'),
	PIXEL_FMT_ASTC_10x6 = PIXEL_FMT_FOURCC('A', 'S', 'A', '6'),
	PIXEL_FMT_ASTC_10x8 = PIXEL_FMT_FOURCC('A', 'S', 'A', '8'),
	PIXEL_FMT_ASTC_10x10 = PIXEL_FMT_FOURCC('A', 'S', 'A', 'A'),
	PIXEL_FMT_ASTC_12x10 = PIXEL_FMT_FOURCC('A', 'S', 'C', 'A'),
	PIXEL_FMT_ASTC_12x12 = PIXEL_FMT_FOURCC('A', 'S', 'C', 'C'),
	// alternative FourCCs found in BIMG:
	PIXEL_FMT_ASTC_10x5_ALT = PIXEL_FMT_FOURCC('A', 'S', ':', '5'),
	PIXEL_FMT_ASTC_10x6_ALT = PIXEL_FMT_FOURCC('A', 'S', ':', '6'),
	PIXEL_FMT_ASTC_10x8_ALT = PIXEL_FMT_FOURCC('A', 'S', ':', '8'),
	PIXEL_FMT_ASTC_10x10_ALT = PIXEL_FMT_FOURCC('A', 'S', ':', ':'),
	PIXEL_FMT_ASTC_12x10_ALT = PIXEL_FMT_FOURCC('A', 'S', '<', ':'),
	PIXEL_FMT_ASTC_12x12_ALT = PIXEL_FMT_FOURCC('A', 'S', '<', '<'),

	// I think the following are just formats used by crunch and/or bc7enc internally
	// but not actually written to (or parsed from) DDS files
#if 0 // commenting them out so I can use the names for other constants
	PIXEL_FMT_R8G8B8 = PIXEL_FMT_FOURCC('R', 'G', 'B', 'x'),
	PIXEL_FMT_L8 = PIXEL_FMT_FOURCC('L', 'x', 'x', 'x'),
	PIXEL_FMT_A8 = PIXEL_FMT_FOURCC('x', 'x', 'x', 'A'),
	PIXEL_FMT_A8L8 = PIXEL_FMT_FOURCC('L', 'x', 'x', 'A'),
	PIXEL_FMT_A8R8G8B8 = PIXEL_FMT_FOURCC('R', 'G', 'B', 'A'),
#endif

	// some "legacy" FourCCs defined in the second table of
	// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide#common-dds-file-resource-formats-and-associated-header-content
	// (D3DFMT_FOO corresponds to PIXEL_FMT_FOO)
	// they don't seem to have an opengl-equivalent
	PIXEL_FMT_R8G8_B8G8 = PIXEL_FMT_FOURCC('R', 'G', 'B', 'G'),
	PIXEL_FMT_G8R8_G8B8 = PIXEL_FMT_FOURCC('G', 'R', 'G', 'B'),
	PIXEL_FMT_UYVY = PIXEL_FMT_FOURCC('U', 'Y', 'V', 'Y'),
	PIXEL_FMT_YUY2 = PIXEL_FMT_FOURCC('Y', 'U', 'Y', '2'),

	// D3DFMT_MULTI2_ARGB8 - MultiElement texture (not compressed)
	// https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-element-textures
	// no idea if this was ever used in DDS files, or only for FBOs or similar
	PIXEL_FMT_MULTI2_ARGB8 = PIXEL_FMT_FOURCC('M','E','T','1'), // TODO?

	// furthermore, there appear to be some D3DFMT_* constants that can be set
	// as fourcc even though they're just simple numbers.
	// to set them apart from the fourcc constants, they're called D3DFMT_*
	// here as well, instead of PIXEL_FMT_*
	// (probably because they have >32bit or use floats, so they can't be represented by the masks?)
	// most (but not all!) of them have an equivalent DXGI format (with different
	// values and inversed order?! like D3DFMT_A8B8G8R8 is DXGI_FORMAT_R8G8B8A8_UNORM)
	// Apparently D3DFMT uses bit order within a little endian integer,
	// while DXGI_FORMAT uses bit/byte order based on memory address.
	// DX10+ DXGI_FMT docs say, at
	// https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format#remarks
	// "Most formats have byte-aligned components, and the components are in C-array
	//  order (the least address comes first). For those formats that don't have
	//  power-of-2-aligned components, the first named component is in the least-significant bits."
	// D3D9 D3DFORMAT docs (https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dformat) say:
	// "All formats are listed from left to right, most-significant bit to least-significant bit."
	// "The order of the bits is from the most significant byte first, so D3DFMT_A8L8
	//  indicates that the high byte of this 2-byte format is alpha.
	//  D3DFMT_D16 indicates a 16-bit integer value and an application-lockable surface."

	D3DFMT_A16B16G16R16  = 36,  // DXGI_FORMAT_R16G16B16A16_UNORM
	D3DFMT_Q16W16V16U16  = 110, // DXGI_FORMAT_R16G16B16A16_SNORM
	D3DFMT_R16F          = 111, // DXGI_FORMAT_R16_FLOAT
	D3DFMT_G16R16F       = 112, // DXGI_FORMAT_R16G16_FLOAT
	D3DFMT_A16B16G16R16F = 113, // DXGI_FORMAT_R16G16B16A16_FLOAT
	D3DFMT_R32F          = 114, // DXGI_FORMAT_R32_FLOAT
	D3DFMT_G32R32F       = 115, // DXGI_FORMAT_R32G32_FLOAT
	D3DFMT_A32B32G32R32F = 116, // DXGI_FORMAT_R32G32B32A32_FLOAT
	// CxV8U8: "16-bit normal compression format. The texture sampler
	//          computes the C channel from: C = sqrt(1 - U² - V²)"
	D3DFMT_CxV8U8        = 117, // no equivalent DXGI (or OpenGL) format

	// .. and other D3DFMT_* constants that shouldn't be used but some are
	// (at least by GLIs test data..). So let's support all D3DFMT_ that are either in this table:
	// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide#common-dds-file-resource-formats-and-associated-header-content
	// or have an equivalent DXGI format according to
	// https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-legacy-formats
	D3DFMT_R8G8B8 = 20,
	D3DFMT_A8R8G8B8 = 21,
	D3DFMT_X8R8G8B8 = 22,
	D3FMT_R5G6B5 = 23,
	D3DFMT_X1R5G5B5 = 24,
	D3DFMT_A1R5G5B5 = 25,
	D3DFMT_A4R4G4B4 = 26,
	D3DFMT_X4R4G4B4 = 30,
	D3DFMT_A8 = 28,

	D3DFMT_A8B8G8R8 = 32,
	D3DFMT_X8B8G8R8 = 33,
	D3DFMT_G16R16 = 34,
	D3DFMT_A2B10G10R10 = 31,
	D3DFMT_A2R10G10B10 = 35, // not really used, I think, at least not intentionally :-p

	D3DFMT_L8 = 50,
	D3DFMT_A8L8 = 51,
	D3DFMT_L16 = 81,

	D3DFMT_A8R3G3B2 = 29, // no opengl equivalent! 16 bits; DDPF_RGBA, 16, 0xe0, 0x1c, 0x3, 0xff00
	D3DFMT_A4L4 = 52,     // no opengl equivalent! 8 bits; DDPF_LUMINANCE,  8, 0x0f, 0, 0, 0xf0

	D3DFMT_V8U8 = 117,
	D3DFMT_Q8W8V8U8 = 63,
	D3DFMT_D16 = 80,
	D3DFMT_INDEX16 = 101,
	D3DFMT_INDEX32 = 102,
	// Note: a D3DFMT_S8D24 is mentioned in "Map Direct3D 9 Formats to Direct3D 10"
	//       table but doesn't actually exist. I hope they meant D3DFMT_D24S8...
	D3DFMT_D24S8 = 75,
	D3DFMT_D32F_LOCKABLE = 82,

	D3DFMT_B8G8R8 = 220, // DG: non-standard; this is the constant Gimp uses, dxwrapper uses 19 instead

	// if the DX10 FourCC is set, the actual format is defined
	// in the optional DX10 DDS header, as a DXGI_FORMAT
	PIXEL_FMT_DX10 = PIXEL_FMT_FOURCC('D', 'X', '1', '0'),
};

const uint32_t cDDSMaxImageDimensions = 8192U;

// Total size of header is sizeof(uint32)+cDDSSizeofDDSurfaceDesc2;
const uint32_t cDDSSizeofDDSurfaceDesc2 = 124;

// "DDS "
const uint32_t cDDSFileSignature = 0x20534444;

struct DDS_PIXELFORMAT
{
	uint32_t dwSize;
	uint32_t dwFlags;
	uint32_t dwFourCC;
	uint32_t dwRGBBitCount;     // ATI compressonator will place a FOURCC code here for swizzled/cooked DXTn formats
	uint32_t dwRBitMask;
	uint32_t dwGBitMask;
	uint32_t dwBBitMask;
	uint32_t dwRGBAlphaBitMask;
};

struct DDS_HEADER
{
	uint32_t dwSize;
	uint32_t dwFlags;
	uint32_t dwHeight;
	uint32_t dwWidth;
	union
	{
		int32_t lPitch;
		uint32_t dwLinearSize;
	};
	uint32_t dwDepth;
	uint32_t dwMipMapCount;
	uint32_t dwReserved1[11];
	DDS_PIXELFORMAT ddpfPixelFormat;
	uint32_t dwCaps;
	uint32_t dwCaps2;
	uint32_t dwCaps3;
	uint32_t dwCaps4;
	uint32_t dwReserved2;
};

const uint32_t DDSD_CAPS = 0x00000001;
const uint32_t DDSD_HEIGHT = 0x00000002;
const uint32_t DDSD_WIDTH = 0x00000004;
const uint32_t DDSD_PITCH = 0x00000008;

const uint32_t DDSD_BACKBUFFERCOUNT = 0x00000020;
const uint32_t DDSD_ZBUFFERBITDEPTH = 0x00000040;
const uint32_t DDSD_ALPHABITDEPTH = 0x00000080;

const uint32_t DDSD_LPSURFACE = 0x00000800;

const uint32_t DDSD_PIXELFORMAT = 0x00001000;
const uint32_t DDSD_CKDESTOVERLAY = 0x00002000;
const uint32_t DDSD_CKDESTBLT = 0x00004000;
const uint32_t DDSD_CKSRCOVERLAY = 0x00008000;

const uint32_t DDSD_CKSRCBLT = 0x00010000;
const uint32_t DDSD_MIPMAPCOUNT = 0x00020000;
const uint32_t DDSD_REFRESHRATE = 0x00040000;
const uint32_t DDSD_LINEARSIZE = 0x00080000;

const uint32_t DDSD_TEXTURESTAGE = 0x00100000;
const uint32_t DDSD_FVF = 0x00200000;
const uint32_t DDSD_SRCVBHANDLE = 0x00400000;
const uint32_t DDSD_DEPTH = 0x00800000;

const uint32_t DDSD_ALL = 0x00fff9ee;

const uint32_t DDPF_ALPHAPIXELS = 0x00000001;
const uint32_t DDPF_ALPHA = 0x00000002;
const uint32_t DDPF_FOURCC = 0x00000004;
const uint32_t DDPF_PALETTEINDEXED8 = 0x00000020;
const uint32_t DDPF_RGB = 0x00000040;
const uint32_t DDPF_LUMINANCE = 0x00020000;

const uint32_t DDSCAPS_COMPLEX = 0x00000008;
const uint32_t DDSCAPS_TEXTURE = 0x00001000;
const uint32_t DDSCAPS_MIPMAP = 0x00400000;

const uint32_t DDSCAPS2_CUBEMAP = 0x00000200;
const uint32_t DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400;
const uint32_t DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800;
const uint32_t DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000;
const uint32_t DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000;
const uint32_t DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000;
const uint32_t DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x00008000;

const uint32_t DDSCAPS2_CUBEMAP_MASK = DDSCAPS2_CUBEMAP_POSITIVEX | DDSCAPS2_CUBEMAP_NEGATIVEX
		| DDSCAPS2_CUBEMAP_POSITIVEY | DDSCAPS2_CUBEMAP_NEGATIVEY
		| DDSCAPS2_CUBEMAP_POSITIVEZ | DDSCAPS2_CUBEMAP_NEGATIVEZ;

const uint32_t DDSCAPS2_VOLUME = 0x00200000;

enum {
	DDS_DX10MISC_TEXTURECUBE          = 4,

	DDS_DX10MISC2_ALPHA_UNKNOWN       = 0,
	DDS_DX10MISC2_ALPHA_STRAIGHT      = 1,
	DDS_DX10MISC2_ALPHA_PREMULTIPLIED = 2,
	DDS_DX10MISC2_ALPHA_OPAQUE        = 3,
	DDS_DX10MISC2_ALPHA_CUSTOM        = 4, // alpha is 4th channel, not really alpha
};

typedef enum DXGI_FORMAT 
{
	DXGI_FORMAT_UNKNOWN = 0,
	DXGI_FORMAT_R32G32B32A32_TYPELESS = 1,
	DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
	DXGI_FORMAT_R32G32B32A32_UINT = 3,
	DXGI_FORMAT_R32G32B32A32_SINT = 4,
	DXGI_FORMAT_R32G32B32_TYPELESS = 5,
	DXGI_FORMAT_R32G32B32_FLOAT = 6,
	DXGI_FORMAT_R32G32B32_UINT = 7,
	DXGI_FORMAT_R32G32B32_SINT = 8,
	DXGI_FORMAT_R16G16B16A16_TYPELESS = 9,
	DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
	DXGI_FORMAT_R16G16B16A16_UNORM = 11,
	DXGI_FORMAT_R16G16B16A16_UINT = 12,
	DXGI_FORMAT_R16G16B16A16_SNORM = 13,
	DXGI_FORMAT_R16G16B16A16_SINT = 14,
	DXGI_FORMAT_R32G32_TYPELESS = 15,
	DXGI_FORMAT_R32G32_FLOAT = 16,
	DXGI_FORMAT_R32G32_UINT = 17,
	DXGI_FORMAT_R32G32_SINT = 18,
	DXGI_FORMAT_R32G8X24_TYPELESS = 19,
	DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
	DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
	DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
	DXGI_FORMAT_R10G10B10A2_TYPELESS = 23,
	DXGI_FORMAT_R10G10B10A2_UNORM = 24,
	DXGI_FORMAT_R10G10B10A2_UINT = 25,
	DXGI_FORMAT_R11G11B10_FLOAT = 26,
	DXGI_FORMAT_R8G8B8A8_TYPELESS = 27,
	DXGI_FORMAT_R8G8B8A8_UNORM = 28,
	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
	DXGI_FORMAT_R8G8B8A8_UINT = 30,
	DXGI_FORMAT_R8G8B8A8_SNORM = 31,
	DXGI_FORMAT_R8G8B8A8_SINT = 32,
	DXGI_FORMAT_R16G16_TYPELESS = 33,
	DXGI_FORMAT_R16G16_FLOAT = 34,
	DXGI_FORMAT_R16G16_UNORM = 35,
	DXGI_FORMAT_R16G16_UINT = 36,
	DXGI_FORMAT_R16G16_SNORM = 37,
	DXGI_FORMAT_R16G16_SINT = 38,
	DXGI_FORMAT_R32_TYPELESS = 39,
	DXGI_FORMAT_D32_FLOAT = 40,
	DXGI_FORMAT_R32_FLOAT = 41,
	DXGI_FORMAT_R32_UINT = 42,
	DXGI_FORMAT_R32_SINT = 43,
	DXGI_FORMAT_R24G8_TYPELESS = 44,
	DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
	DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
	DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
	DXGI_FORMAT_R8G8_TYPELESS = 48,
	DXGI_FORMAT_R8G8_UNORM = 49,
	DXGI_FORMAT_R8G8_UINT = 50,
	DXGI_FORMAT_R8G8_SNORM = 51,
	DXGI_FORMAT_R8G8_SINT = 52,
	DXGI_FORMAT_R16_TYPELESS = 53,
	DXGI_FORMAT_R16_FLOAT = 54,
	DXGI_FORMAT_D16_UNORM = 55,
	DXGI_FORMAT_R16_UNORM = 56,
	DXGI_FORMAT_R16_UINT = 57,
	DXGI_FORMAT_R16_SNORM = 58,
	DXGI_FORMAT_R16_SINT = 59,
	DXGI_FORMAT_R8_TYPELESS = 60,
	DXGI_FORMAT_R8_UNORM = 61,
	DXGI_FORMAT_R8_UINT = 62,
	DXGI_FORMAT_R8_SNORM = 63,
	DXGI_FORMAT_R8_SINT = 64,
	DXGI_FORMAT_A8_UNORM = 65,
	DXGI_FORMAT_R1_UNORM = 66,
	DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67,
	DXGI_FORMAT_R8G8_B8G8_UNORM = 68,
	DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
	DXGI_FORMAT_BC1_TYPELESS = 70,
	DXGI_FORMAT_BC1_UNORM = 71,
	DXGI_FORMAT_BC1_UNORM_SRGB = 72,
	DXGI_FORMAT_BC2_TYPELESS = 73,
	DXGI_FORMAT_BC2_UNORM = 74,
	DXGI_FORMAT_BC2_UNORM_SRGB = 75,
	DXGI_FORMAT_BC3_TYPELESS = 76,
	DXGI_FORMAT_BC3_UNORM = 77,
	DXGI_FORMAT_BC3_UNORM_SRGB = 78,
	DXGI_FORMAT_BC4_TYPELESS = 79,
	DXGI_FORMAT_BC4_UNORM = 80,
	DXGI_FORMAT_BC4_SNORM = 81,
	DXGI_FORMAT_BC5_TYPELESS = 82,
	DXGI_FORMAT_BC5_UNORM = 83,
	DXGI_FORMAT_BC5_SNORM = 84,
	DXGI_FORMAT_B5G6R5_UNORM = 85,
	DXGI_FORMAT_B5G5R5A1_UNORM = 86,
	DXGI_FORMAT_B8G8R8A8_UNORM = 87,
	DXGI_FORMAT_B8G8R8X8_UNORM = 88,
	DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
	DXGI_FORMAT_B8G8R8A8_TYPELESS = 90,
	DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
	DXGI_FORMAT_B8G8R8X8_TYPELESS = 92,
	DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
	DXGI_FORMAT_BC6H_TYPELESS = 94,
	DXGI_FORMAT_BC6H_UF16 = 95,
	DXGI_FORMAT_BC6H_SF16 = 96,
	DXGI_FORMAT_BC7_TYPELESS = 97,
	DXGI_FORMAT_BC7_UNORM = 98,
	DXGI_FORMAT_BC7_UNORM_SRGB = 99,
	DXGI_FORMAT_AYUV = 100,
	DXGI_FORMAT_Y410 = 101,
	DXGI_FORMAT_Y416 = 102,
	DXGI_FORMAT_NV12 = 103,
	DXGI_FORMAT_P010 = 104,
	DXGI_FORMAT_P016 = 105,
	DXGI_FORMAT_420_OPAQUE = 106,
	DXGI_FORMAT_YUY2 = 107,
	DXGI_FORMAT_Y210 = 108,
	DXGI_FORMAT_Y216 = 109,
	DXGI_FORMAT_NV11 = 110,
	DXGI_FORMAT_AI44 = 111,
	DXGI_FORMAT_IA44 = 112,
	DXGI_FORMAT_P8 = 113,
	DXGI_FORMAT_A8P8 = 114,
	DXGI_FORMAT_B4G4R4A4_UNORM = 115,
	DXGI_FORMAT_P208 = 130,
	DXGI_FORMAT_V208 = 131,
	DXGI_FORMAT_V408 = 132,

	// apparently microsoft specified ASTC DXGI formats, but then removed them again?!
	// see https://forums.developer.nvidia.com/t/nv-tt-exporter-astc-compression/122477/2
	// and https://github.com/bkaradzic/bgfx/blob/384e514f1b295f64c4070697fd9f8b444a0f256c/src/renderer_d3d.h#L16-L57
	// these are the values that were used (according to bgfx):
	DXGI_FORMAT_ASTC_4X4_TYPELESS     = 133,
	DXGI_FORMAT_ASTC_4X4_UNORM        = 134,
	DXGI_FORMAT_ASTC_4X4_UNORM_SRGB   = 135,
	DXGI_FORMAT_ASTC_5X4_TYPELESS     = 137,
	DXGI_FORMAT_ASTC_5X4_UNORM        = 138,
	DXGI_FORMAT_ASTC_5X4_UNORM_SRGB   = 139,
	DXGI_FORMAT_ASTC_5X5_TYPELESS     = 141,
	DXGI_FORMAT_ASTC_5X5_UNORM        = 142,
	DXGI_FORMAT_ASTC_5X5_UNORM_SRGB   = 143,
	DXGI_FORMAT_ASTC_6X5_TYPELESS     = 145,
	DXGI_FORMAT_ASTC_6X5_UNORM        = 146,
	DXGI_FORMAT_ASTC_6X5_UNORM_SRGB   = 147,
	DXGI_FORMAT_ASTC_6X6_TYPELESS     = 149,
	DXGI_FORMAT_ASTC_6X6_UNORM        = 150,
	DXGI_FORMAT_ASTC_6X6_UNORM_SRGB   = 151,
	DXGI_FORMAT_ASTC_8X5_TYPELESS     = 153,
	DXGI_FORMAT_ASTC_8X5_UNORM        = 154,
	DXGI_FORMAT_ASTC_8X5_UNORM_SRGB   = 155,
	DXGI_FORMAT_ASTC_8X6_TYPELESS     = 157,
	DXGI_FORMAT_ASTC_8X6_UNORM        = 158,
	DXGI_FORMAT_ASTC_8X6_UNORM_SRGB   = 159,
	DXGI_FORMAT_ASTC_8X8_TYPELESS     = 161,
	DXGI_FORMAT_ASTC_8X8_UNORM        = 162,
	DXGI_FORMAT_ASTC_8X8_UNORM_SRGB   = 163,
	DXGI_FORMAT_ASTC_10X5_TYPELESS    = 165,
	DXGI_FORMAT_ASTC_10X5_UNORM       = 166,
	DXGI_FORMAT_ASTC_10X5_UNORM_SRGB  = 167,
	DXGI_FORMAT_ASTC_10X6_TYPELESS    = 169,
	DXGI_FORMAT_ASTC_10X6_UNORM       = 170,
	DXGI_FORMAT_ASTC_10X6_UNORM_SRGB  = 171,
	DXGI_FORMAT_ASTC_10X8_TYPELESS    = 173,
	DXGI_FORMAT_ASTC_10X8_UNORM       = 174,
	DXGI_FORMAT_ASTC_10X8_UNORM_SRGB  = 175,
	DXGI_FORMAT_ASTC_10X10_TYPELESS   = 177,
	DXGI_FORMAT_ASTC_10X10_UNORM      = 178,
	DXGI_FORMAT_ASTC_10X10_UNORM_SRGB = 179,
	DXGI_FORMAT_ASTC_12X10_TYPELESS   = 181,
	DXGI_FORMAT_ASTC_12X10_UNORM      = 182,
	DXGI_FORMAT_ASTC_12X10_UNORM_SRGB = 183,
	DXGI_FORMAT_ASTC_12X12_TYPELESS   = 185,
	DXGI_FORMAT_ASTC_12X12_UNORM      = 186,
	DXGI_FORMAT_ASTC_12X12_UNORM_SRGB = 187,

	DXGI_FORMAT_FORCE_UINT = 0xffffffff
} DXGI_FORMAT;

enum D3D10_RESOURCE_DIMENSION 
{
	D3D10_RESOURCE_DIMENSION_UNKNOWN = 0,
	D3D10_RESOURCE_DIMENSION_BUFFER = 1,
	D3D10_RESOURCE_DIMENSION_TEXTURE1D = 2,
	D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3,
	D3D10_RESOURCE_DIMENSION_TEXTURE3D = 4
};

struct DDS_HEADER_DXT10
{
	DXGI_FORMAT              dxgiFormat;
	D3D10_RESOURCE_DIMENSION resourceDimension;
	uint32_t                 miscFlag;
	uint32_t                 arraySize;
	uint32_t                 miscFlags2;
};

