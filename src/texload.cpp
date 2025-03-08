
#define STB_IMAGE_IMPLEMENTATION
#include "libs/stb_image.h"

#include <glad/gl.h>

#include "texview.h"

namespace texview {

bool Texture::Load(const char* filename)
{
	Clear();

	// TODO: probably memory map the input file and use stbi_load_from_memory()
	//       and actually check file extension and load dds if applicable before trying stb_image

	int w, h, comp;
	unsigned char* pix = stbi_load(filename, &w, &h, &comp, STBI_rgb_alpha);

	if(pix != nullptr) {
		name = filename;
		fileType = 0; // TODO
		dataType = GL_RGBA8; // TODO

		texData = pix;
		texDataFreeFun = [](void* texData, intptr_t) -> void { STBI_FREE(texData); };

		mipLevels.push_back( Texture::MipLevel(w, h, pix) );
		return true;
	}

	return false;
}

void Texture::Clear()
{
	mipLevels.clear();
	if(texDataFreeFun != nullptr) {
		texDataFreeFun( (void*)texData, texDataFreeCookie );
	}
	name.clear();
	fileType = dataType = 0;
}

} //namespace texview
