
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <stdio.h>

#include "texview.h"

#include "data/texview_icon.h"
#include "data/texview_icon32.h"

// TODO: could have an error printing function that also shows message through ImGui (if that is running)
#define errprintf(...) fprintf(stderr, __VA_ARGS__)

static ImVec4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

static void glfw_error_callback(int error, const char* description)
{
	errprintf("GLFW Error: %d - %s\n", error, description);
}

static void GenericFrame(GLFWwindow* window)
{
	// TODO: direct opengl rendering
}

static void ImGuiFrame(GLFWwindow* window)
{
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// TODO: draw windows..
	bool show_demo_window = true;
	ImGui::ShowDemoWindow(&show_demo_window);


	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

int main(int argc, char** argv)
{
	int ret = 0;
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit()) {
		errprintf("glfwInit() failed! Exiting..\n");
		return 1;
	}

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

		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);

		GenericFrame(window);

		ImGuiFrame(window);

		glfwSwapBuffers(window);
	}

	glfwDestroyWindow(window);
	glfwTerminate();

	return ret;
}
