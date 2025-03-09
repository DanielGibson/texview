
#include "libs/stb_image.h"
#include <glad/gl.h>

#include "texview.h"

#include <stdio.h>
#include <string.h>

namespace texview {

bool Texture::Load(const char* filename)
{
	Clear();

	MemMappedFile* mmf = LoadMemMappedFile(filename);
	if(mmf == nullptr) {
		return false;
	}

	const char* lastDot = strrchr(filename, '.');
	if(lastDot != nullptr && strcasecmp(lastDot, ".dds") == 0) {
		// TODO: load DDS
		name = filename;
		fileType = 0; // TODO
		dataType = 0; // TODO parse from DDS?
		texData = mmf;
		texDataFreeFun = [](void* texData, intptr_t) -> void { UnloadMemMappedFile( (MemMappedFile*)texData ); };

	} else {
		// some other kind of file, try throwing it at stb_image
		int w, h, comp;
		unsigned char* pix;
		// TODO: check if mmf->length fits into int
		pix = stbi_load_from_memory((const unsigned char*)mmf->data,
		                            (int)mmf->length, &w, &h, &comp, STBI_rgb_alpha);

		if(pix != nullptr) {
			// mmf is not needed anymore, decoded image data is in pix
			UnloadMemMappedFile(mmf);
			mmf = nullptr;

			name = filename;
			fileType = 0; // TODO
			dataType = GL_RGBA8; // TODO
			texData = pix;
			texDataFreeFun = [](void* texData, intptr_t) -> void { stbi_image_free(texData); };

			mipLevels.push_back( Texture::MipLevel(w, h, pix) );

			return true;
		}

		// TODO: anything else to try?
	}

	errprintf("Couldn't load '%s', maybe the filetype is unsupported?\n", filename);
	UnloadMemMappedFile(mmf);
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
