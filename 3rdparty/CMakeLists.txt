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


# Protobuf
add_subdirectory(protobuf EXCLUDE_FROM_ALL)



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

# yaml-cpp
add_subdirectory(yaml-cpp)


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

# stblib
add_subdirectory(stblib)

# Vulkan
set(VULKAN_TARGET 3rdparty_dummy_lib)
if(USE_VULKAN)
	if(APPLE)
		if(USE_SYSTEM_MVK)
			message(STATUS "RPCS3: Using system MoltenVK")
		else()
			message(STATUS "RPCS3: MoltenVK submodule")

			execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" .
				WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK"
			)
			execute_process(COMMAND "${CMAKE_COMMAND}" --build .
				WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK"
			)

			add_library(moltenvk_lib SHARED IMPORTED)
			add_dependencies(moltenvk_lib moltenvk)
			set_target_properties(moltenvk_lib
				PROPERTIES IMPORTED_LOCATION "{Vulkan_LIBRARY}"
			)

			set(VULKAN_SDK "${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/MoltenVK/MoltenVK")
			set(VK_ICD_FILENAMES "${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/MoltenVK/MoltenVK/icd/MoltenVK_icd.json")
			set(Vulkan_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/MoltenVK/MoltenVK/include")
			set(Vulkan_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/MoltenVK/Build/Products/Release/dynamic/libMoltenVK.dylib")
			set(Vulkan_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/MoltenVK/MoltenVK/Build/Products/Release")
		endif()
	endif()

	find_package(Vulkan)
	if(VULKAN_FOUND)
		add_library(3rdparty_vulkan INTERFACE)
		target_compile_definitions(3rdparty_vulkan INTERFACE -DHAVE_VULKAN)
		target_link_libraries(3rdparty_vulkan INTERFACE Vulkan::Vulkan)

		if(UNIX AND NOT APPLE AND NOT ANDROID)
			find_package(Wayland)
			if (WAYLAND_FOUND)
				target_include_directories(3rdparty_vulkan
					SYSTEM INTERFACE ${WAYLAND_INCLUDE_DIR})
			endif()
		endif()

		set(VULKAN_TARGET 3rdparty_vulkan)
	else()
		message(WARNING "USE_VULKAN was enabled, but libvulkan was not found. RPCS3 will be compiled without Vulkan support.")
		if(APPLE)
			message(FATAL_ERROR "To build without Vulkan support on macOS, please disable USE_VULKAN.")
		endif()
	endif()
endif()

# AsmJit
add_subdirectory(asmjit EXCLUDE_FROM_ALL)

# FFMPEG
# The alias further down is created unconditionally, so skipping this block
# outright leaves "3rdparty::ffmpeg" pointing at a target that was never made
# and configuration fails - Android has never been configured through here.
# A system ffmpeg is something the caller has had to provide deliberately, so
# when there is one, let Android take the same path as everyone else.
if(NOT ANDROID OR USE_SYSTEM_FFMPEG)
	add_library(3rdparty_ffmpeg INTERFACE)

	# Select the version of ffmpeg to use, default is builtin
	if(USE_SYSTEM_FFMPEG)
		message(STATUS "RPCS3: using shared ffmpeg")
		find_package(FFMPEG REQUIRED)

		target_include_directories(3rdparty_ffmpeg SYSTEM INTERFACE ${FFMPEG_INCLUDE_DIR})
		target_link_libraries(3rdparty_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
	else()
		message(STATUS "RPCS3: using builtin ffmpeg")
		add_subdirectory(ffmpeg EXCLUDE_FROM_ALL)
		# ffmpeg-core libraries are extracted to CMAKE_BINARY_DIR
		set(FFMPEG_LIB_DIR "${CMAKE_BINARY_DIR}/3rdparty/ffmpeg/lib")

		if (WIN32)
			target_link_libraries(3rdparty_ffmpeg INTERFACE "Bcrypt.lib")
		endif()

		find_library(FFMPEG_LIB_AVFORMAT avformat PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
		find_library(FFMPEG_LIB_AVCODEC avcodec PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
		find_library(FFMPEG_LIB_AVUTIL avutil PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
		find_library(FFMPEG_LIB_SWSCALE swscale PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
		find_library(FFMPEG_LIB_SWRESAMPLE swresample PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)

		if (FFMPEG_LIB_AVFORMAT MATCHES "FFMPEG_LIB_AVFORMAT-NOTFOUND")
			message(FATAL_ERROR "@#$%! FFMPEG NOT FOUND! ${FFMPEG_LIB_DIR}")
		endif()

		target_link_libraries(3rdparty_ffmpeg
		INTERFACE
			${FFMPEG_LIB_AVFORMAT}
			${FFMPEG_LIB_AVCODEC}
			${FFMPEG_LIB_AVUTIL}
			${FFMPEG_LIB_SWSCALE}
			${FFMPEG_LIB_SWRESAMPLE}
		)
		target_include_directories(3rdparty_ffmpeg SYSTEM INTERFACE "ffmpeg/include")
	endif()
endif()


# GLEW
add_library(3rdparty_glew INTERFACE)
if(NOT MSVC AND NOT ANDROID AND NOT APPLE)
	# A libretro core is dropped into a frontend on a machine nobody here
	# chose, so it must not go out needing libGLEW.so.2.2 by soname -
	# distributions disagree about that one, and the core then fails to load
	# with nothing but a dynamic linker error to go on. Setting
	# GLEW_USE_STATIC_LIBS is not enough on its own: FindGLEW honours it only
	# in some versions and quietly hands back the shared target in the rest,
	# which is what happened to the first build that went out. So look the
	# archive up directly and say in the configure log which one was taken.
	set(GLEW_USE_STATIC_LIBS ON)
	find_package(GLEW REQUIRED)
	find_library(GLEW_STATIC_ARCHIVE NAMES libGLEW.a libglew.a)
	if(GLEW_STATIC_ARCHIVE)
		message(STATUS "GLEW: linking statically against ${GLEW_STATIC_ARCHIVE}")
		target_link_libraries(3rdparty_glew INTERFACE ${GLEW_STATIC_ARCHIVE})
		# The archive is a bare path, so its headers do not come along with it.
		target_include_directories(3rdparty_glew INTERFACE ${GLEW_INCLUDE_DIRS})
		target_compile_definitions(3rdparty_glew INTERFACE GLEW_STATIC)
	else()
		message(STATUS "GLEW: no static archive found, linking the shared library")
		target_link_libraries(3rdparty_glew INTERFACE GLEW::GLEW)
	endif()
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
add_library(3rdparty::protobuf ALIAS 3rdparty_protobuf)
add_library(3rdparty::glslang ALIAS 3rdparty_glslang)
add_library(3rdparty::yaml-cpp ALIAS yaml-cpp)
add_library(3rdparty::opengl ALIAS 3rdparty_opengl)
add_library(3rdparty::stblib ALIAS 3rdparty_stblib)
add_library(3rdparty::vulkan ALIAS ${VULKAN_TARGET})
add_library(3rdparty::ffmpeg ALIAS 3rdparty_ffmpeg)
add_library(3rdparty::glew ALIAS 3rdparty_glew)
add_library(3rdparty::rtmidi ALIAS rtmidi)
add_library(3rdparty::opencv ALIAS ${OPENCV_TARGET})
add_library(3rdparty::fusion ALIAS Fusion)

