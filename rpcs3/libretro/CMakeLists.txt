# RPCS3 Libretro Core CMakeLists.txt

# Build as a shared library
# libretro_video.cpp is the OpenGL video unit; libretro_video_sw.cpp answers the
# same interface without it, for platforms that have no desktop GL renderer:
# Android, and macOS, where RPCS3 builds no GL renderer (rpcs3/Emu) and the core
# draws with Vulkan through MoltenVK.
if(ANDROID OR APPLE)
    set(LIBRETRO_VIDEO_SRC libretro_video_sw.cpp)
else()
    set(LIBRETRO_VIDEO_SRC libretro_video.cpp)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/embed_ui_icons.cmake)
libretro_embed_ui_icons(${CMAKE_CURRENT_BINARY_DIR}/libretro_ui_icons.cpp)
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_config_database.cmake)
libretro_embed_config_database(${CMAKE_CURRENT_BINARY_DIR}/libretro_config_database.cpp)
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_patches.cmake)
libretro_embed_patches(${CMAKE_CURRENT_BINARY_DIR}/libretro_patches.cpp)
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_vblank_unlock.cmake)
libretro_embed_vblank_unlock(${CMAKE_CURRENT_BINARY_DIR}/libretro_vblank_unlock.cpp)

add_library(rpcs3_libretro SHARED
    libretro_core.cpp
    config_database.c
    ${CMAKE_CURRENT_BINARY_DIR}/libretro_ui_icons.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/libretro_config_database.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/libretro_patches.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/libretro_vblank_unlock.cpp
    ${LIBRETRO_VIDEO_SRC}
    libretro_audio.cpp
    libretro_input.cpp
    libretro_pad_handler.cpp
    libretro_mouse_handler.cpp
    libretro_frontend.cpp
    libretro_firmware.cpp
    libretro_patch_manager.cpp
    libretro_vfs.cpp
    ${CMAKE_SOURCE_DIR}/rpcs3/rpcs3_version.cpp
    ${CMAKE_SOURCE_DIR}/rpcs3/Input/product_info.cpp
    ${CMAKE_SOURCE_DIR}/rpcs3/Input/ps_move_config.cpp
    ${CMAKE_SOURCE_DIR}/rpcs3/Input/ps_move_tracker.cpp
    ${CMAKE_SOURCE_DIR}/rpcs3/Input/pad_thread.cpp
)

# Set output name without 'lib' prefix on non-Windows
set_target_properties(rpcs3_libretro PROPERTIES
    OUTPUT_NAME "parallel_ps3_libretro"
    PREFIX ""
)

# Platform-specific suffix
if(WIN32)
    set_target_properties(rpcs3_libretro PROPERTIES SUFFIX ".dll")
elseif(APPLE)
    set_target_properties(rpcs3_libretro PROPERTIES SUFFIX ".dylib")
else()
    set_target_properties(rpcs3_libretro PROPERTIES SUFFIX ".so")
endif()

# Include directories - must match rpcs3_emu includes
target_include_directories(rpcs3_libretro PRIVATE
    ${CMAKE_SOURCE_DIR}/rpcs3
    ${CMAKE_SOURCE_DIR}/rpcs3/Emu
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against emucore and required libraries
target_link_libraries(rpcs3_libretro PRIVATE
    rpcs3_emu
    3rdparty::fusion
)

# OpenGL, and everything that goes with it. Not on Android: there is no desktop
# GL there, only GLES, and this port's video path is written against GL 4.3. It
# renders with Vulkan into system memory instead - see g_libretro_software_present.
# Nor on macOS, which has no GL renderer to drive (see LIBRETRO_VIDEO_SRC).
if(ANDROID OR APPLE)
    target_compile_definitions(rpcs3_libretro PRIVATE LIBRETRO_NO_GL)
else()
    # GLEW for OpenGL extension loading
    if(NOT WIN32)
        target_link_libraries(rpcs3_libretro PRIVATE 3rdparty::glew)
    endif()

    find_package(OpenGL REQUIRED OPTIONAL_COMPONENTS EGL OpenGL)
    if(UNIX AND NOT APPLE AND TARGET OpenGL::OpenGL)
        # libOpenGL, which has OpenGL's own functions and nothing of X11.
        # libGL would bring libGLX and libX11 with it, and the core would not
        # load without them; GLX and Xlib are found at run time
        # (libretro_x11_runtime.cpp).
        target_link_libraries(rpcs3_libretro PRIVATE OpenGL::OpenGL)
        target_sources(rpcs3_libretro PRIVATE libretro_x11_runtime.cpp)
    else()
        target_link_libraries(rpcs3_libretro PRIVATE OpenGL::GL)
    endif()

    # EGL: needed to create RSX contexts that share the frontend's GL objects
    if(UNIX AND NOT APPLE AND TARGET OpenGL::EGL)
        target_link_libraries(rpcs3_libretro PRIVATE OpenGL::EGL)
    endif()
endif()

# Platform-specific libraries
if(WIN32)
    target_link_libraries(rpcs3_libretro PRIVATE
        opengl32
        ws2_32
        Iphlpapi
        Winmm
        Psapi
        gdi32
        setupapi
        DbgHelp
    )
    target_compile_definitions(rpcs3_libretro PRIVATE UNICODE _UNICODE)
elseif(UNIX AND NOT APPLE)
    # Xlib's headers only; libX11 is not linked (libretro_x11_runtime.cpp)
    find_package(X11)
    if(X11_FOUND)
        target_include_directories(rpcs3_libretro PRIVATE ${X11_INCLUDE_DIR})
    endif()
    target_link_libraries(rpcs3_libretro PRIVATE ${CMAKE_DL_LIBS})
    
    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    find_package(Threads REQUIRED)
    target_link_libraries(rpcs3_libretro PRIVATE Threads::Threads)
elseif(APPLE)
    target_link_libraries(rpcs3_libretro PRIVATE iconv)
    # file_path.c finds the bundle through CoreFoundation
    target_link_libraries(rpcs3_libretro PRIVATE "-framework CoreFoundation")
    # sysinfo_darwin.mm asks NSProcessInfo
    target_link_libraries(rpcs3_libretro PRIVATE "-framework Foundation")
endif()

# Compile definitions
target_compile_definitions(rpcs3_libretro PRIVATE
    LIBRETRO_CORE
    RPCS3_LIBRETRO_VERSION="${RPCS3_LIBRETRO_VERSION}"
)

# Hide symbols by default (expose only libretro API)
if(NOT MSVC)
    target_compile_options(rpcs3_libretro PRIVATE -fvisibility=hidden)
    # The core sources use try/catch and the C-style casts of the libretro API,
    # both of which the global rpcs3 flags forbid (-fno-exceptions,
    # -Werror=old-style-cast). Relax them for this target only.
    target_compile_options(rpcs3_libretro PRIVATE -fexceptions -Wno-error=old-style-cast)
else()
    # Enable standard-conforming preprocessor for __VA_OPT__ support
    target_compile_options(rpcs3_libretro PRIVATE /Zc:preprocessor)
endif()

# C++20 standard
target_compile_features(rpcs3_libretro PRIVATE cxx_std_20)

# Use precompiled headers if enabled
if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(rpcs3_libretro PRIVATE ${CMAKE_SOURCE_DIR}/rpcs3/stdafx.h)
endif()

# Installation
install(TARGETS rpcs3_libretro
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

message(STATUS "RPCS3 Libretro core configured")
