find_package(PkgConfig)
include(ExternalProject)
include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 20)

# Defines the ARCHITECTURE variable
include("DetectArchitecture.cmake")

# Warnings are silenced for 3rdparty code
if(NOT MSVC)
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX,C>:-w>")
endif()

# Dummy target to use when lib isn't available
add_library(3rdparty_dummy_lib INTERFACE)


if (USE_SYSTEM_VULKAN_MEMORY_ALLOCATOR)
	find_package(VulkanMemoryAllocator REQUIRED GLOBAL)
	add_library(3rdparty::vulkanmemoryallocator ALIAS GPUOpen::VulkanMemoryAllocator)
else()
	add_library(3rdparty_vulkanmemoryallocator INTERFACE)
	target_include_directories(3rdparty_vulkanmemoryallocator SYSTEM INTERFACE GPUOpen/VulkanMemoryAllocator/include)
	add_library(3rdparty::vulkanmemoryallocator ALIAS 3rdparty_vulkanmemoryallocator)
endif()

# libusb
if(CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD")
	pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0>=1.0 )
	cmake_dependent_option(USE_SYSTEM_LIBUSB "Use system libusb-1.0 as shared library" ON
			"LIBUSB_FOUND" OFF)
else()
	pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0>=1.0 )
	cmake_dependent_option(USE_SYSTEM_LIBUSB "Use system libusb-1.0 as shared library" OFF
			"LIBUSB_FOUND" OFF)
endif()
if(CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD")
	# Always use system libusb as reference implementation isn't supported
	add_library(usb-1.0-shared INTERFACE)
	target_link_libraries(usb-1.0-shared INTERFACE PkgConfig::LIBUSB)
elseif(MSVC)
	# Windows time.h defines timespec but doesn't add any flag for it, which makes libusb attempt to define it again
	add_definitions(-DHAVE_STRUCT_TIMESPEC=1)
	add_subdirectory(libusb EXCLUDE_FROM_ALL)
else()
	if(USE_SYSTEM_LIBUSB)
		# we have the system libusb and have selected to use it
		add_library(usb-1.0-shared INTERFACE)
		target_link_libraries(usb-1.0-shared INTERFACE PkgConfig::LIBUSB)
	else()
		# we don't have the system libusb, so we compile from submodule
		unset(LIBUSB_LIBRARIES CACHE)
		add_subdirectory(libusb EXCLUDE_FROM_ALL)

		if (NOT TARGET usb-1.0 AND TARGET usb-1.0-static)
			add_library(usb-1.0 ALIAS usb-1.0-static)
		endif()
	endif()
endif()


# glslang
add_subdirectory(glslang EXCLUDE_FROM_ALL)


# OpenGL

# GLU is not linked: there is not a single glu* call in the tree, and on a
# libretro core it is one more NEEDED entry a frontend's sandbox has to provide.
# That one is real - the RetroArch flatpak's runtime does not carry libGLU, so a
# core that names it does not load there at all.
if (NOT ANDROID AND NOT APPLE)
	find_package(OpenGL REQUIRED OPTIONAL_COMPONENTS EGL OpenGL)

	add_library(3rdparty_opengl INTERFACE)
	target_include_directories(3rdparty_opengl SYSTEM INTERFACE GL)

	if (WIN32)
		if(NOT MSVC)
			target_link_libraries(3rdparty_opengl INTERFACE OpenGL::GL OpenGL::GLU)
		else()
			target_link_libraries(3rdparty_opengl INTERFACE dxgi.lib d2d1.lib dwrite.lib)
		endif()
	elseif(TARGET OpenGL::OpenGL)
		# The core: libOpenGL alone, so that it loads without X11. GLX is found
		# at run time (rpcs3/libretro/libretro_x11_runtime.cpp).
		target_link_libraries(3rdparty_opengl INTERFACE OpenGL::OpenGL)
	else()
		target_link_libraries(3rdparty_opengl INTERFACE OpenGL::GL OpenGL::GLX)
	endif()
else()
	add_library(3rdparty_opengl INTERFACE)
	target_compile_definitions(3rdparty_opengl INTERFACE WITHOUT_OPENGL=1)
endif()


# Vulkan: Khronos' headers and volk from the tree. volk loads the functions
# at run time, from the frontend's vkGetInstanceProcAddr or the system's
# loader (vulkan-1.dll, libvulkan.so.1, MoltenVK), so nothing is linked and
# nothing has to be installed to build.
set(VULKAN_TARGET 3rdparty_dummy_lib)
if(USE_VULKAN)
	add_library(3rdparty_volk STATIC volk/volk.c)
	set_property(TARGET 3rdparty_volk PROPERTY POSITION_INDEPENDENT_CODE ON)
	target_include_directories(3rdparty_volk SYSTEM PUBLIC Vulkan-Headers/include volk)
	target_compile_definitions(3rdparty_volk PUBLIC VK_NO_PROTOTYPES)
	if(WIN32)
		target_compile_definitions(3rdparty_volk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
	elseif(APPLE)
		target_compile_definitions(3rdparty_volk PRIVATE VK_USE_PLATFORM_METAL_EXT)
	elseif(ANDROID)
		target_compile_definitions(3rdparty_volk PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
	else()
		find_package(X11)
		if(X11_FOUND)
			target_compile_definitions(3rdparty_volk PRIVATE VK_USE_PLATFORM_XLIB_KHR)
			target_include_directories(3rdparty_volk PRIVATE ${X11_INCLUDE_DIR})
		endif()
		find_package(Wayland)
		if(WAYLAND_FOUND)
			target_compile_definitions(3rdparty_volk PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
			target_include_directories(3rdparty_volk PRIVATE ${WAYLAND_INCLUDE_DIR})
		endif()
	endif()
	if(NOT WIN32)
		target_link_libraries(3rdparty_volk PRIVATE ${CMAKE_DL_LIBS})
	endif()

	add_library(3rdparty_vulkan INTERFACE)
	target_compile_definitions(3rdparty_vulkan INTERFACE -DHAVE_VULKAN)
	target_link_libraries(3rdparty_vulkan INTERFACE 3rdparty_volk)

	if(UNIX AND NOT APPLE AND NOT ANDROID)
		find_package(Wayland)
		if (WAYLAND_FOUND)
			target_include_directories(3rdparty_vulkan
				SYSTEM INTERFACE ${WAYLAND_INCLUDE_DIR})
		endif()
	endif()

	set(VULKAN_TARGET 3rdparty_vulkan)
endif()

# AsmJit
add_subdirectory(asmjit EXCLUDE_FROM_ALL)


# GLEW, from the tree, for the OpenGL renderer on Linux and the BSDs (on
# Windows it loads its functions itself; Android and macOS have no GL
# renderer). Compiled into the core: nothing to install, no libGLEW.so to
# need by soname.
if(UNIX AND NOT APPLE AND NOT ANDROID)
	add_library(3rdparty_glew STATIC glew/src/glew.c)
	target_include_directories(3rdparty_glew SYSTEM PUBLIC glew/include)
	target_compile_definitions(3rdparty_glew PUBLIC GLEW_STATIC GLEW_NO_GLU)
	find_package(X11)
	if(X11_FOUND)
		target_include_directories(3rdparty_glew PRIVATE ${X11_INCLUDE_DIR})
	endif()
	set_property(TARGET 3rdparty_glew PROPERTY POSITION_INDEPENDENT_CODE ON)
else()
	add_library(3rdparty_glew INTERFACE)
endif()


# LLVM
add_subdirectory(llvm EXCLUDE_FROM_ALL)

# RTMIDI
add_subdirectory(rtmidi EXCLUDE_FROM_ALL)

# OPENCV
add_subdirectory(opencv EXCLUDE_FROM_ALL)

# FUSION
add_subdirectory(fusion EXCLUDE_FROM_ALL)

# add nice ALIAS targets for ease of use
if(USE_SYSTEM_LIBUSB)
	add_library(3rdparty::libusb ALIAS usb-1.0-shared)
else()
	add_library(3rdparty::libusb ALIAS usb-1.0-static)
endif()
add_library(3rdparty::glslang ALIAS 3rdparty_glslang)
add_library(3rdparty::opengl ALIAS 3rdparty_opengl)
add_library(3rdparty::vulkan ALIAS ${VULKAN_TARGET})
add_library(3rdparty::glew ALIAS 3rdparty_glew)
add_library(3rdparty::rtmidi ALIAS rtmidi)
add_library(3rdparty::opencv ALIAS ${OPENCV_TARGET})
add_library(3rdparty::fusion ALIAS Fusion)

