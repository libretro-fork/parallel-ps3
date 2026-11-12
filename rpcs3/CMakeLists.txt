# Define GNU standard installation directories
include(GNUInstallDirs)

# Generate git-version.h at build time.
include(${CMAKE_CURRENT_SOURCE_DIR}/git-version.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/libretro/upstream_version.cmake)

# Check for a sufficient compiler and set build options
include(ConfigureCompiler)
include(CheckFunctionExists)

set(CMAKE_CXX_STANDARD 23)

if(UNIX AND NOT APPLE AND NOT ANDROID)
    add_compile_definitions(DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/rpcs3")
    # No window system: the frontend has the window
endif()

# This repository builds the libretro core and nothing else: the Qt
# application, its executable, packaging and unit tests are gone.
add_subdirectory(Emu)

gen_git_version(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(libretro)
