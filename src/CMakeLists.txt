# TODO: which cmake versions exactly?
cmake_minimum_required(VERSION 3.0...3.22 FATAL_ERROR)
project(TextureViewer)

if(NOT MSVC) # GCC/clang or compatible, hopefully
	option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored compiler warnings/errors (GCC/Clang only; esp. useful with ninja)." OFF)
	option(ASAN		"Enable GCC/Clang Adress Sanitizer (ASan)" OFF) # TODO: MSVC might also support this, somehow?
	option(UBSAN	"Enable GCC/Clang Undefined Behavior Sanitizer (UBSan), implies HARDLINK_GAME" OFF)
endif()

set (CMAKE_CXX_STANDARD 14)

set(TV_COMPILER_IS_CLANG FALSE)
set(TV_COMPILER_IS_GCC_OR_CLANG FALSE)

# TODO: properly find glfw3
# TODO: option to link it statically
set (sys_libs glfw)

if(NOT MSVC)
	# check if this is some kind of clang (Clang, AppleClang, whatever)
	# (convert compiler ID to lowercase so we match Clang, clang, AppleClang etc, regardless of case)
	string(TOLOWER ${CMAKE_CXX_COMPILER_ID} compiler_id_lower)
	if(compiler_id_lower MATCHES ".*clang.*")
		message(STATUS "Compiler \"${CMAKE_CXX_COMPILER_ID}\" detected as some kind of clang")
		set(TV_COMPILER_IS_CLANG TRUE)
		set(TV_COMPILER_IS_GCC_OR_CLANG TRUE)
	elseif(CMAKE_COMPILER_IS_GNUCC)
		set(TV_COMPILER_IS_GCC_OR_CLANG TRUE)
	endif()
	unset(compiler_id_lower)

	# on some platforms/compilers, libm must be linked explicitly
	# (not MSVC though, so I'm putting this here)
	set (sys_libs ${sys_libs} m)
endif()

if(TV_COMPILER_IS_GCC_OR_CLANG)

	if(FORCE_COLORED_OUTPUT)
		if(CMAKE_COMPILER_IS_GNUCC)
		   add_compile_options (-fdiagnostics-color=always)
		elseif (TV_COMPILER_IS_CLANG)
		   add_compile_options (-fcolor-diagnostics)
		endif ()
	endif ()

	add_compile_options(-pipe)
	add_compile_options(-fno-strict-aliasing)
	add_compile_options(-Wall)

	if(ASAN)
		# if this doesn't work, ASan might not be available on your platform, don't set ASAN then..
		add_compile_options(-fsanitize=address)
		set(sys_libs ${sys_libs} -fsanitize=address)
	endif()
	if(UBSAN)
		# if this doesn't work, UBSan might not be available on your platform, don't set UBSAN then..
		add_compile_options(-fsanitize=undefined)
		set(sys_libs ${sys_libs} -fsanitize=undefined)
	endif()

elseif(MSVC)
	add_compile_options(/MP) # parallel build
else()
	message(WARNING "Unsupported compiler, good luck!")
endif()

set (texview_src
	main.cpp
	texload.cpp
	texview.h)

set (imgui_src
	libs/imgui/imgui.cpp
	libs/imgui/imgui.h
	libs/imgui/imgui_internal.h
	libs/imgui/imgui_demo.cpp
	libs/imgui/imgui_draw.cpp
	libs/imgui/imgui_tables.cpp
	libs/imgui/imgui_widgets.cpp
	libs/imgui/backends/imgui_impl_opengl3.cpp
	libs/imgui/backends/imgui_impl_glfw.cpp)

add_executable(texview ${texview_src} ${imgui_src} libs/glad/src/gl.c)
target_include_directories(texview PRIVATE "libs/imgui")
target_include_directories(texview PRIVATE "libs/glad/include")

target_link_libraries(texview glfw ${sys_libs})
