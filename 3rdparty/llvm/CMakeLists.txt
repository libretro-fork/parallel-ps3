if(WITH_LLVM)
	check_cxx_compiler_flag("-msse -msse2 -mcx16" COMPILER_X86)
	check_cxx_compiler_flag("-march=armv8-a+lse" COMPILER_ARM)

	if(BUILD_LLVM)
		# LLVM's sources are in this repository (llvm/): the part of LLVM the
		# core is built from, without LLVM's own CMake. The core's Makefile
		# knows how to build it (rpcs3/libretro/make/llvm), so it is asked
		# to: one description of the build, the same LLVM in every build.
		message(STATUS "LLVM will be built from 3rdparty/llvm/llvm by the core's Makefile.")

		find_program(LLVM_TREE_MAKE NAMES gmake make mingw32-make REQUIRED)
		include(ProcessorCount)
		ProcessorCount(LLVM_TREE_JOBS)
		if(LLVM_TREE_JOBS EQUAL 0)
			set(LLVM_TREE_JOBS 1)
		endif()

		# Where its objects, TableGen's output and the archive go. A cache
		# variable so that a tree built by the Makefile can be reused.
		set(LLVM_TREE_OUT "${CMAKE_CURRENT_BINARY_DIR}/llvm_build" CACHE PATH "Where LLVM is built from the tree")
		set(LLVM_TREE_ARCHIVE "${LLVM_TREE_OUT}/libllvm.a")
		get_filename_component(LLVM_TREE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
		set(LLVM_TREE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/llvm/llvm")
		set(LLVM_TREE_CFG "${LLVM_TREE_ROOT}/rpcs3/libretro/make/llvm/include")

		if(WIN32)
			set(LLVM_TREE_PLATFORM win)
			set(LLVM_TREE_OS windows)
		elseif(APPLE)
			set(LLVM_TREE_PLATFORM osx)
			set(LLVM_TREE_OS darwin)
		else()
			set(LLVM_TREE_PLATFORM unix)
			set(LLVM_TREE_OS linux)
		endif()

		if(COMPILER_ARM)
			set(LLVM_TREE_ARCH aarch64)
		else()
			set(LLVM_TREE_ARCH x86)
		endif()

		# The compilers as CMake calls them: with the target and the sysroot
		# of a cross build (the NDK's clang, llvm-mingw, a macOS slice)
		set(LLVM_TREE_TARGET_FLAGS "")
		if(CMAKE_CXX_COMPILER_TARGET)
			string(APPEND LLVM_TREE_TARGET_FLAGS " --target=${CMAKE_CXX_COMPILER_TARGET}")
		endif()
		if(CMAKE_SYSROOT)
			string(APPEND LLVM_TREE_TARGET_FLAGS " --sysroot=${CMAKE_SYSROOT}")
		endif()
		if(APPLE)
			if(CMAKE_OSX_ARCHITECTURES)
				string(APPEND LLVM_TREE_TARGET_FLAGS " -arch ${CMAKE_OSX_ARCHITECTURES}")
			endif()
			if(CMAKE_OSX_SYSROOT)
				string(APPEND LLVM_TREE_TARGET_FLAGS " -isysroot ${CMAKE_OSX_SYSROOT}")
			endif()
			if(CMAKE_OSX_DEPLOYMENT_TARGET)
				string(APPEND LLVM_TREE_TARGET_FLAGS " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
			endif()
		endif()

		# TableGen runs on the machine that builds
		if(CMAKE_CROSSCOMPILING)
			find_program(LLVM_TREE_HOST_CXX NAMES c++ g++ clang++ REQUIRED)
		else()
			set(LLVM_TREE_HOST_CXX "${CMAKE_CXX_COMPILER}")
		endif()

		add_custom_target(llvm_from_tree
			COMMAND ${LLVM_TREE_MAKE} -C "${LLVM_TREE_ROOT}" -j${LLVM_TREE_JOBS} llvm
				HAVE_LLVM=1 SYSTEM_LLVM=0
				platform=${LLVM_TREE_PLATFORM}
				arch=${LLVM_TREE_ARCH}
				"CC=${CMAKE_C_COMPILER}${LLVM_TREE_TARGET_FLAGS}"
				"CXX=${CMAKE_CXX_COMPILER}${LLVM_TREE_TARGET_FLAGS}"
				"AR=${CMAKE_AR}"
				"HOST_CXX=${LLVM_TREE_HOST_CXX}"
				"LLVM_OUT=${LLVM_TREE_OUT}"
				"LLVM_ARCHIVE=${LLVM_TREE_ARCHIVE}"
			BYPRODUCTS "${LLVM_TREE_ARCHIVE}"
			COMMENT "LLVM, from 3rdparty/llvm/llvm"
			VERBATIM)

		set(STATIC_LINK_LLVM ON CACHE BOOL "Link against LLVM statically. This will get set to ON if you build LLVM from the tree." FORCE)

		add_library(3rdparty_llvm INTERFACE)
		add_dependencies(3rdparty_llvm llvm_from_tree)
		target_link_libraries(3rdparty_llvm INTERFACE "${LLVM_TREE_ARCHIVE}")
		if(WIN32)
			target_link_libraries(3rdparty_llvm INTERFACE psapi shell32 ole32 uuid advapi32 ws2_32 ntdll)
		endif()
		target_include_directories(3rdparty_llvm SYSTEM INTERFACE
			"${LLVM_TREE_SRC}/include"
			"${LLVM_TREE_OUT}/gen/include"
			"${LLVM_TREE_CFG}/${LLVM_TREE_OS}"
			"${LLVM_TREE_CFG}/${LLVM_TREE_ARCH}"
			"${LLVM_TREE_CFG}/common")
		target_compile_definitions(3rdparty_llvm INTERFACE LLVM_AVAILABLE
			__STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS)

		add_library(3rdparty::llvm ALIAS 3rdparty_llvm)
		return()
	else()
		message(STATUS "Using prebuilt or system LLVM")

		if (LLVM_DIR AND NOT IS_ABSOLUTE "${LLVM_DIR}")
			# change relative LLVM_DIR to be relative to the source dir
			set(LLVM_DIR ${CMAKE_SOURCE_DIR}/${LLVM_DIR})
		endif()

		find_package(LLVM CONFIG)

		if (NOT LLVM_FOUND)
			message(FATAL_ERROR "Can't find LLVM libraries from the CMAKE_PREFIX_PATH path or LLVM_DIR. Enable the BUILD_LLVM option to build LLVM from the sources in 3rdparty/llvm/llvm.")
		endif()
		if (LLVM_VERSION VERSION_LESS 18)
			message(FATAL_ERROR "Found LLVM version ${LLVM_VERSION}. Required version 18 or above. Enable the BUILD_LLVM option to build LLVM from the sources in 3rdparty/llvm/llvm.")
		endif()
	endif()

	if (STATIC_LINK_LLVM)
		if (NOT DEFINED LLVM_TARGETS_TO_BUILD)
			if(COMPILER_ARM)
				set(LLVM_TARGETS_TO_BUILD "AArch64" CACHE STRING "Semicolon-separated list of targets to build, or \"all\".")
			else()
				set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "Semicolon-separated list of targets to build, or \"all\".")
			endif()
		endif()

		# For Windows x86 (not Windows AArch64) only when BUILD_LLVM is enabled and
		# for Linux x86 (not Linux AArch64) even if BUILD_LLVM is disabled (precompiled llvm used)
		if(LLVM_USE_INTEL_JITEVENTS OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND COMPILER_X86))
			list (APPEND LLVM_ADDITIONAL_LIBS IntelJITEvents)
		endif()

		# For Linux even if BUILD_LLVM is disabled (precompiled llvm used)
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			list (APPEND LLVM_ADDITIONAL_LIBS PerfJITEvents)
		endif()

		llvm_map_components_to_libnames(LLVM_LIBS
			${LLVM_TARGETS_TO_BUILD}
			${LLVM_ADDITIONAL_LIBS}
			Core
			ExecutionEngine
			MCJIT
			Passes
		)
	else()
		set(LLVM_LIBS LLVM)
	endif()

	add_library(3rdparty_llvm INTERFACE)
	target_link_libraries(3rdparty_llvm INTERFACE ${LLVM_LIBS})
	target_include_directories(3rdparty_llvm SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
	separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
	target_compile_definitions(3rdparty_llvm INTERFACE ${LLVM_DEFINITIONS_LIST} LLVM_AVAILABLE)

	add_library(3rdparty::llvm ALIAS 3rdparty_llvm)
else()
	add_library(3rdparty::llvm ALIAS 3rdparty_dummy_lib)
endif()
