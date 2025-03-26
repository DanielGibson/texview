/*
 * dg_libktx_extra.h
 *
 * some additional functions to get additional infos from KTX/KTX2 textures
 * and some related functions to get additional infos about OpenGL types
 * they rely on internal code from libktx!
 *
 * do with this whatever you want.
 *
 */

#ifndef SRC_LIBS_DG_LIBKTX_EXTRA_H_
#define SRC_LIBS_DG_LIBKTX_EXTRA_H_

#include <assert.h>
#include <string.h>

#include <ktx.h>
#include "ktx/lib/vk_format.h"
#include "ktx/lib/vk2gl.h"

#ifdef __cplusplus
extern "C" {
#endif

static uint32_t ktxTexture_GetVkFormat(const ktxTexture* tex)
{
	assert(tex && "don't call this with NULL");
	if(tex == NULL)
		return 0;
	if(tex->classId == ktxTexture1_c) {
		const ktxTexture1* tex1 = (const ktxTexture1*)tex;
		uint32_t ret = vkGetFormatFromOpenGLInternalFormat(tex1->glInternalformat);
		if(ret == 0)
			ret = vkGetFormatFromOpenGLFormat(tex1->glFormat, tex1->glType);
		return ret;
	} else if(tex->classId == ktxTexture2_c) {
		const ktxTexture2* tex2 = (const ktxTexture2*)tex;
		return tex2->vkFormat;
	} else {
		assert(0 && "unsupported texture format");
	}
	return 0;
}

static GLint dg_glGetBaseInternalFormat(GLenum glInternalFormat)
{
	// NOTE: glGetFormatFromInternalFormat() returns GL_INVALID_VALUE if it's already a base format
	//       (or is invalid.. in the end: if it's not in its table)
	GLint ret = glGetFormatFromInternalFormat(glInternalFormat);
	// FIXME: yes, it would be cleaner to check if glInternalFormat is actually
	//        a valid baseformat but I hope this is good enough...
	return (ret != GL_INVALID_VALUE) ? ret : glInternalFormat;
}

// set arguments you don't care about to NULL (except for tex, of course)
// might set GL_INVALID_VALUE if no valid whatever was found
// but glFormat and glType are set to 0 if it's a compressed format
static bool ktxTexture_GetOpenGLFormat(const ktxTexture* tex, GLint* glInternalFormat, GLenum* glBaseInternalformat, GLenum* glFormat, GLenum* glType)
{
	assert(tex && "don't call this with tex = NULL");
	if(tex == NULL)
		return false;
	if(tex->classId == ktxTexture1_c) {
		const ktxTexture1* tex1 = (const ktxTexture1*)tex;
		if(glInternalFormat)
			*glInternalFormat = tex1->glInternalformat;
		if(glBaseInternalformat)
			*glBaseInternalformat = tex1->glBaseInternalformat;
		if(glFormat)
			*glFormat = tex1->glFormat;
		if(glType)
			*glType = tex1->glType;
		return true;
	} else if(tex->classId == ktxTexture2_c) {
		const ktxTexture2* tex2 = (const ktxTexture2*)tex;

		if(glInternalFormat || glBaseInternalformat) {
			GLint intFmt = vkFormat2glInternalFormat((VkFormat)tex2->vkFormat);
			if(glInternalFormat)
				*glInternalFormat = intFmt;
			if(glBaseInternalformat)
				*glBaseInternalformat = dg_glGetBaseInternalFormat(intFmt);
		}
		if(glFormat)
			*glFormat = tex2->isCompressed ? 0 : vkFormat2glFormat((VkFormat)tex2->vkFormat);
		if(glType)
			*glType = tex2->isCompressed ? 0 : vkFormat2glType((VkFormat)tex2->vkFormat);
		return true;
	} else {
		assert(0 && "unsupported KTX format");
	}
	return false;
}

// NOTE: Does NOT work with compressed formats, unless you get their base internal format first!
static inline bool dg_glFormatHasAlpha(GLenum glFormat)
{
	switch(glFormat) {
		case GL_RGBA:
		case GL_BGRA:
		case GL_ALPHA:
		case GL_SRGB_ALPHA:
		case GL_LUMINANCE_ALPHA:
		case GL_RGBA_INTEGER:
		case GL_BGRA_INTEGER:
		case GL_ALPHA_INTEGER:
		case GL_LUMINANCE_ALPHA_INTEGER:
			return true;
	}
	return false;
}

// Note: this one *does* work with compressed formats
static inline bool dg_glInternalFormatHasAlpha(GLint glInternalFormat)
{
	GLenum baseIntFmt = dg_glGetBaseInternalFormat(glInternalFormat);
	return dg_glFormatHasAlpha(baseIntFmt);
}

static inline bool ktxTexture_FormatHasAlpha(const ktxTexture* tex)
{
	GLenum baseIntFmt = 0;
	if(ktxTexture_GetOpenGLFormat(tex, NULL, &baseIntFmt, NULL, NULL)) {
		return dg_glFormatHasAlpha(baseIntFmt);
	}
	return false;
}

// from ktx/lib/vkformat_str.c
extern const char* vkFormatString(VkFormat format);

static inline bool ktxTexture_FormatIsSRGB(const ktxTexture* tex)
{
	VkFormat fmt = (VkFormat)ktxTexture_GetVkFormat(tex);
	assert(fmt != 0 && "tex has invalid format?!");
	if(fmt != 0) {
		const char* fmtStr = vkFormatString(fmt);
		return (strstr(fmtStr, "SRGB") != NULL);
	}
	return false;
}

static const char* ktxTexture_GetFormatName(const ktxTexture* tex)
{
	const char* ret = "<Unknown Format>";
	VkFormat fmt = (VkFormat)ktxTexture_GetVkFormat(tex);
	if(fmt != 0) {
		const char* fmtStr = vkFormatString(fmt);
		if(strncmp(fmtStr, "VK_FORMAT_", 10) == 0) {
			ret = fmtStr + 10;
		}
	}
	return ret;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* SRC_LIBS_DG_LIBKTX_EXTRA_H_ */
