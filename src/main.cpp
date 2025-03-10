
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#ifdef TV_USE_NFD
#include <nfd.h>
#endif

#include <stdio.h>

#include "texview.h"

#include "data/texview_icon.h"
#include "data/texview_icon32.h"

static ImVec4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

static GLuint curGlTex; // TODO: should probably support more than one eventually..
static texview::Texture curTex;

static bool showImGuiDemoWindow = false;

static void glfw_error_callback(int error, const char* description)
{
	errprintf("GLFW Error: %d - %s\n", error, description);
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

	if(curGlTex != 0) {
		glDeleteTextures(1, &curGlTex);
		curGlTex = 0;
	}
	glGenTextures(1, &curGlTex);
	glBindTexture(GL_TEXTURE_2D, curGlTex);

	GLint internalFormat = curTex.dataType;

	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, curTex.mipLevels[0].width,
	             curTex.mipLevels[0].height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
	             curTex.mipLevels[0].data);

	// TODO: additional mipmap levels?

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void GenericFrame(GLFWwindow* window)
{
	int display_w, display_h;
	glfwGetFramebufferSize(window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
	             clear_color.z * clear_color.w, clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);

	// good thing we're using a compat profile :-p
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, display_w, display_h, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	float quadSize = display_h * 0.8f;
	float quadX = (display_w - quadSize)*0.5f;
	float quadY = display_h * 0.1f;

	//glColor3f(0, 0, 1);
	if(curGlTex)
	{
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, curGlTex);
		glBegin(GL_QUADS);
			glTexCoord2f(0, 0);
			glVertex2f(quadX, quadY);
			glTexCoord2f(0, 1);
			glVertex2f(quadX, quadY+quadSize);
			glTexCoord2f(1, 1);
			glVertex2f(quadX+quadSize, quadY+quadSize);
			glTexCoord2f(1, 0);
			glVertex2f(quadX+quadSize, quadY);
		glEnd();
	}
}

static void ImGuiFrame(GLFWwindow* window)
{
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// TODO: draw windows..

	if(showImGuiDemoWindow)
		ImGui::ShowDemoWindow(&showImGuiDemoWindow);

	ImGuiIO& io = ImGui::GetIO();

	ImGui::SetNextWindowPos( ImVec2(0, 0), ImGuiCond_Appearing );
	ImGui::SetNextWindowSize(ImVec2(0, io.DisplaySize.y), ImGuiCond_Appearing);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
	if(ImGui::Begin("##options", NULL, flags)) {
		if(ImGui::Button("Open File")) {
#ifdef TV_USE_NFD
			nfdopendialogu8args_t args = {0};
			//args.filterList = filters;
			//args.filterCount = 2;
			//args.defaultPath = "/tmp/";
			std::string dp;
			if(!curTex.name.empty()) {
				dp = curTex.name;
				size_t lastSlash = dp.find_last_of('/');
#ifdef _WIN32
				size_t lastBS = dp.find_last_of('\\');
				if(lastBS != std::string::npos && lastBS > lastSlash)
					lastSlash = lastBS;
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
#endif
		}

		ImGui::Checkbox("Show ImGui Demo Window", &showImGuiDemoWindow);
	}
	ImGui::End();


	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

#ifdef _WIN32
int my_main(int argc, char** argv) // called from WinMain() in winmain.cpp
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
	GLFWwindow* window = glfwCreateWindow(1280, 720, "Texture Viewer", nullptr, nullptr);
	if (window == nullptr) {
		errprintf("Couldn't create glfw window! Exiting..\n");
		glfwTerminate();
		return 1;
	}

	GLFWimage icons[2] = {
		{ texview_icon32.width, texview_icon32.height, (unsigned char*)texview_icon32.pixel_data },
		{ texview_icon.width, texview_icon.height, (unsigned char*)texview_icon.pixel_data }
	};
	glfwSetWindowIcon(window, 2, icons);

	glfwMakeContextCurrent(window);
	gladLoadGL(glfwGetProcAddress);
	glfwSwapInterval(1); // Enable vsync

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
	//ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	while (!glfwWindowShouldClose(window)) {
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		glfwPollEvents();
		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
		{
			ImGui_ImplGlfw_Sleep(32);
			continue;
		}

		GenericFrame(window);

		ImGuiFrame(window);

		glfwSwapBuffers(window);
	}

	glfwDestroyWindow(window);
#ifdef TV_USE_NFD
	NFD_Quit();
#endif
	glfwTerminate();

	return ret;
}
