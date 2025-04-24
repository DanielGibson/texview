# TODO: which cmake versions exactly?
cmake_minimum_required(VERSION 3.0...3.22 FATAL_ERROR)
project(TextureViewer)

if(NOT MSVC) # GCC/clang or compatible, hopefully
	option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored compiler warnings/errors (GCC/Clang only; esp. useful with ninja)." OFF)
	option(ASAN		"Enable GCC/Clang Adress Sanitizer (ASan)" OFF) # TODO: MSVC might also support this, somehow?
	option(UBSAN	"Enable GCC/Clang Undefined Behavior Sanitizer (UBSan), implies HARDLINK_GAME" OFF)
endif()

#option(USE_NATIVE_FILE_DIALOG "Use 'Native File Dialog Extended' library" ON)
set(USE_NATIVE_FILE_DIALOG ON) # currently no alternative is implemented

set (CMAKE_CXX_STANDARD 14)

set (TEXVIEW_BINARY "texview")

# I was told this makes it build on macOS
if (APPLE)
	enable_language(OBJC)
endif()

########
## GLFW

set(GLFW_BUILD_DOCS OFF)
set(GLFW_INSTALL OFF)
add_subdirectory(libs/glfw)

set (sys_libs glfw)

##############################
## Native File Dialog Extended

if(USE_NATIVE_FILE_DIALOG)
	set(NFD_OVERRIDE_RECENT_WITH_DEFAULT ON)
	set(nfd_PLATFORM Undefined)
	if(WIN32)
	  set(nfd_PLATFORM PLATFORM_WIN32)
	elseif(APPLE)
	  set(nfd_PLATFORM PLATFORM_MACOS)
	elseif(UNIX AND NOT APPLE)
	  set(nfd_PLATFORM PLATFORM_LINUX)
	endif()
	add_subdirectory(libs/nfd_extended_src)
	set(sys_libs ${sys_libs} nfd)
	add_definitions(-DTV_USE_NFD)
endif()

##########
## libktx

# TODO: set things
add_subdirectory(libs/ktx)
set(sys_libs ${sys_libs} ktx_read)

#####################
## Compiler Detection

set(TV_COMPILER_IS_CLANG FALSE)
set(TV_COMPILER_IS_GCC_OR_CLANG FALSE)

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
	logging.cpp
	main.cpp
	texload.cpp
	texview.h)

if(WIN32)
	set(texview_src ${texview_src} sys_win.cpp)
else() # the two genders: Win32 and POSIX
	set(texview_src ${texview_src} sys_posix.cpp)
endif()

set (add_lib_src
	libs/glad/src/gl.c
	libs/stb_impl.c)

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

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX src FILES ${texview_src} ${imgui_src} ${add_lib_src})

add_executable(${TEXVIEW_BINARY} WIN32
	${texview_src}
	${imgui_src}
	${add_lib_src})

target_link_libraries(${TEXVIEW_BINARY} ${sys_libs})
target_include_directories(${TEXVIEW_BINARY} PRIVATE "libs/imgui")
target_include_directories(${TEXVIEW_BINARY} PRIVATE "libs/glad/include")

# CMake >= 3.6 supports setting the default project started for debugging (instead of trying to launch ALL_BUILD ...)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${TEXVIEW_BINARY})
