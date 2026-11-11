#!/bin/sh -ex

# Cross-builds the libretro core for Windows x64 on Linux, with llvm-mingw
# (clang, lld, libc++ and mingw-w64 - the same kind of toolchain as MSYS2's
# clang64, which build-windows-clang-libretro.sh uses natively). This is what
# the libretro buildbot runs: it has no Windows machine with a toolchain new
# enough for RPCS3, only Linux runners.
#
# clang64 has LLVM, GLEW and the Vulkan loader as packages; a cross toolchain
# has none of them, so they are made here:
# - LLVM from its submodule (BUILD_LLVM), the way the Android build gets it;
# - GLEW from its release source, as a static library;
# - the Vulkan headers from Khronos, and an import library for vulkan-1.dll
#   made from the loader's .def file - the core links the loader that comes
#   with the GPU driver, it does not carry one.
# ffmpeg is the prebuilt ffmpeg-mingw the build fetches by itself, and OpenCV
# is left out, as it is optional.
#
# Expects to run from the top of the source tree, with the submodule URLs
# already pointing at GitHub (see .gitlab-ci.yml).

TRIPLE=x86_64-w64-mingw32
SRC="$PWD"
DEPS="$SRC/deps-$TRIPLE"
JOBS="${NUMPROC:-$(nproc)}"
[ "$JOBS" -ge 1 ] || JOBS=1

git config --global --add safe.directory '*'

# Everything but the ones this build does not use.
# shellcheck disable=SC2046
git submodule -q update --init --force --depth 1 $(awk '/path/ && !/opencv/ { print $3 }' .gitmodules)

mkdir -p "$DEPS/include" "$DEPS/lib"

cat > "$DEPS/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $TRIPLE-clang)
set(CMAKE_CXX_COMPILER $TRIPLE-clang++)
set(CMAKE_RC_COMPILER $TRIPLE-windres)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_FIND_ROOT_PATH $DEPS)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
EOF

# Windows headers and libraries as the source spells them. MSVC and MSYS2 sit on
# case-insensitive file systems, so RPCS3 can say <WinSock2.h> or <Windows.h>;
# mingw-w64 names its headers in lower case, and on Linux those includes fail.
# Each mixed-case include whose lower-case name is a mingw-w64 header gets a
# link under that spelling, searched after everything else.
MINGW_INC="$(echo | $TRIPLE-clang -E -x c - -v 2>&1 | sed -n '/<...> search starts here/,/End of search list/p' | sed 's/^ *//' | grep -i 'mingw32/include$' | head -1)"
test -d "$MINGW_INC"
CASE_INC="$DEPS/case-include"
mkdir -p "$CASE_INC"
grep -rhoE '#[[:space:]]*include[[:space:]]*[<"][A-Za-z0-9_]*[A-Z][A-Za-z0-9_]*\.h[>"]' \
    --include='*.c' --include='*.cpp' --include='*.h' --include='*.hpp' \
    --exclude-dir=llvm --exclude-dir=opencv --exclude-dir=build --exclude-dir="deps-$TRIPLE" . |
    sed -E 's/.*[<"]([^>"]*)[>"].*/\1/' | sort -u |
    while read -r h; do
        lc="$(echo "$h" | tr 'A-Z' 'a-z')"
        if [ ! -e "$MINGW_INC/$h" ] && [ -e "$MINGW_INC/$lc" ]; then
            ln -sf "$MINGW_INC/$lc" "$CASE_INC/$h"
        fi
    done
ls "$CASE_INC"

# The same for libraries: CMake files link -lWinmm, -lPsapi, -lDbgHelp and the
# like, and mingw-w64 has libwinmm.a, libpsapi.a, libdbghelp.a.
MINGW_LIB="$(dirname "$MINGW_INC")/lib"
test -d "$MINGW_LIB"
CASE_LIB="$DEPS/case-lib"
mkdir -p "$CASE_LIB"
find . -name CMakeLists.txt -not -path './3rdparty/llvm/*' -not -path './3rdparty/opencv/*' \
        -not -path './build/*' -not -path "./deps-$TRIPLE/*" -exec cat {} + |
    grep -oE '[A-Za-z0-9_]*[A-Z][A-Za-z0-9_]*' | sort -u |
    while read -r l; do
        lc="$(echo "$l" | tr 'A-Z' 'a-z')"
        if [ -e "$MINGW_LIB/lib$lc.a" ] && [ ! -e "$MINGW_LIB/lib$l.a" ]; then
            ln -sf "$MINGW_LIB/lib$lc.a" "$CASE_LIB/lib$l.a"
        fi
    done
ls "$CASE_LIB"

# LLVM's Intel JIT events (on for Windows x86) clone ittapi from GitHub while
# LLVM is configured, once, and stop the configure when GitHub turns the
# runner's anonymous clone away ("could not read Username"), as it did on 22
# and 23 Oct. It is cloned here instead, with retries, and LLVM is pointed at it
# (ITTAPI_SOURCE_DIR); LLVM then only checks out its tag.
if [ ! -d "$DEPS/src/ittapi" ]; then
    mkdir -p "$DEPS/src"
    for attempt in 1 2 3 4 5; do
        GIT_TERMINAL_PROMPT=0 git clone --quiet https://github.com/intel/ittapi.git "$DEPS/src/ittapi" && break
        rm -rf "$DEPS/src/ittapi"
        [ "$attempt" -lt 5 ] || exit 1
        sleep $((attempt * 30))
    done
fi

# Vulkan headers and the vulkan-1 import library.
if [ ! -f "$DEPS/lib/libvulkan-1.a" ]; then
    git clone --quiet --depth 1 -b v1.4.321 https://github.com/KhronosGroup/Vulkan-Headers.git "$DEPS/src/Vulkan-Headers"
    cp -r "$DEPS/src/Vulkan-Headers/include/vulkan" "$DEPS/src/Vulkan-Headers/include/vk_video" "$DEPS/include/"
    git clone --quiet --depth 1 -b v1.4.321 https://github.com/KhronosGroup/Vulkan-Loader.git "$DEPS/src/Vulkan-Loader"
    llvm-dlltool -m i386:x86-64 -d "$DEPS/src/Vulkan-Loader/loader/vulkan-1.def" -D vulkan-1.dll -l "$DEPS/lib/libvulkan-1.a"
fi

# GLEW, static. The release tarball carries the generated sources; the git
# repository does not.
if [ ! -f "$DEPS/lib/libglew32.a" ]; then
    mkdir -p "$DEPS/src"
    curl -sSL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz | tar -xz -C "$DEPS/src"
    cmake -S "$DEPS/src/glew-2.2.0/build/cmake" -B "$DEPS/src/glew-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$DEPS/toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS" \
        -DBUILD_UTILS=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    cmake --build "$DEPS/src/glew-build" -j "$JOBS"
    cmake --install "$DEPS/src/glew-build"
fi

cmake -S . -B build -G Ninja                           \
    -DCMAKE_TOOLCHAIN_FILE="$DEPS/toolchain.cmake"     \
    -DCMAKE_BUILD_TYPE=Release                         \
    -DBUILD_LIBRETRO_CORE_ONLY=ON                      \
    -DUSE_NATIVE_INSTRUCTIONS=OFF                      \
    -DUSE_PRECOMPILED_HEADERS=OFF                      \
    -DCMAKE_C_FLAGS="-idirafter $CASE_INC"             \
    -DCMAKE_CXX_FLAGS="-idirafter $CASE_INC"           \
    -DCMAKE_SHARED_LINKER_FLAGS="-static -L$CASE_LIB"  \
    -DUSE_FAUDIO=OFF                                   \
    -DUSE_SDL=OFF                                      \
    -DUSE_SYSTEM_FFMPEG=OFF                            \
    -DUSE_SYSTEM_OPENCV=OFF                            \
    -DUSE_SYSTEM_OPENAL=OFF                            \
    -DUSE_DISCORD_RPC=OFF                              \
    -DUSE_LTO=OFF                                      \
    -DOpenGL_GL_PREFERENCE=LEGACY                      \
    -DWITH_LLVM=ON                                     \
    -DBUILD_LLVM=ON                                    \
    -DITTAPI_SOURCE_DIR="$DEPS/src"                    \
    -DGLEW_INCLUDE_DIR="$DEPS/include"                 \
    -DGLEW_STATIC_ARCHIVE="$DEPS/lib/libglew32.a"      \
    -DVulkan_INCLUDE_DIR="$DEPS/include"               \
    -DVulkan_LIBRARY="$DEPS/lib/libvulkan-1.a"

# -k 0 under KEEP_GOING=1, so a trial run reports every error, not the first.
cmake --build build --target rpcs3_libretro -j "$JOBS" -- ${KEEP_GOING:+-k 0}

# Runtime output (a DLL counts as one) goes to bin/, see CMAKE_RUNTIME_OUTPUT_DIRECTORY.
CORE=build/bin/parallel_ps3_libretro.dll
test -f "$CORE"
llvm-strip --strip-unneeded "$CORE"
ls -la "$CORE"
# What the DLL needs at load time: Windows' own DLLs, vulkan-1 and opengl32,
# nothing else.
llvm-readobj --coff-imports "$CORE" | grep 'Name:' | sort -u
