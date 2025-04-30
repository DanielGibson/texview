/*
 * Copyright (C) 2025 Daniel Gibson
 *
 * Released under MIT License, see Licenses.txt
 */

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <ktx.h>

#ifdef TV_USE_NFD
#include <nfd.h>
#endif

#include <math.h>
#include <stdio.h>

#include <initializer_list>

#include "texview.h"
#include "version.h"

#include "data/texview_icon.h"
#include "data/texview_icon32.h"
#include "data/proggyvector_font.h"

using namespace texview;

// a wrapper around glVertexAttribPointer() to stay sane
// (caller doesn't have to cast to GLintptr and then void*)
static inline void
qglVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLintptr offset)
{
	glVertexAttribPointer(index, size, type, normalized, stride, (const void*)offset);
}

static GLFWwindow* glfwWindow;

static ImVec4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

// TODO: should probably support more than one texture eventually..
static texview::Texture curTex;

static GLuint shaderProgram = 0;
static GLuint quadsVBO = 0;
static GLuint quadsVAO = 0;
static GLint mvpMatrixUniform = 0;

static bool showImGuiDemoWindow = false;
static bool showAboutWindow = false;
static bool showGLSLeditWindow = false;

static float imguiMenuWidth = 0.0f;
static bool imguiMenuCollapsed = false;

static bool updateFont;
static float imguiScale = 1.0f;
static ImGuiStyle defaultStyle; // to reset style sizes before calling ScaleAllSizes()

static double zoomLevel = 1.0;
static double transX = 10;
static double transY = 10;
static bool dragging = false;
static ImVec2 lastDragPos;

static bool linearFilter = false;
static int mipmapLevel = -1; // -1: auto, otherwise enforce that level
static int overrideSRGB = -1; // -1: auto, 0: force disable, 1: force enable
static int overrideAlpha = -1; // -1: auto, 0: force disable alpha blending, 1: force enable

static int cubeCrossVariant = 0; // 0-3
static int textureArrayIndex = 0;
static std::string texSampleAndNormalize; // used in shader and shown in GLSL (swizzle) editor
static std::string swizzle; // used in shader, modifiable by user
// something like "b1ga", transformed to swizzle with SetSwizzleFromSimple()
static char simpleSwizzle[5] = {};
static bool useSimpleSwizzle = true;


static enum ViewMode {
	SINGLE,
	MIPMAPS_COMPACT,
	MIPMAPS_ROW,
	MIPMAPS_COLUMN,
	TILED
} viewMode;
static bool viewAtSameSize = true;
static int spacingBetweenMips = 2;
static int numTiles[2] = {2, 2};

static void glfw_error_callback(int error, const char* description)
{
	errprintf("GLFW Error: %d - %s\n", error, description);
}

static void ZoomFitToWindow(GLFWwindow* window, float tw, float th, bool isCube)
{
	if(isCube) {
		// shown as cross lying on the side => 4 wide, 3 high
		tw *= 4.0f;
		th *= 3.0f;
	}
	int display_w, display_h;
	glfwGetFramebufferSize(window, &display_w, &display_h);
	double winW = display_w - imguiMenuWidth;
	double zw = winW / tw;
	double zh = display_h / th;
	if(zw < zh) {
		zoomLevel = zw;
		transX = 0;
		transY = floor(0.5 * (display_h/zw - th));
	} else {
		zoomLevel = zh;
		transX = isCube ? 0.0 : floor(0.5 * (winW/zh - tw));
		transY = 0;
	}
}

// GL vertex attribute location indices
enum {
	TV_ATTRIB_POSITION   = 0,
	TV_ATTRIB_TEXCOORD   = 1,
};

static const char* vertexShaderSrc = R"(
in vec4 position; // TV_ATTRIB_POSITION
in vec4 inTexCoord; // TV_ATTRIB_TEXCOORD
uniform mat4 mvpMatrix;

out vec4 texCoord;
out float mipLevel;
void main()
{
	// position.w contains the desired miplevel (LOD)
	// so replace that with 1 for the actual position
	gl_Position = mvpMatrix * vec4(position.xyz, 1.0);
	texCoord = inTexCoord;
	mipLevel = position.w;
}
)";

// Note: before this something like "uniform sampler2D tex0;" is needed,
//       setting that in UpdateShaders() based on type
static const char* fragShaderStart = R"(
in vec4 texCoord;
in float mipLevel;
out vec4 OutColor;
void main()
{
)";

// ... here UpdateShaders() adds a line like "	vec4 c = texture(tex0, texCoord.st);\n"
// ... at this point swizzling could happen ("	c = c.agbr;") - generate that dynamically

// Note: only indenting with single space so it looks better in the advanced swizzle editor
static const char* fragShaderEnd =  R"(
 OutColor = c;
}
)";

static GLuint
CompileShader(GLenum shaderType, std::initializer_list<const char*> shaderSources)
{
	GLuint shader = glCreateShader(shaderType);
	glShaderSource(shader, shaderSources.size(), shaderSources.begin(), NULL);
	glCompileShader(shader);
	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if(status != GL_TRUE) {
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);
		GLint infoLogLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
		if(infoLogLength >= bufLen) {
			bufPtr = (char*)malloc(infoLogLength+1);
			bufLen = infoLogLength+1;
			if(bufPtr == NULL) {
				bufPtr = buf;
				bufLen = sizeof(buf);
				LogWarn("In CompileShader(), malloc(%d) failed!\n", infoLogLength+1);
			}
		}

		glGetShaderInfoLog(shader, bufLen, NULL, bufPtr);

		const char* shaderTypeStr = "";
		switch(shaderType)
		{
			case GL_VERTEX_SHADER:   shaderTypeStr = "Vertex"; break;
			case GL_FRAGMENT_SHADER: shaderTypeStr = "Fragment"; break;
			// I don't think I need geometry or tesselation shaders here
			// case GL_GEOMETRY_SHADER: shaderTypeStr = "Geometry"; break;
			/* not supported in OpenGL3.2 and we're unlikely to need/use them anyway
			case GL_COMPUTE_SHADER:  shaderTypeStr = "Compute"; break;
			case GL_TESS_CONTROL_SHADER:    shaderTypeStr = "TessControl"; break;
			case GL_TESS_EVALUATION_SHADER: shaderTypeStr = "TessEvaluation"; break;
			*/
		}
		LogError("Compiling %s Shader failed: %s\n", shaderTypeStr, bufPtr);
		LogPrint("Source BEGIN\n");
		for(const char* part : shaderSources) {
			LogPrint("%s", part);
		}
		LogPrint("\nSource END\n");
		LogError("Compiling %s Shader failed!\n", shaderTypeStr); // short version for warning overlay
		glDeleteShader(shader);

		if(bufPtr != buf) {
			free(bufPtr);
		}

		return 0;
	}

	return shader;
}

static GLuint
CreateShaderProgram(const GLuint shaders[2])
{
	GLuint prog = glCreateProgram();
	if(prog == 0) {
		LogError("ERROR: Couldn't create a new Shader Program!\n");
		return 0;
	}

	glAttachShader(prog, shaders[0]);
	glAttachShader(prog, shaders[1]);

	glBindAttribLocation(prog, TV_ATTRIB_POSITION, "position");
	glBindAttribLocation(prog, TV_ATTRIB_TEXCOORD, "inTexCoord");

	glLinkProgram(prog);

	GLint status;
	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if(status != GL_TRUE) {
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);
		GLint infoLogLength;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &infoLogLength);
		if(infoLogLength >= bufLen) {
			bufPtr = (char*)malloc(infoLogLength+1);
			bufLen = infoLogLength+1;
			if(bufPtr == NULL) {
				bufPtr = buf;
				bufLen = sizeof(buf);
				LogError("WARN: In CreateShaderProgram(), malloc(%d) failed!\n", infoLogLength+1);
			}
		}

		glGetProgramInfoLog(prog, bufLen, NULL, bufPtr);

		LogError("ERROR: Linking shader program failed: %s\n", bufPtr);

		glDeleteProgram(prog);

		if(bufPtr != buf) {
			free(bufPtr);
		}
		glDetachShader(prog, shaders[0]);
		glDetachShader(prog, shaders[1]);

		return 0;
	}

	return prog;
}

static void SetSwizzleFromSimple()
{
	swizzle.clear();
	const char* args[4] = { "0.0", "0.0", "0.0", "1.0" };
	for(int i=0; i<4; ++i) {
		char c = simpleSwizzle[i];
		if(c >= 'A' && c <= 'Z') {
			c += 32; // to lowercase
		}
		switch(c) {
			case '0':
				args[i] = "0.0";
				break;
			case '1':
				args[i] = "1.0";
				break;
			case 'r':
			case 'x':
				args[i] = "c.r";
				break;
			case 'g':
			case 'y':
				args[i] = "c.g";
				break;
			case 'b':
			case 'z':
				args[i] = "c.b";
				break;
			case 'a':
			case 'w':
				args[i] = "c.a";
				break;
			case '\0':
				// leave this and following at default value (0.0 or 1.0)
				// make sure the loop is terminated here, string is over
				i = 4;
				break;
			default:
				errprintf("Invalid character '%c' in swizzle!\n", simpleSwizzle[i]);
		}
	}
	StringAppendFormatted(swizzle, "c = vec4(%s, %s, %s, %s);\n", args[0], args[1], args[2], args[3]);
}

static bool UpdateShaders()
{
	const char* glslVersion = "#version 150\n";

	GLuint shaders[2] = {};
	shaders[0] = CompileShader(GL_VERTEX_SHADER, { glslVersion, vertexShaderSrc });
	if(shaders[0] == 0) {
		return false;
	}

	bool isUnsigned = false;
	const char* normDiv = curTex.GetIntTexInfo(isUnsigned); // divisor to normalize integer texture
	bool isIntTexture = normDiv != nullptr;

	std::string glslAdvVersion; // if used, glslVersion will point to it

	const char* samplerBaseType = "sampler2D";
	int numTexCoords = 2; // default: Texture2D; 2 for .st, 3 for .stp, 4 for stpq (1 for .s once supporting texture1D)
	const char* typePrefix = ""; // default: standard texture (not _INTEGER)
	const char* typePostfix = ""; // default: no array texture
	if(isIntTexture) {
		typePrefix = isUnsigned ? "u" : "i";
	}
	if(curTex.IsCubemap()) {
		samplerBaseType = "samplerCube";
		numTexCoords = 3;
		if(curTex.IsArray()) {
			// for cubemap arrays, this #extension thingy must be added after the #version
			// (unless version >= 400)
			glslAdvVersion = glslVersion;
			glslAdvVersion += "#extension GL_ARB_texture_cube_map_array : enable\n";
			glslVersion = glslAdvVersion.c_str();
		}
	}
	if(curTex.IsArray()) {
		typePostfix = "Array";
		numTexCoords++;
	}

	char samplerUniform[48] = {};
	snprintf(samplerUniform, sizeof(samplerUniform), "uniform %s%s%s tex0;\n", typePrefix, samplerBaseType, typePostfix);

	texSampleAndNormalize.clear();

	if(isIntTexture) {
		StringAppendFormatted(texSampleAndNormalize, " %svec4 v;\n", typePrefix);
		/* ivec4 v; // or uvec4
		 * if(mipLevel < 0.0)
		 *     v = texture( tex0, texCoord.st ); // or maybe .stp or .stpq
		 * else
		 *     v = textureLod( tex0, texCoord.st, mipLevel );
		 *
		 * vec4 c = vec4(4) / 127.0; // or other divisor depending on integer type
		 */

		StringAppendFormatted(texSampleAndNormalize, " if(mipLevel < 0.0)\n"
		                "	v = texture(tex0, texCoord.%.*s);\n",
		                numTexCoords, "stpq");
		StringAppendFormatted(texSampleAndNormalize, " else\n"
		                "	v = textureLod(tex0, texCoord.%.*s, mipLevel);\n",
		                numTexCoords, "stpq");
		// integer textures (GL_RGB_INTEGER etc) need normalization to display something useful
		StringAppendFormatted(texSampleAndNormalize, "\n vec4 c = vec4(v) / %s;\n", normDiv);
	} else {
		/* vec4 c;
		 * if(mipLevel < 0.0)
		 *     c = texture( tex0, texCoord.stp ); // or .st or .stpq
		 * else
		 *     c = textureLod( tex0, texCoord.stp, mipLevel );
		 */
		// normal textures don't need normalization, so assign to vec4 c directly
		StringAppendFormatted(texSampleAndNormalize, " vec4 c;\n");
		StringAppendFormatted(texSampleAndNormalize, " if(mipLevel < 0.0)\n"
		                "	c = texture(tex0, texCoord.%.*s);\n",
		                numTexCoords, "stpq");
		StringAppendFormatted(texSampleAndNormalize, " else\n"
		                "	c = textureLod(tex0, texCoord.%.*s, mipLevel);\n",
		                numTexCoords, "stpq");
	}

	if(useSimpleSwizzle) {
		SetSwizzleFromSimple();
	}

	std::initializer_list<const char*> fragShaderSrc = {
		glslVersion,
		samplerUniform,
		fragShaderStart,
		texSampleAndNormalize.c_str(),
		swizzle.c_str(),
		fragShaderEnd
	};
	shaders[1] = CompileShader(GL_FRAGMENT_SHADER, fragShaderSrc );
	if(shaders[1] == 0) {
		glDeleteShader(shaders[0]);
		return false;
	}

	GLuint prog = CreateShaderProgram(shaders);

	// The shaders aren't needed anymore once they're linked into the program
	glDeleteShader(shaders[0]);
	glDeleteShader(shaders[1]);
	if(prog == 0) {
		return false;
	}

	if(shaderProgram != 0) { // if we already had one and want to replace it
		glDeleteProgram(shaderProgram);
	}

	glUseProgram(prog);

	mvpMatrixUniform = glGetUniformLocation(prog, "mvpMatrix");
	if(mvpMatrixUniform == -1) {
		errprintf("Can't find mvpMatrix uniform in the shader?!\n");
		glUseProgram(0);
		glDeleteProgram(prog);
		return false;
	}

	float idmat[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};
	glUniformMatrix4fv(mvpMatrixUniform, 1, GL_FALSE, idmat);

	shaderProgram = prog;

	return true;
}

static void UpdateTextureFilter(bool bindTex = true)
{
	GLuint glTex = curTex.glTextureHandle;
	GLenum target = curTex.glTarget;
	if(glTex == 0) {
		return;
	}
	if(bindTex) {
		glBindTexture(target, glTex);
	}
	GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
	if(curTex.GetNumMips() == 1) {
		glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
	} else {
		GLint mipFilter = linearFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
		glTexParameteri(target, GL_TEXTURE_MIN_FILTER, mipFilter);
		glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
	}
}

static void LoadTexture(const char* path)
{
	{
		texview::Texture newTex;
		if(!newTex.Load(path)) {
			errprintf("Couldn't load texture '%s'!\n", path);
			return;
		}

		curTex = std::move(newTex);
	}
	// set windowtitle to filename (not entire path)
	{
		const char* fileName = strrchr(path, '/');
#ifdef _WIN32
		const char* lastBS = strrchr(path, '\\');
		if( lastBS != nullptr && (fileName == nullptr || fileName < lastBS) )
			fileName = lastBS;
#endif
		if(fileName == nullptr)
			fileName = path;
		else
			++fileName; // skip (back)slash

		char winTitle[256];
		snprintf(winTitle, sizeof(winTitle), "Texture Viewer - %s", fileName);

		glfwSetWindowTitle(glfwWindow, winTitle);
	}

	curTex.CreateOpenGLtexture();
	int numMips = curTex.GetNumMips();

	UpdateTextureFilter(false);
	if(numMips > 1) {
		if(mipmapLevel != -1) {
			// if it's set to auto, keep it at auto, otherwise default to 0
			mipmapLevel = 0;
		}
		int maxLevel = curTex.GetNumMips() - 1;
		glTexParameteri(curTex.glTarget, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(curTex.glTarget, GL_TEXTURE_MAX_LEVEL, maxLevel);
	}

	if(curTex.IsCubemap()) {
		float w, h;
		curTex.GetSize(&w, &h);
		ZoomFitToWindow(glfwWindow, w, h, true);
		spacingBetweenMips = 0;
	} else {
		spacingBetweenMips = 2;
	}

	textureArrayIndex = 0;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


	if(curTex.defaultSwizzle != nullptr) {
		strncpy(simpleSwizzle, curTex.defaultSwizzle, 4);
		simpleSwizzle[4] = '\0';
	} else {
		if(curTex.textureFlags & texview::TF_HAS_ALPHA) {
			strncpy(simpleSwizzle, "rgba", 5);
		} else {
			strncpy(simpleSwizzle, "rgb1", 5);
		}
	}
	useSimpleSwizzle = true;
	swizzle.clear();

	UpdateShaders();
}

struct vec4 {
	union {
		struct { float x, y, z, w; };
		float vals[4];
	};
	vec4() = default;
	vec4(float x_, float y_, float z_ = 0.0f, float w_ = 0.0f)
	: x(x_), y(y_), z(z_), w(w_) {}
};

struct VertexData {
	vec4 pos; // Note: pos.w holds desired mipLevel (LOD) or -1 for "automatically choose"
	vec4 tc;
};

static std::vector<VertexData> drawData;

// mipLevel -1 == use configured mipmapLevel
static void AddQuad(texview::Texture& texture, int mipLevel, int arrayIndex, ImVec2 pos, ImVec2 size, ImVec2 texCoordMax = ImVec2(1, 1))
{
	ImVec2 texCoordMin = ImVec2(0, 0);

	if(mipLevel < 0) {
		mipLevel = mipmapLevel;
	}

	float lod = std::min(mipLevel, texture.GetNumMips() - 1);
	float idx = arrayIndex;

	// vertices of the quad
	VertexData v1 = {
		{ pos.x, pos.y, 0.0f, lod },
		{ texCoordMin.x, texCoordMin.y, idx }
	};
	VertexData v2 = {
		{ pos.x, pos.y + size.y, 0.0f, lod },
		{ texCoordMin.x, texCoordMax.y, idx }
	};
	VertexData v3 = {
		{ pos.x + size.x, pos.y + size.y, 0.0f, lod },
		{ texCoordMax.x, texCoordMax.y, idx }
	};
	VertexData v4 = {
		{ pos.x + size.x, pos.y, 0.0f, lod },
		{ texCoordMax.x, texCoordMin.y, idx }
	};

	// now add the vertices for two triangles that draw the quad
	drawData.push_back(v1);
	drawData.push_back(v2);
	drawData.push_back(v3);

	drawData.push_back(v1);
	drawData.push_back(v3);
	drawData.push_back(v4);
}

enum CubeFaceIndex {
	FI_XPOS = 0,
	FI_XNEG = 1,
	FI_YPOS = 2,
	FI_YNEG = 3,
	FI_ZPOS = 4,
	FI_ZNEG = 5
};

// mipLevel -1 == use configured mipmapLevel
static void AddCubeQuad(texview::Texture& texture, int mipLevel, int faceIndex, int arrayIndex, ImVec2 pos, ImVec2 size, ImVec2 texCoordMax = ImVec2(1, 1))
{
	ImVec2 texCoordMin = ImVec2(0, 0);

	// helpful: https://stackoverflow.com/questions/38543155/opengl-render-face-of-cube-map-to-a-quad

	// scale from [0, 1] to [-1, 1]
	texCoordMin.x = texCoordMin.x * 2.0f - 1.0f;
	texCoordMin.y = texCoordMin.y * 2.0f - 1.0f;
	texCoordMax.x = texCoordMax.x * 2.0f - 1.0f;
	texCoordMax.y = texCoordMax.y * 2.0f - 1.0f;

	vec4 mapCoords[4] = {
		// initialize with x, y coordinates (or s,t or whatever)
		{ texCoordMin.x, texCoordMin.y },
		{ texCoordMin.x, texCoordMax.y },
		{ texCoordMax.x, texCoordMax.y },
		{ texCoordMax.x, texCoordMin.y }
	};

	for(vec4& mc : mapCoords) {
		vec4 tmp;
		switch(faceIndex) {
			case FI_XPOS:
				tmp = vec4( 1.0f, -mc.y, -mc.x );
				break;
			case FI_XNEG:
				tmp = vec4( -1.0f, -mc.y, mc.x );
				break;
			case FI_YPOS:
				tmp = vec4( mc.x, 1.0f, mc.y );
				break;
			case FI_YNEG:
				tmp = vec4( mc.x, -1.0f, -mc.y );
				break;
			case FI_ZPOS:
				tmp = vec4( mc.x, -mc.y, 1.0f );
				break;
			case FI_ZNEG:
				tmp = vec4( -mc.x, -mc.y, -1.0f );
				break;
			default: // should really not happen, but make compiler happy
				assert(0 && "invalid faceIndex");
				tmp = mc;
		}
		mc = tmp;
		mc.w = arrayIndex;
	}

	if(cubeCrossVariant > 0 && (faceIndex == FI_YPOS || faceIndex == FI_YNEG)) {
		int rotationSteps = (faceIndex == FI_YPOS) ? cubeCrossVariant : (4 - cubeCrossVariant);
		vec4 mapCoordsCopy[4];
		for(int i=0; i<4; ++i) {
			mapCoordsCopy[i] = mapCoords[ (i+rotationSteps) % 4 ];
		}
		memcpy(mapCoords, mapCoordsCopy, sizeof(mapCoords));
	}

	if(mipLevel < 0) {
		mipLevel = mipmapLevel;
	}

	float lod = std::min(mipLevel, texture.GetNumMips() - 1);

	// vertices of the quad
	VertexData v1 = {
		{ pos.x, pos.y, 0.0f, lod },
		mapCoords[0]
	};
	VertexData v2 = {
		{ pos.x, pos.y + size.y, 0.0f, lod },
		mapCoords[1]
	};
	VertexData v3 = {
		{ pos.x + size.x, pos.y + size.y, 0.0f, lod },
		mapCoords[2]
	};
	VertexData v4 = {
		{ pos.x + size.x, pos.y, 0.0f, lod },
		mapCoords[3]
	};

	// now add the vertices for two triangles that draw the quad
	drawData.push_back(v1);
	drawData.push_back(v2);
	drawData.push_back(v3);

	drawData.push_back(v1);
	drawData.push_back(v3);
	drawData.push_back(v4);
}

static void DrawQuads()
{
	glBindVertexArray(quadsVAO);

	glBindBuffer(GL_ARRAY_BUFFER, quadsVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(VertexData) * drawData.size(), drawData.data(), GL_STREAM_DRAW);

	glDrawArrays(GL_TRIANGLES, 0, drawData.size());

	drawData.clear();
}

static void DrawTexture()
{
	texview::Texture& tex = curTex;

	GLuint gltex = tex.glTextureHandle;
	if(!gltex) {
		return;
	}

	bool enableAlphaBlend = (tex.textureFlags & texview::TF_HAS_ALPHA) != 0;
	if(overrideAlpha != -1)
		enableAlphaBlend = overrideAlpha;
	if(enableAlphaBlend)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);

	int arrayIndex = textureArrayIndex;

	// this whole SRGB thing confuses me.. if the gl texture has an SRGB format
	// (like GL_SRGB_ALPHA), it must have GL_FRAMEBUFFER_SRGB enabled for drawing.
	// if it has a non-SRGB format (even if using the exact same pixeldata
	// e.g. from stb_image!) it must have GL_FRAMEBUFFER_SRGB disabled.
	// no idea what sense that's supposed to make (if all the information is in
	// the texture, why is there no magic to always make it look correct?),
	// but maybe it makes a difference when writing shaders?
	bool enableSRGB = (tex.textureFlags & texview::TF_SRGB) != 0;
	if(overrideSRGB != -1)
		enableSRGB = overrideSRGB;
	if(enableSRGB)
		glEnable( GL_FRAMEBUFFER_SRGB );
	else
		glDisable( GL_FRAMEBUFFER_SRGB );

	glBindTexture(tex.glTarget, gltex);

	float texW, texH;
	tex.GetSize(&texW, &texH);

	if(tex.IsCubemap()) {
		// render it as a scandinavian-flag style cross (those "Mittelchristen"
		//  can't decide between cross and inverted cross)
		// Y+ is always the upper square, Y- the lower square
		// between them are the remaining ones, by default X-, Z+, X+, Z-
		// extra feature of this texture viewer: cycle the middle ones (e.g. Z+, X+, Z+, X-)
		// and rotate the upper/lower ones accordingly

		const float offset = texW + spacingBetweenMips; // texW = texH
		float posX = offset;
		float posY = 0.0f;
		const ImVec2 size(texW, texH);
		AddCubeQuad(tex, -1, FI_YPOS, arrayIndex, ImVec2(posX, posY), size);

		posX = 0.0f;
		posY += offset;
		const int middleIndices[4] = { FI_XNEG, FI_ZPOS, FI_XPOS, FI_ZNEG };
		for(int i=cubeCrossVariant, n=cubeCrossVariant+4; i < n; ++i) {
			int faceIndex = middleIndices[i % 4];
			AddCubeQuad(tex, -1, faceIndex, arrayIndex, ImVec2(posX, posY), size);
			posX += offset;
		}
		posX = offset;
		posY += offset;

		AddCubeQuad(tex, -1, FI_YNEG, arrayIndex, ImVec2(posX, posY), size);

		DrawQuads();

		glDisable( GL_FRAMEBUFFER_SRGB ); // make sure it's disabled or ImGui will look wrong
		return;
	}

	if(viewMode == SINGLE) {
		AddQuad(tex, -1, arrayIndex, ImVec2(0, 0), ImVec2(texW, texH));
	} else if(viewMode == TILED) {
		float tilesX = numTiles[0];
		float tilesY = numTiles[1];
		ImVec2 size(texW*tilesX, texH*tilesY);
		AddQuad(tex, -1, arrayIndex, ImVec2(0, 0), size, ImVec2(tilesX, tilesY));
	} else if(viewAtSameSize) {
		int numMips = tex.GetNumMips();
		if(viewMode == MIPMAPS_COMPACT) {
			// try to have about the same with and height
			// (but round up because more horizontally is preferable due to displays being wide)
			int numHor = ceil(sqrtf(numMips * texH / texW));
			float posX = 0.0f;
			float posY = 0.0f;
			float hOffset = texW + spacingBetweenMips;
			float vOffset = texH + spacingBetweenMips;
			for(int i=0; i < numMips; ++i) {
				AddQuad(tex, i, arrayIndex, ImVec2(posX, posY), ImVec2(texW, texH));
				if(((i+1) % numHor) == 0) {
					posY += vOffset;
					// change horizontal direction every line
					// so the next level of the last mip of one line
					// is right below it instead of the start of the next line
					hOffset = -hOffset;
				} else {
					posX += hOffset;
				}
			}
		} else if(viewMode == MIPMAPS_ROW || viewMode == MIPMAPS_COLUMN) {
			float hOffset = (viewMode == MIPMAPS_ROW) ? texW + spacingBetweenMips : 0.0f;
			float vOffset = (viewMode == MIPMAPS_ROW) ? 0.0f : texH + spacingBetweenMips;
			float posX = 0.0f;
			float posY = 0.0f;
			for(int i=0; i < numMips; ++i) {
				AddQuad(tex, i, arrayIndex, ImVec2(posX, posY), ImVec2(texW, texH));
				posX += hOffset;
				posY += vOffset;
			}
		} else {
			assert(0 && "unknown viewmode?!");
		}

	} else { // don't view at same size
		int numMips = tex.GetNumMips();
		if(viewMode == MIPMAPS_COMPACT) {

			bool toRight = (texW/texH <= 1.2f); // otherwise down

			// below I adjust the spacing between mipmaps so it's not absurdly big
			// for the smallest mips, by limiting it to half the current mipmap width or height
			// but I also want to make sure that it's at least 2 pixels
			// UNLESS spacingBetweenMips is smaller than that.
			// using minSpace instead of 2 helps with that.
			float minSpace = std::min(2, spacingBetweenMips);

			float posX = 0.0f;
			float posY = 0.0f;
			for(int i=0; i < numMips; ++i) {
				float w, h;
				tex.GetMipSize(i, &w, &h);
				AddQuad(tex, i, arrayIndex, ImVec2(posX, posY), ImVec2(w, h));

				if( (toRight && (i & 1) == 0)
				   || (!toRight && (i & 1) == 1) ) {
					float space = std::max(minSpace, std::min(float(spacingBetweenMips), w * 0.5f));
					posX += space + w;
				} else {
					float space = std::max(minSpace, std::min(float(spacingBetweenMips), h * 0.5f));
					posY += space + h;
				}
			}

		} else if(viewMode == MIPMAPS_ROW || viewMode == MIPMAPS_COLUMN) {
			bool inRow = (viewMode == MIPMAPS_ROW);
			float posX = 0.0f;
			float posY = 0.0f;
			for(int i=0; i < numMips; ++i) {
				float w, h;
				tex.GetMipSize(i, &w, &h);
				AddQuad(tex, i, arrayIndex, ImVec2(posX, posY), ImVec2(w, h));
				if(inRow) {
					posX += spacingBetweenMips + w;
				} else {
					posY += spacingBetweenMips + h;
				}
			}
		} else {
			assert(0 && "unknown viewmode?!");
		}
	}

	DrawQuads();

	glDisable( GL_FRAMEBUFFER_SRGB ); // make sure it's disabled or ImGui will look wrong
}

static void GenericFrame(GLFWwindow* window)
{
	int display_w, display_h;
	glfwGetFramebufferSize(window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
	             clear_color.z * clear_color.w, clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);

	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	ImVec2 imguiCoordScale = ImGui::GetIO().DisplayFramebufferScale;

	float xOffs = imguiMenuCollapsed ? 0.0f : imguiMenuWidth * imguiCoordScale.x;
	float winW = display_w - xOffs;
	if(winW <= 0.0f) { // most probably very high imgui scale
		return;
	}

	glUseProgram(shaderProgram);

	float mvp[4][4] = {};
	glViewport(xOffs, 0, winW, display_h);

	// ortho like glOrtho(0, winW, display_h, 0, -1, 1);
	{
		float left = 0, right = winW, bottom = display_h, top = 0, near = -1, far = 1;
		mvp[0][0] = 2.0f / (right - left);
		mvp[1][1] = 2.0f / (top - bottom);
		mvp[2][2] = 2.0f / (near - far);
		mvp[3][3] = 1.0f;

		mvp[3][0] = (left + right) / (left - right);
		mvp[3][1] = (bottom + top) / (bottom - top);
		mvp[3][2] = (near + far) / (near - far);
	}
	// scale with (zoomLevel, zoomLevel, 1.0)
	mvp[0][0] *= zoomLevel;
	mvp[1][1] *= zoomLevel;
	// translate by ((transX * sx) / zoomLevel, (transY * sy) / zoomLevel, 0.0)
	{
		float tx = (transX * imguiCoordScale.x) / zoomLevel;
		float ty = (transY * imguiCoordScale.y) / zoomLevel;
		mvp[3][0] += mvp[0][0] * tx;
		mvp[3][1] += mvp[1][1] * ty;
	}

	glUniformMatrix4fv(mvpMatrixUniform, 1, GL_FALSE, mvp[0]);

	DrawTexture();
}


static void OpenFilePicker() {
#ifdef TV_USE_NFD
		nfdopendialogu8args_t args = {0};
		//args.filterList = filters;
		//args.filterCount = 2;
		std::string dp;
		if(!curTex.name.empty()) {
			dp = curTex.name;
			size_t lastSlash = dp.find_last_of('/');
	#ifdef _WIN32
			size_t lastBS = dp.find_last_of('\\');
			if( (lastBS != std::string::npos && lastBS > lastSlash)
			   || lastSlash == std::string::npos )
			{
				lastSlash = lastBS;
			}
	#endif
			if(lastSlash != std::string::npos) {
				dp.resize(lastSlash);
				args.defaultPath = dp.c_str();
			}
		}
		nfdu8char_t* outPath = nullptr;
		nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
		if(result == NFD_OKAY) {
			LoadTexture(outPath);
		}
		if(outPath != nullptr) {
			NFD_FreePathU8(outPath);
		}
#else
		// TODO: imgui-only alternative, maybe https://github.com/aiekick/ImGuiFileDialog
		errprintf("Built without NativeFileDialog support, have no alternative (yet)!\n");
#endif
}

// should return the correct value even if ImGui itself hasn't updated
// io.DisplayFramebufferScale yet
static ImVec2 GetImGuiDisplayScale(GLFWwindow* window)
{
	int fbW=0, fbH=0;
	glfwGetFramebufferSize(window, &fbW, &fbH);
	int winW=0, winH=0;
	glfwGetWindowSize(window, &winW, &winH);
	// Note: io.DisplayFramebufferScale isn't set yet, so calculate the same value here..
	return ImVec2(float(fbW)/winW, float(fbH)/winH);
}

static void DrawAboutWindow(GLFWwindow* window)
{
	ImGuiIO& io = ImGui::GetIO();

	ImGui::SetNextWindowPos( ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
	                         ImGuiCond_Appearing, ImVec2(0.5f, 0.5f) );
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
	                        | ImGuiWindowFlags_NoCollapse;
	if(ImGui::Begin("About", &showAboutWindow, flags)) {
		ImGui::TextDisabled("A texture viewer.");
		ImGui::TextDisabled("              v"  texview_version);

		ImGui::Spacing();
		ImGui::Text("Zoom with the mouse wheel,\nmove texture by dragging mouse.");
		ImGui::Text("Press R to reset view.");
		ImGui::Text("You can Ctrl-Click into sliders and\n"
		            "similar to enter the value as text.");
		ImGui::Spacing();

		ImGui::BeginDisabled();
		ImGui::Text("(C) 2025 Daniel Gibson");
		ImGui::Spacing();
		ImGui::Text("Released under MIT license.");
		ImGui::Text("Uses several libraries including GLFW,\n"
		            "Dear ImGui, Native File Dialog Extended,\nstb_image.h and libktx.");
		ImGui::Text("See Licenses.txt for details.");
		ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::TextLinkOpenURL("https://github.com/DanielGibson/texview");
		ImGui::TextLinkOpenURL("https://blog.gibson.sh");
		ImGui::Spacing();
		ImGui::Spacing();

		float dialogButtonWidth = ImGui::CalcTextSize( "Ok or Cancel ???" ).x; // this width looks ok
		float buttonOffset = (ImGui::GetWindowWidth() - dialogButtonWidth) * 0.5f;
		ImGui::SetCursorPosX( buttonOffset );
		if( ImGui::Button("Close", ImVec2(dialogButtonWidth, 0))
		   || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ) {
			showAboutWindow = false;
		}
	}
	ImGui::End();
}

static void DrawGLSLeditWindow(GLFWwindow* window)
{
	ImGuiIO& io = ImGui::GetIO();

	ImVec2 winSize(440*texview::imguiAdditionalScale, 0);
	ImGui::SetNextWindowSize(winSize, ImGuiCond_Appearing);
	ImGui::SetNextWindowPos( ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
	                         ImGuiCond_Once, ImVec2(0.5f, 0.5f) );

	ImGuiWindowFlags flags = 0;
	if(ImGui::Begin("Advanced Swizzling", &showGLSLeditWindow, flags)) {
		ImGui::TextDisabled("%s", texSampleAndNormalize.c_str());

		static char buf[4096] = {};
		if(ImGui::IsWindowAppearing()) {
			size_t len = std::min(swizzle.size(), sizeof(buf)-1);
			memcpy(buf, swizzle.c_str(), len);
			buf[len] = '\0';
		}

		ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
		ImGui::SetNextItemWidth(-8.0f);
		if(ImGui::InputTextMultiline("##glslcode", buf, sizeof(buf), ImVec2(0, 0), flags)) {
			swizzle = buf;
		}

		ImGui::TextDisabled(" OutColor = c;");
		ImGui::Spacing();

		bool haveFocus = ImGui::IsWindowFocused();

		float buttonWidth = ImGui::CalcTextSize("Close or what").x;
		if( ImGui::Button("Apply", ImVec2(buttonWidth, 0.0f))
		   || (haveFocus && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter)) ) {
			UpdateShaders();
		}
		ImGui::SetItemTooltip("Alternatively you can press Ctrl+Enter to apply");

		ImGui::SameLine();
		float buttonOffset = (ImGui::GetWindowWidth() - buttonWidth - 8.0f - ImGui::GetStyle().WindowPadding.x);
		ImGui::SetCursorPosX(buttonOffset);
		if( ImGui::Button("Close", ImVec2(buttonWidth, 0.0f))
		    || (haveFocus && ImGui::IsKeyPressed(ImGuiKey_Escape)) ) {
			showGLSLeditWindow = false;
		}
	}
	ImGui::End();
}

static void DrawSidebar(GLFWwindow* window)
{

	ImGuiViewport* mainViewPort = ImGui::GetMainViewport();
	// NOTE: in ImGui's docking branch, positions are in global (screen/OS) coordinates
	ImGui::SetNextWindowPos(mainViewPort->Pos, ImGuiCond_Always);
	if(!imguiMenuCollapsed) {
		ImGui::SetNextWindowSize(ImVec2(0, mainViewPort->Size.y), ImGuiCond_Always);
	}

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
	if(ImGui::Begin("##options", NULL, flags)) {
		const ImGuiStyle& style = ImGui::GetStyle();
		if(ImGui::Button("Open File")) {
			OpenFilePicker();
		}
		float fontWrapWidth = ImGui::CalcTextSize("0123456789abcdef0123456789ABCDEF").x;
		ImGui::PushTextWrapPos(fontWrapWidth);
		float texWidth, texHeight;
		curTex.GetSize(&texWidth, &texHeight);
		bool isCubemap = curTex.IsCubemap();
		bool texHasAlpha = (curTex.textureFlags & texview::TF_HAS_ALPHA) != 0;
		bool texIsSRGB = (curTex.textureFlags & texview::TF_SRGB) != 0;

		float unindentWidth = style.FramePadding.x;
		// move the treenode arrow a bit to the left to waste less space
		ImGui::Unindent(unindentWidth);
		if(ImGui::TreeNode("Texture Info")) { // ImGuiTreeNodeFlags_SpanFullWidth ?
			// move the treenode contents a bit to the left to waste less space
			ImGui::Unindent(unindentWidth);
			//ImGui::TextWrapped("File: %s", curTex.name.c_str());
			ImGui::Text("File: ");
			ImGui::BeginDisabled(true);
			ImGui::TextWrapped("%s", curTex.name.c_str());
			ImGui::EndDisabled();
			ImGui::Text("Format: %s", curTex.formatName.c_str());
			ImGui::Text("Texture Size: %d x %d", (int)texWidth, (int)texHeight);
			ImGui::Text("MipMap Levels: %d", curTex.GetNumMips());
			int numCubeFaces = curTex.GetNumCubemapFaces();
			if(curTex.IsArray()) {
				ImGui::Text("%sArray Layers: %d", isCubemap ? "Cubemap " : "", curTex.GetNumElements());
			} else if(isCubemap) {
				if(numCubeFaces == 6) {
					ImGui::Text("Cubemap Texture");
				} else {
					ImGui::Text("Cubemap Texture with %d faces", curTex.GetNumCubemapFaces());
				}
			}
			const char* alphaStr = "no";
			if(texHasAlpha) {
				alphaStr = (curTex.textureFlags & texview::TF_PREMUL_ALPHA) ? "Premultiplied" : "Straight";
			}
			ImGui::Text("Alpha: %s - sRGB: %s", alphaStr, texIsSRGB ? "yes" : "no");
			ImGui::Indent(unindentWidth);
			ImGui::TreePop();
		} else {
			ImGui::SetItemTooltip( "Click to show information about the Texture" );
		}
		ImGui::Indent(unindentWidth);

		ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
		ImGui::PushItemWidth(fontWrapWidth - ImGui::CalcTextSize("View Mode  ").x);
		float zl = zoomLevel;
		if(ImGui::SliderFloat("Zoom", &zl, 0.0125, 50.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) {
			zoomLevel = zl;
		}
		if(ImGui::Button("Fit to Window")) {
			ZoomFitToWindow(window, texWidth, texHeight, isCubemap);
		}
		ImGui::SameLine();
		if(ImGui::Button("Reset Zoom")) {
			zoomLevel = 1.0;
		}
		if(ImGui::Button("Reset Position")) {
			transX = transY = 10.0;
		}

		ImGui::Spacing();

		int vMode = viewMode;
		if(curTex.IsCubemap()) {
			ImGui::SliderInt("View Mode##cube", &cubeCrossVariant, 0, 3, "%d", ImGuiSliderFlags_AlwaysClamp);

			ImGui::SliderInt("Spacing", &spacingBetweenMips, 0, 32, "%d pix");
		} else { // not cubemap
			if(ImGui::Combo("View Mode", &vMode, "Single\0MipMaps Compact\0MipMaps in Row\0MipMaps in Column\0Tiled\0")) {
				// zoom out when not single, so everything (or at least more) is on the screen
				// TODO: do some calculation for good amount of zooming out here?
				if(viewMode == SINGLE && vMode != SINGLE) {
					zoomLevel *= 0.5;
				}
				viewMode = (ViewMode)vMode;
			}
			if(vMode != SINGLE && vMode != TILED) {
				ImGui::Checkbox("Show MipMaps at same size", &viewAtSameSize);
				ImGui::SliderInt("Spacing", &spacingBetweenMips, 0, 32, "%d pix");
				ImGui::SetItemTooltip("Spacing between mips");
			} else if(vMode == TILED) {
				ImGui::InputInt2("Tiles", numTiles);
			}
		}
		if(isCubemap || vMode == SINGLE || vMode == TILED) {
			int mipLevel = mipmapLevel;
			int maxLevel = std::max(0, curTex.GetNumMips() - 1);
			if(maxLevel == 0) {
				ImGui::BeginDisabled(true);
				ImGui::SliderInt("LOD", &mipLevel, 0, 1, "0 (No Mip Maps)");
				ImGui::EndDisabled();
			} else {
				const char* miplevelString = "Auto"; // (normal mip mapping)";
				char miplevelStrBuf[64] = {};
				if(mipLevel >= 0) {
					mipLevel = std::min(mipLevel, maxLevel);
					miplevelString = miplevelStrBuf;
					float w, h;
					curTex.GetMipSize(mipLevel, &w, &h);
					snprintf(miplevelStrBuf, sizeof(miplevelStrBuf), "%d (%dx%d)",
					         mipLevel, (int)w, (int)h);
				}
				if(ImGui::SliderInt("Mip Level", &mipLevel, -1, maxLevel,
				                    miplevelString, ImGuiSliderFlags_AlwaysClamp)) {
					mipmapLevel = mipLevel;
				}
			}
		}
		if(curTex.IsArray()) {
			int numElems = curTex.GetNumElements();
			ImGui::SliderInt("Layer", &textureArrayIndex, 0, numElems-1,
			                 "%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SetItemTooltip("Index in Texture Array");
		}

		ImGui::Spacing();
		int texFilter = linearFilter;
		if(ImGui::Combo("Filter", &texFilter, "Nearest\0Linear\0")) {
			if(texFilter != (int)linearFilter) {
				linearFilter = texFilter != 0;
				UpdateTextureFilter();
			}
		}

		int srgb = overrideSRGB + 1 ; // -1 => 0 etc
		const char* srgbStr = texIsSRGB ? "Tex Default (sRGB)\0Force Linear\0Force sRGB\0"
		                                : "Tex Default (Linear)\0Force Linear\0Force sRGB\0";
		if(ImGui::Combo("sRGB", &srgb, srgbStr)) {
			overrideSRGB = srgb - 1;
		}
		ImGui::SetItemTooltip("Override if texture is assumed to have sRGB or Linear data");

		int alpha = overrideAlpha + 1; // -1 => 0 etc
		const char* alphaSelStr = texHasAlpha ? "Tex Default (on)\0Force Disable\0Force Enable\0"
		                                      : "Tex Default (off)\0Force Disable\0Force Enable\0";
		if(ImGui::Combo("Alpha", &alpha, alphaSelStr)) {
			overrideAlpha = alpha - 1;
		}
		ImGui::SetItemTooltip("Enable/Disable Alpha Blending");

		if(useSimpleSwizzle) {
			ImGuiInputTextFlags swizzleInputFlags = ImGuiInputTextFlags_CallbackCharFilter;
			ImGuiInputTextCallback swizzleInputCB = [](ImGuiInputTextCallbackData* data) -> int {
				// according to the documentation, returning 1 here skips the char
				// probably returning 0 means "use it, it's valid"
				const char* validChars = "rgbaRGBAxyzwXYZW01";
				int c = data->EventChar;
				if(c < '0' || c > 'z') {
					return 1; // definitely invalid
				}
				return strchr(validChars, c) == nullptr;
			};
			if( ImGui::InputText("Swizzle", simpleSwizzle, sizeof(simpleSwizzle), swizzleInputFlags, swizzleInputCB) ) {
				UpdateShaders();
			}
			ImGui::SetItemTooltip("Swizzles the color channels. Four characters,\n"
			                      "for the Red, Green, Blue and Alpha channels.\n"
			                      "Valid characters: r, g, b, a, x, y, z, w, 0, 1\n"
			                      "0 and 1 set the color channel to that value,\n"
			                      "the others set the color channel to the value of the given channel.\n"
			                      "Default: \"rgba\" if texture has alpha channel, else \"rgb1\"\n");
		} else {
			ImGui::Text("Using advanced Swizzling:");
			ImGui::BeginDisabled();
			ImGui::Text("%.*s ...", 24, swizzle.c_str());
			ImGui::EndDisabled();
			if(ImGui::Button("Edit advanced Swizzling")) {
				showGLSLeditWindow = true;
			}
		}
		bool useAdvancedSwizzle = !useSimpleSwizzle;
		if(ImGui::Checkbox("Use advanced Swizzling", &useAdvancedSwizzle)) {
			useSimpleSwizzle = !useAdvancedSwizzle;
			if(useAdvancedSwizzle && simpleSwizzle[0] == '\0') {
				// in case no simple swizzle was set, set the default one now
				// so the advanced swizzle text isn't empty
				memcpy(simpleSwizzle, "rgba", 5);
				SetSwizzleFromSimple();
			}
		}

		ImGui::Spacing(); ImGui::Spacing();

		ImGui::ColorEdit3("BG Color", &clear_color.x);
		ImGui::Spacing(); ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing(); ImGui::Spacing();
		float aboutButtonWidth = ImGui::CalcTextSize( "About blah" ).x; // this width looks ok
		ImGui::SetCursorPosX( (ImGui::GetWindowWidth() - aboutButtonWidth) * 0.5f );
		if(ImGui::Button("About")) {
			showAboutWindow = true;
		}
		ImGui::Dummy(ImVec2(8, 32));

		ImGui::PushItemWidth( ImGui::CalcTextSize("10.0625+-").x
		                      + (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x) * 2.0f );
		ImGui::InputFloat("UI Scale", &imguiScale, 0.0625f, 0.25f, "%.4f");
		if(ImGui::IsItemDeactivatedAfterEdit()) {
			updateFont = true;
		}
		ImGui::SetItemTooltip("Adjust the size of the UI (like this sidebar)");
		if(ImGui::Button("Show Log Window")) {
			texview::LogWindowShow();
		}

#if 0 // for debugging scaling issues
		{
			int winW, winH;
			int fbW, fbH;
			float scaleX, scaleY;
			glfwGetWindowSize(window, &winW, &winH);
			glfwGetFramebufferSize(window, &fbW, &fbH);
			glfwGetWindowContentScale(window, &scaleX, &scaleY);

			ImGui::Text("GLFW log WinSize: %d x %d", winW, winH);
			ImGui::Text("GLFW FB size:     %d x %d", fbW, fbH);
			ImGui::Text("     => ratio: %g ; %g", float(fbW)/winW, float(fbH)/winH);
			ImGui::Text("GLFW WinScale: %g ; %g", scaleX, scaleY);
		}
#endif

		ImGui::Checkbox("Show ImGui Demo Window", &showImGuiDemoWindow);
		imguiMenuWidth = ImGui::GetWindowWidth();
	}
	imguiMenuCollapsed = ImGui::IsWindowCollapsed();
	ImGui::End();
}

static void SetImGuiStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	// reset to default, esp. relevant for the sizes
	style = defaultStyle;

	ImGui::StyleColorsDark(&style);
	// make it look a bit nicer with rounded edges
	style.WindowRounding = 2.0f;
	style.FrameRounding = 3.0f;
	style.FramePadding = ImVec2( 6.0f, 3.0f );
	//style.ChildRounding = 6.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding = 3.0f;
	style.PopupRounding = 2.0f;
}

static void UpdateFontsAndScaling(GLFWwindow* window)
{
	/* This here is about scaling sizes of ImGui fonts and styling parameters,
	 * mostly for High-DPI (Retina, whatever) displays.
	 * Operating systems/windowing systems handle "High-DPI" in two (3) different ways:
	 * 1. Both the framebuffer and window coordinates (used for window size and
	 *    mouse coordinates) are in physical pixels, just like they are in traditional
	 *    display modes. The operating/windowing system somehow communicates how
	 *    much the application should scale its user interface.
	 *    This is what MS Windows and X11 do, at least in the modes used by glfw.
	 * 2. The framebuffer is in physical pixels, but window coordinates (window size,
	 *    mouse coordinates, ...) are in logical points that have a lower resolution.
	 *    For example on macOS with High-DPI "Retina" displays, requesting
	 *    a 1280x720 window gives you a window that is displayed as 2560x1440 pixels
	 *    on your screen (and that's also the size of the framebuffer), but
	 *    mouse coordinates (or values used for resizing the window etc) pretend
	 *    it's 1280x720.
	 *    Wayland does the same, but there odd scaling factors (like 125%) are
	 *    more common (on macOS it seems to always be 200% with "Retina" displays).
	 * (3. Both framebuffer and window coordinates are in logical points and the
	 *    operating/windowing system scales up to physical pixels.
	 *    Good for backwards-compatibility with applications that are not High-DPI
	 *    aware, but blurry - I obviously don't want that)
	 *
	 * "Physical pixel" means that one pixel that you can draw/address in your
	 * window corresponds to one pixel on your screen. (At least if you configured
	 * the Desktop resolution in your operating/windowing system to the native
	 * resolution of the display.)
	 *
	 * glfw has three functions related to this:
	 * - glfwGetFramebufferSize(), which returns the window's framebuffer size
	 *   in physical pixels
	 * - glfwGetWindowSize(), which returns the window size in logical points
	 * - glfwGetWindowContentScale() which returns how much content should be
	 *   scaled, according to the operating/windowing system.
	 *   On macOS and Wayland this is just framebuffer_size_in_pixels / window_size_in_points,
	 *   but on Windows and X11 that ratio is always 1.0 and the content scale
	 *   tells you by how much your user interface should be scaled up (e.g. 1.5)
	 *
	 * ImGui uses logical points for all sizes and coordinates and when it renders,
	 * that is scaled by io.DisplayFramebufferScale, which is calculated by
	 * framebuffer_size_in_pixels / window_size_in_points, so it's the x and y factor
	 * needed to turn a logical point coordinate into a physical pixel coordinate.
	 *
	 * This means that on macOS and Wayland ImGui does the scaling for us (though
	 * it benefits from some tweaking of font parameters), while on Windows and X11
	 * we need to do the scaling ourselves by using a bigger font size and scaling
	 * up the sizes used by the style (style.ScaleAllSizes(scale)).
	 *
	 * texview also lets you set your own "ImGui Scale" which is applied additionally,
	 * in that case even on macOS/Wayland the font and style sizes are scaled up.
	 *
	 * All cases are handled together here, there are no explicit code-paths per
	 * operating/windowing system, which should also make this more future-proof.
	 */

	// destroy the old font(s) before loading a new one
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();
	ImGui_ImplOpenGL3_DestroyFontsTexture();

	// how much should the UI be scaled, according to the operating/windowing system?
	float xscale = 0.0f, yscale = 0.0f;
	glfwGetWindowContentScale(window, &xscale, &yscale);

	// how much of that scaling is already done by ImGui?
	// (this is basically io.DisplayFramebufferScale, but we can't use that here
	//  as it may not be set yet. GetImGuiDisplayScale() does the same calculation.)
	ImVec2 imguiCoordScale = GetImGuiDisplayScale(window);

	// how much scaling needs to be done "manually"?
	float sx = xscale / imguiCoordScale.x;
	float sy = yscale / imguiCoordScale.y;

	// user-configured scaling must also be applied
	sx *= imguiScale;
	sy *= imguiScale;

	// both the font and the style must be scaled by a single factor
	// (usually they're the same anyway, +/- some rounding, but theoretically
	//  devices can have different horizontal and vertical pixels-per-inch values.
	//  I think some smartphones actually do this?)
	float ourImguiScale = std::max(sx, sy);
	texview::imguiAdditionalScale = ourImguiScale;

	ImFontConfig fontCfg = {};
	strcpy(fontCfg.Name, "ProggyVector");
	float fontSize = 16.0f * ourImguiScale;
	// RasterizerDensity allows increasing the font "density" without changing
	// its logical size. Increasing it by the scale ImGui already applies
	// compensates for ImGui loading the font with a *logical* size (in points)
	// and then scaling it up to the physical size for rendering.
	// ("Should" because the fontsize passed to ImGui is rounded to integer and
	//  then ImGui applies the RasterizerDensity when loading so it might not be
	//  loaded as an integer size after all, which might make it look less perfect
	//  than it could. But this usually looks pretty good, and e.g. 125% scaling
	//  should be fine as 1.25 * 16 = 20, same for any multiples of 1/16 = 0.0625)
	fontCfg.RasterizerDensity = std::max(imguiCoordScale.x, imguiCoordScale.y);
	float fontSizeInt = std::max(1.0f, roundf(fontSize)); // font sizes are supposed to be rounded to integers (and > 0)
	io.Fonts->AddFontFromMemoryCompressedTTF(ProggyVector_compressed_data, ProggyVector_compressed_size, fontSizeInt, &fontCfg);

	// ScaleAllSizes() does exactly that, so calling it twice would scale the sizes twice..
	// so first reset the style so it has its default sizes, that are then scaled
	// (only!) by ourImguiScale
	SetImGuiStyle();
	ImGui::GetStyle().ScaleAllSizes(ourImguiScale);
}

static void ImGuiFrame(GLFWwindow* window)
{
	// I think right before a new imgui frame is the safest place to
	// update (reload) the font (which is done on start and if scaling changes)
	if(updateFont) {
		UpdateFontsAndScaling(window);
		updateFont = false;
	}

	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	if(showImGuiDemoWindow)
		ImGui::ShowDemoWindow(&showImGuiDemoWindow);

	if(showAboutWindow)
		DrawAboutWindow(window);

	if(showGLSLeditWindow)
		DrawGLSLeditWindow(window);

	DrawSidebar(window);

	texview::DrawLogWindow(); // whether it should be shown is handled there (logging.cpp)

	// NOTE: ImGui::GetMouseDragDelta() is not very useful here, because
	//       I only want drags that start outside of ImGui windows
	bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	if( dragging || (mouseDown && !ImGui::GetIO().WantCaptureMouse) ) {
		ImVec2 mousePos = ImGui::GetMousePos();
		if(mouseDown) {
			if(dragging) {
				float dx = mousePos.x - lastDragPos.x;
				float dy = mousePos.y - lastDragPos.y;
				transX += dx;
				transY += dy;
				lastDragPos = mousePos;
			} else {
				lastDragPos = mousePos;
				dragging = true;
			}
		} else { // left mousebutton not down (anymore) => stop dragging
			dragging = false;
		}
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// Update and Render additional Platform Windows
	// (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
	//  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		GLFWwindow* backup_current_context = glfwGetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		glfwMakeContextCurrent(backup_current_context);
	}
}

static double CalcZoomLevel(double zl, bool increase)
{
	if(increase) {
		if(zl >= 2.0)
			zl += 0.5;
		else if(zl >= 1.0)
			zl += 0.25;
		else if (zl >= 0.125)
			zl += 0.125;
		else
			zl *= sqrt(2.0);
	} else {
		if(zl <= 0.125)
			zl *= 1.0/sqrt(2.0);
		else if(zl <= 1.0)
			zl -= 0.125;
		else if(zl <= 2.0)
			zl -= 0.25;
		else
			zl -= 0.5;
	}

	if(zl >= 1.0) {
		double nearestHalf = round(zl*2.0)*0.5;
		if(fabs(nearestHalf - zl) <= std::min(0.25, 0.1 * zl)) {
			return nearestHalf;
		}
	} else if(zl > 0.25) {
		double nearestEighth = round(zl*8.0)*0.125;
		if(fabs(nearestEighth - zl) <= 0.05) {
			return nearestEighth;
		}
	}
	return zl;
}

static void myGLFWscrollfun(GLFWwindow* window, double xoffset, double yoffset)
{
	// ImGui_ImplSDL2_ProcessEvent() doc says:
	//   You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
	//   - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
	//   - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
	//   Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
	if(yoffset == 0 || ImGui::GetIO().WantCaptureMouse) {
		return;
	}

	zoomLevel = CalcZoomLevel(zoomLevel, yoffset > 0.0);
}

static void myGLFWkeyfun(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	// while io.WantCaptureKeyboard doesn't work well (it returns true if an
	// ImGui window has focus, even if no text input is active), this seems to
	// do exactly what I want (i.e. let me ignore keys only if one is currently
	// typing text into some ImGui widget)
	if(ImGui::GetIO().WantTextInput) {
		return;
	}

	if(key == GLFW_KEY_R) {
		zoomLevel = 1.0;
		transX = 10.0;
		transY = 10.0;
	}
}

static void myGLFWwindowcontentscalefun(GLFWwindow* window, float xscale, float yscale)
{
	updateFont = true;
}

/*
 * Callback function for debug output.
 */
static void APIENTRY
GLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                const GLchar *message, const void *userParam)
{
	const char* sourceStr = "Source: Unknown";
	const char* typeStr = "Type: Unknown";
	const char* severityStr = "Severity: Unknown";

	switch (severity)
	{
#define SVRCASE(X, STR)  case GL_DEBUG_SEVERITY_ ## X ## _ARB : severityStr = STR; break;
		// GL_DEBUG_SEVERITY_NOTIFICATION_ARB is not in the glad header
		// (not specified in GL_ARB_debug_output I think?) but drivers send such
		// messages anyway. I don't want them so just return when getting that value
#if 01 // allow to quickly enable notification messages as well
		case 0x826B:  return;
#else
		case 0x826B: severityStr = "Severity: Notification"; break;
#endif
		SVRCASE(HIGH, "Severity: High")
		SVRCASE(MEDIUM, "Severity: Medium")
		SVRCASE(LOW, "Severity: Low")
#undef SVRCASE
	}

	switch (source)
	{
#define SRCCASE(X)  case GL_DEBUG_SOURCE_ ## X ## _ARB: sourceStr = "Source: " #X; break;
		SRCCASE(API);
		SRCCASE(WINDOW_SYSTEM);
		SRCCASE(SHADER_COMPILER);
		SRCCASE(THIRD_PARTY);
		SRCCASE(APPLICATION);
		SRCCASE(OTHER);
#undef SRCCASE
	}

	switch(type)
	{
#define TYPECASE(X)  case GL_DEBUG_TYPE_ ## X ## _ARB: typeStr = "Type: " #X; break;
		TYPECASE(ERROR);
		TYPECASE(DEPRECATED_BEHAVIOR);
		TYPECASE(UNDEFINED_BEHAVIOR);
		TYPECASE(PORTABILITY);
		TYPECASE(PERFORMANCE);
		TYPECASE(OTHER);
#undef TYPECASE
	}

	errprintf("GLDBG %s %s %s: %s\n", sourceStr, typeStr, severityStr, message);
}


#ifdef _WIN32
int my_main(int argc, char** argv) // called from WinMain() in sys_win.cpp
#else
int main(int argc, char** argv)
#endif
{
	int ret = 0;
	static std::string imguiIniPath;
	imguiIniPath = texview::GetSettingsDir();
	// make sure the settings directory exists so imgui.ini and maybe logs
	// can be written there
	texview::CreatePathRecursive(&imguiIniPath.front());

	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit()) {
		errprintf("glfwInit() failed! Exiting..\n");
		return 1;
	}

#ifdef TV_USE_NFD
	if (NFD_Init() != NFD_OKAY) {
		errprintf("Couldn't initialize Native File Dialog library!\n");
		glfwTerminate();
		return 1; // TODO: instead start and just don't provide a file picker?
	}
#endif

	const char* glDebugEnv = getenv("TEXVIEW_GLDEBUG");
	bool wantDebugContext = (glDebugEnv != nullptr && atoi(glDebugEnv) != 0);

	// Create window with graphics context
	const char* glsl_version = "#version 150"; // for ImGui
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	#ifdef __APPLE__
	  // https://www.khronos.org/opengl/wiki/OpenGL_Context#Forward_compatibility
	  // says forward compat should only be enabled on macOS
	  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	#endif
	if(wantDebugContext) {
		glfwWindowHint(GLFW_CONTEXT_DEBUG, GLFW_TRUE);
	}
	glfwWindowHint(GLFW_SRGB_CAPABLE, 1); // FIXME: this doesn't seem to make a difference visually or in behavior?!
	glfwWindow = glfwCreateWindow(1280, 720, "Texture Viewer", nullptr, nullptr);
	if (glfwWindow == nullptr) {
		errprintf("Couldn't create glfw glfwWindow! Exiting..\n");
		glfwTerminate();
		return 1;
	}

	GLFWimage icons[2] = {
		{ texview_icon32.width, texview_icon32.height, (unsigned char*)texview_icon32.pixel_data },
		{ texview_icon.width, texview_icon.height, (unsigned char*)texview_icon.pixel_data }
	};
	glfwSetWindowIcon(glfwWindow, 2, icons);

	glfwMakeContextCurrent(glfwWindow);
	gladLoadGL(glfwGetProcAddress);

	if(wantDebugContext) {
		int haveDebugContext = glfwGetWindowAttrib(glfwWindow, GLFW_CONTEXT_DEBUG);
		if(!GLAD_GL_ARB_debug_output) {
			errprintf( "You set the TEXVIEW_GLDEBUG environment variable, but GL_ARB_debug_output is not available!\n" );
		} else if(!haveDebugContext) {
			errprintf( "You set the TEXVIEW_GLDEBUG environment variable, but GLFW didn't give us a debug context (for whatever reason)!\n" );
		} else {
			texview::LogInfo( "You set the TEXVIEW_GLDEBUG environment variable, enabling OpenGL debug logging\n" );
			glDebugMessageCallbackARB(GLDebugCallback, NULL);
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
		}
	}

	glfwSwapInterval(1); // Enable vsync

	ktxLoadOpenGL(glfwGetProcAddress); // libktx

	glGenVertexArrays(1, &quadsVAO);
	glBindVertexArray(quadsVAO);
	glGenBuffers(1, &quadsVBO);
	glBindBuffer(GL_ARRAY_BUFFER, quadsVBO);
	glEnableVertexAttribArray(TV_ATTRIB_POSITION);
	qglVertexAttribPointer(TV_ATTRIB_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(VertexData), 0);
	glEnableVertexAttribArray(TV_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(TV_ATTRIB_TEXCOORD, 4, GL_FLOAT, GL_FALSE, sizeof(VertexData), offsetof(VertexData, tc));

	glfwSetScrollCallback(glfwWindow, myGLFWscrollfun);
	glfwSetKeyCallback(glfwWindow, myGLFWkeyfun);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	// things for docking/multi viewport
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	//io.ConfigViewportsNoAutoMerge = true;
	//io.ConfigViewportsNoTaskBarIcon = true;


	imguiIniPath += "/imgui.ini";
	io.IniFilename = imguiIniPath.c_str();

	defaultStyle = ImGui::GetStyle(); // get default unscaled style
	SetImGuiStyle();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	texview::LogImGuiInit();

	{
		// according to https://github.com/glfw/glfw/issues/1968 polling events
		// before getting the window scale works around issues on macOS
		glfwPollEvents();
		float xscale = 1.0f;
		float yscale = 1.0f;
		glfwGetWindowContentScale(glfwWindow, &xscale, &yscale);
		myGLFWwindowcontentscalefun(glfwWindow, xscale, yscale);
		glfwSetWindowContentScaleCallback(glfwWindow, myGLFWwindowcontentscalefun);
		updateFont = true; // make sure font is loaded
	}

	// load texture once everything is set up, so if errors happen they can be displayed
	if(argc > 1) {
		LoadTexture(argv[1]);
	}

	while (!glfwWindowShouldClose(glfwWindow)) {
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		glfwPollEvents();

		GenericFrame(glfwWindow);

		ImGuiFrame(glfwWindow);

		glfwSwapBuffers(glfwWindow);

		if (glfwGetWindowAttrib(glfwWindow, GLFW_ICONIFIED) != 0) {
			ImGui_ImplGlfw_Sleep(32);
			continue;
		}
	}

	if(shaderProgram != 0) {
		glDeleteProgram(shaderProgram);
	}
	glDeleteBuffers(1, &quadsVBO);
	quadsVBO = 0;
	glDeleteVertexArrays(1, &quadsVAO);
	quadsVAO = 0;

	curTex.Clear(); // also frees opengl texture which must happen before shutdown

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();

	glfwDestroyWindow(glfwWindow);
#ifdef TV_USE_NFD
	NFD_Quit();
#endif
	glfwTerminate();

	return ret;
}
