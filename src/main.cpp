
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

#include "texview.h"

#include "data/texview_icon.h"
#include "data/texview_icon32.h"

static GLFWwindow* glfwWindow;

static ImVec4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

// TODO: should probably support more than one texture eventually..
static texview::Texture curTex;

static bool showImGuiDemoWindow = false;

static float imGuiMenuWidth = 0.0f;
static bool imguiMenuCollapsed = false;

static double zoomLevel = 1.0;
static double transX = 10;
static double transY = 10;
static bool dragging = false;
static ImVec2 lastDragPos;

static bool linearFilter = false;
static int mipmapLevel = -1; // -1: auto, otherwise enforce that level

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

// mipLevel -1 = auto (let GPU choose from all levels)
// otherwise use the given level (if it exists..)
static void SetMipmapLevel(texview::Texture& texture, GLint mipLevel, bool bindTexture = true)
{
	GLuint tex = texture.glTextureHandle;
	GLint numMips = texture.GetNumMips();
	if(tex == 0 || numMips == 1) {
		return;
	}
	if(bindTexture) {
		glBindTexture(GL_TEXTURE_2D, tex);
	}

	mipLevel = std::min(mipLevel, numMips - 1);
	// setting both to the same level enforces using that level
	GLint baseLevel = mipLevel;
	GLint maxLevel  = mipLevel;
	if(mipLevel < 0) { // auto mode
		baseLevel = 0;
		maxLevel = numMips - 1;
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, baseLevel);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
}

static void UpdateTextureFilter(bool bindTex = true)
{
	GLuint glTex = curTex.glTextureHandle;
	if(glTex == 0) {
		return;
	}
	if(bindTex) {
		glBindTexture(GL_TEXTURE_2D, glTex);
	}
	GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
	if(curTex.GetNumMips() == 1) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	} else {
		GLint mipFilter = linearFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
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
		SetMipmapLevel(curTex, mipmapLevel, false);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// mipLevel -1 == use configured mipmapLevel
static void DrawQuad(texview::Texture& texture, int mipLevel, ImVec2 pos, ImVec2 size, ImVec2 texCoordMax = ImVec2(1, 1), ImVec2 texCoordMin = ImVec2(0, 0))
{
	GLuint tex = texture.glTextureHandle;
	if(tex) {

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, tex);

		SetMipmapLevel(texture, (mipLevel < 0) ? mipmapLevel : mipLevel, false);

		glBegin(GL_QUADS);
			glTexCoord2f(texCoordMin.x, texCoordMin.y);
			glVertex2f(pos.x, pos.y);

			glTexCoord2f(texCoordMin.x, texCoordMax.y);
			glVertex2f(pos.x, pos.y + size.y);

			glTexCoord2f(texCoordMax.x, texCoordMax.y);
			glVertex2f(pos.x + size.x, pos.y + size.y);

			glTexCoord2f(texCoordMax.x, texCoordMin.y);
			glVertex2f(pos.x + size.x, pos.y);
		glEnd();
	}
}

static void DrawTexture()
{
	texview::Texture& tex = curTex;
	float texW, texH;
	tex.GetSize(&texW, &texH);
	if(viewMode == SINGLE) {
		DrawQuad(tex, -1, ImVec2(0, 0), ImVec2(texW, texH));
	} else if(viewMode == TILED) {
		float tilesX = numTiles[0];
		float tilesY = numTiles[1];
		ImVec2 size(texW*tilesX, texH*tilesY);
		DrawQuad(tex, -1, ImVec2(0, 0), size, ImVec2(tilesX, tilesY));
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
			int rowNum = 0;
			for(int i=0; i < numMips; ++i) {
				DrawQuad(tex, i, ImVec2(posX, posY), ImVec2(texW, texH));
				if(((i+1) % numHor) == 0) {
					posY += vOffset;
					// change horizontal direction every line
					// so the next level of the last mip of one line
					// is right below it instead of the start of the next line
					hOffset = -hOffset;
					++rowNum;
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
				DrawQuad(tex, i, ImVec2(posX, posY), ImVec2(texW, texH));
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
				DrawQuad(tex, i, ImVec2(posX, posY), ImVec2(w, h));

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
				DrawQuad(tex, i, ImVec2(posX, posY), ImVec2(w, h));
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
}

static void GenericFrame(GLFWwindow* window)
{
	int display_w, display_h;
	glfwGetFramebufferSize(window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
	             clear_color.z * clear_color.w, clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);

	float sx, sy;
	glfwGetWindowContentScale(window, &sx, &sy);

	float xOffs = imguiMenuCollapsed ? 0.0f : imGuiMenuWidth * sx;
	float winW = display_w - xOffs;

	// good thing we're using a compat profile :-p
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(xOffs, 0, winW, display_h);
	glOrtho(0, winW, display_h, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glScaled(zoomLevel, zoomLevel, 1);
	glTranslated((transX * sx) / zoomLevel, (transY * sy) / zoomLevel, 0.0);

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

static void ImGuiFrame(GLFWwindow* window)
{
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	if(showImGuiDemoWindow)
		ImGui::ShowDemoWindow(&showImGuiDemoWindow);

	ImGuiIO& io = ImGui::GetIO();

	ImGui::SetNextWindowPos( ImVec2(0, 0), ImGuiCond_Appearing );
	if(!imguiMenuCollapsed) {
		ImGui::SetNextWindowSize(ImVec2(0, io.DisplaySize.y), ImGuiCond_Always);
	}

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
	if(ImGui::Begin("##options", NULL, flags)) {
		if(ImGui::Button("Open File")) {
			OpenFilePicker();
		}
		float fontWrapWidth = ImGui::CalcTextSize("0123456789abcdef0123456789ABCDEF").x;
		ImGui::PushTextWrapPos(fontWrapWidth);
		//ImGui::TextWrapped("File: %s", curTex.name.c_str());
		ImGui::Text("File: ");
		ImGui::BeginDisabled(true);
		ImGui::TextWrapped("%s", curTex.name.c_str());
		ImGui::EndDisabled();
		ImGui::Text("Format: %s", curTex.formatName);
		float tw, th;
		curTex.GetSize(&tw, &th);
		ImGui::Text("Texture Size: %d x %d", (int)tw, (int)th);
		ImGui::Text("MipMap Levels: %d", curTex.GetNumMips());

		ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
		ImGui::PushItemWidth(fontWrapWidth - ImGui::CalcTextSize("View Mode  ").x);
		float zl = zoomLevel;
		if(ImGui::SliderFloat("Zoom", &zl, 0.0125, 50.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) {
			zoomLevel = zl;
		}
		if(ImGui::Button("Fit to Window")) {
			int display_w, display_h;
			glfwGetFramebufferSize(window, &display_w, &display_h);
			double winW = display_w - imGuiMenuWidth;
			double zw = winW / tw;
			double zh = display_h / th;
			if(zw < zh) {
				zoomLevel = zw;
				transX = 0;
				transY = floor(0.5 * (display_h/zw - th));
			} else {
				zoomLevel = zh;
				transX = floor(0.5 * (winW/zh - tw));
				transY = 0;
			}
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
		if(vMode == SINGLE || vMode == TILED) {
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
				if(ImGui::SliderInt("Mip Level", &mipLevel, -1, maxLevel, miplevelString)) {
					mipmapLevel = mipLevel;
					SetMipmapLevel(curTex, mipLevel);
				}
			}
		}

		ImGui::Spacing();
		int texFilter = linearFilter;
		if(ImGui::Combo("Filter", &texFilter, "Nearest\0Linear\0")) {
			if(texFilter != (int)linearFilter) {
				linearFilter = texFilter != 0;
				UpdateTextureFilter();
			}
		}

		ImGui::Spacing(); ImGui::Spacing();

		ImGui::ColorEdit3("BG Color", &clear_color.x);

		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Checkbox("Show ImGui Demo Window", &showImGuiDemoWindow);
		imGuiMenuWidth = ImGui::GetWindowWidth();
	}
	imguiMenuCollapsed = ImGui::IsWindowCollapsed();
	ImGui::End();


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

#ifdef _WIN32
int my_main(int argc, char** argv) // called from WinMain() in sys_win.cpp
#else
int main(int argc, char** argv)
#endif
{
	int ret = 0;
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

	// Create window with graphics context
	const char* glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
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
	glfwSwapInterval(1); // Enable vsync

	ktxLoadOpenGL(glfwGetProcAddress);

	glfwSetScrollCallback(glfwWindow, myGLFWscrollfun);
	glfwSetKeyCallback(glfwWindow, myGLFWkeyfun);

	if(argc > 1) {
		LoadTexture(argv[1]);
	}

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	// make it look a bit nicer with rounded edges
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 2.0f;
	style.FrameRounding = 3.0f;
	style.FramePadding = ImVec2( 6.0f, 3.0f );
	//style.ChildRounding = 6.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding = 3.0f;
	style.PopupRounding = 2.0f;

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	while (!glfwWindowShouldClose(glfwWindow)) {
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		glfwPollEvents();
		if (glfwGetWindowAttrib(glfwWindow, GLFW_ICONIFIED) != 0)
		{
			ImGui_ImplGlfw_Sleep(32);
			continue;
		}

		GenericFrame(glfwWindow);

		ImGuiFrame(glfwWindow);

		glfwSwapBuffers(glfwWindow);
	}

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
