
#define STB_IMAGE_IMPLEMENTATION
#include "libs/stb_image.h"

#include "texview.h"

namespace texview {

Texture LoadTexture(const char* filename)
{
	Texture ret;

	// TODO: probably memory map the input file and use stbi_load_from_memory()
	//       and actually check file extension and load dds if applicable before trying stb_image

	int w, h, comp;
	unsigned char* pix = stbi_load(filename, &w, &h, &comp, STBI_rgb_alpha);

	if(pix != nullptr) {
		ret.name = filename;
		ret.fileType = 0; // TODO
		ret.dataType = 0; // TODO

		ret.texData = pix;
		ret.texDataFreeFun = [](void* texData, void*) -> void { STBI_FREE(texData); };

		ret.mipLevels.push_back( Texture::MipLevel(w, h, pix) );
	}

	return ret;
}

} //namespace texview
