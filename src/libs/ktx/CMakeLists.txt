cmake_minimum_required(VERSION 3.22)

include(cmake/codesign.cmake)

#option( KTX_FEATURE_ETC_UNPACK "ETC decoding support." ON )
set(KTX_FEATURE_ETC_UNPACK ON) # TODO: do this in including cmake file
set(KTX_FEATURE_GL_UPLOAD ON) # TODO: same here
set(KTX_FEATURE_VK_UPLOAD OFF) # TODO: same here

set(BUILD_SHARED_LIBS OFF)

set(KTX_FEATURE_KTX1 ON) # I think we always want this?

set(KTX_BUILD_RW_LIB OFF)

project(KTX-Libs)
#    VERSION ${KTX_VERSION}
#    DESCRIPTION "Libraries to create and read KTX image texture files."
#)

# Global compile & link options including optimization flags
# FIXME: this probably should not be global, but only be set for libktx and libktx_read
#if(MSVC)
#    add_compile_options( /W4;$<$<BOOL:${KTX_WERROR}>:/WX> )
#    add_compile_options( $<IF:$<CONFIG:Debug>,/Gz,/O2> )
#    # Enable UTF-8 support
#    add_compile_options( $<$<C_COMPILER_ID:MSVC>:/utf-8> )
#    add_compile_options( $<$<CXX_COMPILER_ID:MSVC>:/utf-8> )
#elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU"
#       OR ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
#    add_compile_options( -Wall -Wextra $<$<BOOL:${KTX_WERROR}>:-Werror>)
#    add_compile_options( $<IF:$<CONFIG:Debug>,-O0$<SEMICOLON>-g,-O3> )
#    if(EMSCRIPTEN)
#        add_link_options( $<IF:$<CONFIG:Debug>,-gsource-map,-O3> )
#    else()
#        add_link_options( $<IF:$<CONFIG:Debug>,-g,-O3> )
#    endif()
#else()
#    message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} not yet supported.")
#endif()

if(NOT MSVC)
	add_compile_options(-fno-strict-aliasing -ffp-contract=off)
	add_compile_options(-Wall -Wextra)
endif()

if(NOT BUILD_SHARED_LIBS)
    set(LIB_TYPE STATIC)
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EMSCRIPTEN)
        message(SEND_ERROR "Library type cannot be shared for the current platform. Set BUILD_SHARED_LIBS to OFF!")
    endif()
    set(LIB_TYPE SHARED)
endif()

set(KTX_MAIN_SRC
    include/KHR/khr_df.h
    include/ktx.h
    lib/basis_sgd.h
    lib/basis_transcode.cpp
    lib/miniz_wrapper.cpp
    external/basisu/encoder/basisu_miniz.h # DG: this header is used somewhere in libktx_read
    external/basisu/transcoder/basisu_containers.h
    external/basisu/transcoder/basisu_containers_impl.h
    external/basisu/transcoder/basisu_file_headers.h
    external/basisu/transcoder/basisu_transcoder_internal.h
    external/basisu/transcoder/basisu_transcoder_uastc.h
    external/basisu/transcoder/basisu_transcoder.cpp
    external/basisu/transcoder/basisu_transcoder.h
    external/basisu/transcoder/basisu.h
    external/basisu/zstd/zstd.c
    lib/checkheader.c
    external/dfdutils/createdfd.c
    external/dfdutils/colourspaces.c
    external/dfdutils/dfd.h
    external/dfdutils/interpretdfd.c
    external/dfdutils/printdfd.c
    external/dfdutils/queries.c
    external/dfdutils/vk2dfd.c
    external/dfdutils/vk2dfd.inl
    external/dfdutils/vulkan/vk_platform.h
    external/dfdutils/vulkan/vulkan_core.h
    lib/etcunpack.cxx
    lib/filestream.c
    lib/filestream.h
    lib/formatsize.h
    lib/gl_format.h
    lib/hashlist.c
    lib/info.c
    lib/ktxint.h
    lib/memstream.c
    lib/memstream.h
    lib/strings.c
    lib/swap.c
    lib/texture.c
    lib/texture.h
    lib/texture2.c
    lib/texture2.h
    lib/texture_funcs.inl
    lib/uthash.h
    lib/vk2gl.h
    lib/vk_format.h
    lib/vkFormat2glFormat.inl
    lib/vkFormat2glInternalFormat.inl
    lib/vkFormat2glType.inl
    lib/vkformat_check.c
    lib/vkformat_check_variant.c
    lib/vkformat_enum.h
    lib/vkformat_str.c
    lib/vkformat_typesize.c
    )

if (KTX_FEATURE_ETC_UNPACK)
	list(APPEND KTX_MAIN_SRC 
		external/etcdec/etcdec.cxx
	)
endif()

if(KTX_FEATURE_GL_UPLOAD)
    list(APPEND KTX_MAIN_SRC
        lib/gl_funclist.inl
        lib/gl_funcs.c
        lib/gl_funcs.h
        lib/glloader.c
    )
endif()

#set(KTX_BASISU_INCLUDE_DIRS
#    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/basisu/transcoder>
#)

# Main library
if(KTX_BUILD_RW_LIB)
	add_library( ktx ${LIB_TYPE}
		${KTX_MAIN_SRC}
		)
endif()

# Read-only library
add_library( ktx_read ${LIB_TYPE}
    ${KTX_MAIN_SRC}
    )

macro(common_libktx_settings target enable_write library_type)

    if(TARGET mkvk)
        # Creating vulkan headers only required after Vulkan Spec/SDK updates.
        add_dependencies(${target} mkvk)
    endif()

    set_target_properties(${target} PROPERTIES
        PUBLIC_HEADER
#          "${CMAKE_CURRENT_SOURCE_DIR}/include/ktx.h;${CMAKE_CURRENT_SOURCE_DIR}/include/KHR/khr_df.h"
          # Omit khr_df.h. Its installation has to be handled separately to
          # workaround CMake's failure to preserve the directory hierarchy.
          "${CMAKE_CURRENT_SOURCE_DIR}/include/ktx.h"
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
    )
    if(APPLE_LOCKED_OS)
        set_target_properties(${target} PROPERTIES
            FRAMEWORK TRUE
        )
    endif()

    if( NOT ${library_type} STREQUAL STATIC )
        # Must not call this macro for static libs on Windows. To keep
        # the if test simple, never call it for static libs. On macOS
        # and iOS Xcode knows libs aren't signed so it would ignore the
        # settings made by this macro.
        set_code_sign(${target} "NOPPS")
    endif()

    target_compile_definitions(
        ${target}
    PUBLIC
        "$<$<CONFIG:Debug>:_DEBUG;DEBUG>"
    PRIVATE
        LIBKTX
        SUPPORT_SOFTWARE_ETC_UNPACK=$<BOOL:${KTX_FEATURE_ETC_UNPACK}>
    )

    # C/C++ Standard
    # Need c11 for Unicode string literals
    target_compile_features(${target} PUBLIC c_std_11 cxx_std_11)

    # Compiler Warning Flags
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE
            -Wno-nested-anon-types
            -Wno-gnu-anonymous-struct
        )
    else()
        target_compile_options(${target} PRIVATE
            # clang options
            $<$<CXX_COMPILER_ID:AppleClang,Clang>:
                -Wno-nested-anon-types
                -Wno-gnu-anonymous-struct
            >
            $<$<CXX_COMPILER_ID:GNU>:
                -Wno-cast-function-type
            >
            # not clang options
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
                -Wno-pedantic
            >
        )
    endif()

    target_include_directories(
        ${target}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${KTX_BASISU_INCLUDE_DIRS}
        external

        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/basisu/zstd>
        $<INSTALL_INTERFACE:external/basisu/zstd>

        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utils>
        $<INSTALL_INTERFACE:utils>
    )

    target_include_directories(
        ${target}
        SYSTEM
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/other_include>
        $<INSTALL_INTERFACE:other_include>
    )

    if( ${library_type} STREQUAL STATIC )
        target_compile_definitions(${target} PUBLIC KHRONOS_STATIC)
    endif()

    # To reduce size, don't support transcoding to ancient formats.
    target_compile_definitions(${target} PRIVATE BASISD_SUPPORT_FXT1=0)

    # TODO: make options for all formats and good per-platform defaults
    # - BASISD_SUPPORT_UASTC
    # - BASISD_SUPPORT_DXT1 (BC1)
    # - BASISD_SUPPORT_DXT5A (BC3/4/5)
    # - BASISD_SUPPORT_BC7
    # - BASISD_SUPPORT_BC7_MODE5
    # - BASISD_SUPPORT_PVRTC1
    # - BASISD_SUPPORT_ETC2_EAC_A8
    # - BASISD_SUPPORT_ASTC
    # - BASISD_SUPPORT_ATC
    # - BASISD_SUPPORT_ASTC_HIGHER_OPAQUE_QUALITY
    # - BASISD_SUPPORT_ETC2_EAC_RG11
    # - BASISD_SUPPORT_FXT1
    # - BASISD_SUPPORT_PVRTC2

    if(WIN32)
        target_compile_definitions(
            ${target}
        PRIVATE
            # Only set dllexport when building a shared library.
            $<$<STREQUAL:${library_type},SHARED>:KTX_API=__declspec\(dllexport\)>
            # Code compiled with the versions shown defaults to a constexpr
            # std::mutex constructor and requires a mscvp140.dll of at least
            # version 14.40.33810.00 otherwise code creating a mutex
            # crashes mysteriously. Since many JVM installations bundle 
            # their own version of the VC++ redistributables chances are
            # high they will not have a modern enough version so JNI modules
            # linked with libktx will crash when multiple threads are used,
            # as they are in the BasisU and ASTC encoders.
            #
            # To avoid this set a define to prevent the compiler using
            # constexpr mutex constructors. Remove this eventually after
            # in-use JVM installations have at least this VC runtime. Remove
            # also from ASTCENC_LIB_TARGET settings around line 1169.
            $<$<AND:${is_msvccl},$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,19.40.33811>>:_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR>
            $<$<AND:${is_clangcl},$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,17.0.3>>:_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR>
        PUBLIC # only for basisu_c_binding.
            BASISU_NO_ITERATOR_DEBUG_LEVEL
        )
        # The generator automatically sets the needed VCLinker
        # option when a .def file is seen in sources.
        # The def files that we add have a different syntax depending on the ABI
        if(MINGW)
            target_sources(
                ${target}
            PRIVATE
                lib/internalexport_mingw.def
                $<${enable_write}:lib/internalexport_write_mingw.def>
            )
            # Need these flags if mingw happens to target the ucrt (new) rather
            # than the legacy msvcrt. Otherwise tests will fail to run because
            # the necessary dlls will be missing. If we statically link
            # them instead it's fine. This does not cause any abberations if
            # the mingw toolchain targets msvcrt instead.
            target_link_options(${target} PUBLIC -static-libgcc -static-libstdc++)
        else()
            target_sources(
                ${target}
            PRIVATE
                lib/internalexport.def
                $<${enable_write}:lib/internalexport_write.def>
            )
        endif()
    elseif(EMSCRIPTEN)
        target_compile_definitions(${target} PRIVATE
            # To reduce size, don't support transcoding to formats not
            # supported # by WebGL.
            BASISD_SUPPORT_ATC=0
            BASISD_SUPPORT_PVRTC2=0
            # Don't support higher quality mode to avoid 64k table.
            BASISD_SUPPORT_ASTC_HIGHER_OPAQUE_QUALITY=0
            KTX_OMIT_VULKAN=1
        )
        target_link_options(${target} INTERFACE
#            "SHELL:-s ASSERTIONS=2"
#            "SHELL:-s SAFE_HEAP=1"
#            "SHELL:-s STACK_OVERFLOW_CHECK=2"
            "SHELL:-s ALLOW_MEMORY_GROWTH=1"
            "SHELL:-s MALLOC=emmalloc"
            "SHELL:-s FULL_ES3=1"
            "SHELL:-s GL_ENABLE_GET_PROC_ADDRESS=1" # For Emscripten 3.1.51+
        )
    endif()

    if(KTX_FEATURE_KTX1)
        target_compile_definitions(${target} PUBLIC KTX_FEATURE_KTX1)
        target_sources(
            ${target}
        PRIVATE
            lib/texture1.c
            lib/texture1.h
        )
    endif()

    if(KTX_FEATURE_KTX2)
        target_compile_definitions(${target} PUBLIC KTX_FEATURE_KTX2)
    endif()

    if(WIN32)
        if(MINGW)
            # Check if the Threads package is provided; if using Mingw it MIGHT be
            find_package(Threads)
            if(Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)
                target_compile_definitions(${target} PRIVATE WIN32_HAS_PTHREADS)
                target_link_libraries(${target} PRIVATE Threads::Threads)
            endif()
        endif()

    elseif(APPLE)
        if(KTX_EMBED_BITCODE)
            target_compile_options(${target} PRIVATE "-fembed-bitcode")
        endif()

    elseif(LINUX)
        find_package(Threads REQUIRED)
        target_link_libraries(
            ${target}
            PRIVATE
            dl
            Threads::Threads
        )
    endif()

    if(KTX_FEATURE_VK_UPLOAD)
        target_sources(
            ${target}
        PRIVATE
            include/ktxvulkan.h
            lib/vk_funcs.c
            lib/vk_funcs.h
            lib/vkloader.c
        )
        target_include_directories(
            ${target}
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/dfdutils>
            $<INSTALL_INTERFACE:external/dfdutils>
        )

        get_target_property( KTX_PUBLIC_HEADER ${target} PUBLIC_HEADER )
        list(APPEND KTX_PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/ktxvulkan.h)
        set_target_properties(${target} PROPERTIES
            PUBLIC_HEADER "${KTX_PUBLIC_HEADER}"
        )
    else()
        target_compile_definitions( ${target} PRIVATE KTX_OMIT_VULKAN=1 )
    endif()

    # Adding write capability to target ktx
    if(${enable_write})
        target_sources(
            ${target}
        PRIVATE
            lib/basis_encode.cpp
            lib/astc_codec.cpp
            ${BASISU_ENCODER_C_SRC}
            ${BASISU_ENCODER_CXX_SRC}
            lib/writer1.c
            lib/writer2.c
        )

        target_include_directories(
            ${target}
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/basisu>
            $<INSTALL_INTERFACE:external/basisu>
            $<$<BOOL:${BASISU_SUPPORT_OPENCL}>:${OpenCL_INCLUDE_DIRS}>
        )
        target_compile_definitions(
            ${target}
        PUBLIC
            KTX_FEATURE_WRITE
        PRIVATE
            # BASISD_SUPPORT_KTX2 has to be 1 to compile the encoder. We
            # don't use it. Hopefully it doesn't add too much code. We're using
            # the zstd encoder in basisu by explicitly including the file in our
            # source list. We don't need the related code in the encoder.
            BASISD_SUPPORT_KTX2_ZSTD=0
            BASISD_SUPPORT_KTX2=1
            $<$<BOOL:${BASISU_SUPPORT_SSE}>:BASISU_SUPPORT_SSE=1>
            $<$<NOT:$<BOOL:${BASISU_SUPPORT_SSE}>>:BASISU_SUPPORT_SSE=0>
            $<$<BOOL:${BASISU_SUPPORT_OPENCL}>:BASISU_SUPPORT_OPENCL=1>
            $<$<NOT:$<BOOL:${BASISU_SUPPORT_OPENCL}>>:BASISU_SUPPORT_OPENCL=0>
        )
        target_compile_options(
            ${target}
        PRIVATE
            $<$<AND:$<BOOL:${BASISU_SUPPORT_SSE}>,$<CXX_COMPILER_ID:AppleClang,Clang,GNU>>:
                -msse4.1
            >
        )
        if(EMSCRIPTEN)
            target_link_options(
                ${target}
            INTERFACE
                # Default 64kb not enough for encode_uastc.
                "SHELL:-s STACK_SIZE=96kb"
            )
        endif()
        target_link_libraries(
            ${target}
        PRIVATE
            $<$<BOOL:${BASISU_SUPPORT_OPENCL}>:${OpenCL_LIBRARIES}>
        )
    endif()
endmacro(common_libktx_settings)

if(KTX_BUILD_RW_LIB)
	common_libktx_settings(ktx 1 ${LIB_TYPE})
endif()
common_libktx_settings(ktx_read 0 ${LIB_TYPE})

#create_version_header(lib ktx)
#create_version_file()

target_compile_definitions(
    ktx_read
PRIVATE
    # We're reading the files ourselves so don't need Basis KTX v2 support.
    BASISD_SUPPORT_KTX2_ZSTD=0
    BASISD_SUPPORT_KTX2=0
)



