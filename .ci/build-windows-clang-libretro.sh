#!/bin/sh -ex

# Builds the libretro core on Windows, under MSYS2's clang64 environment.
#
# Not MSVC: RPCS3 builds Windows two ways, msbuild over rpcs3.sln and this one,
# and the core exists only as a CMake target so the solution is no use. MSVC
# with CMake does not work either - the LLVM on the runner image is the client
# distribution, with no CMake package and no static libraries, so
# find_package(LLVM) finds nothing. clang64 has all of it as packages: LLVM
# with its CMake config, and ffmpeg, OpenCV and GLEW besides, which is why
# this asks for the system copies of those. Not curl: since RPCN and clans
# went into the emulator itself it links libcurl, and clang64's is a DLL, so
# curl and wolfSSL come from the submodules and are linked in. protobuf, for
# RPCN, would take clang64's abseil, DLLs again; it fetches and builds its own.

git config --global --add safe.directory '*'

# The ones clang64 provides as packages are left out, as is LLVM.
# shellcheck disable=SC2046
git submodule -q update --init --depth 1 $(awk '/path/ && !/llvm/ && !/opencv/ && !/ffmpeg/ && !/FAudio/ { print $3 }' .gitmodules)

# The core has to be one DLL that needs nothing but Windows, the Vulkan loader
# and OpenGL. RetroArch loads it with LoadLibraryW on its full path, so Windows
# looks for the DLLs it depends on next to retroarch.exe, in System32 and on
# PATH - never in the cores folder - and a core that links clang64's shared
# libraries does not load at all: "missing modules" (NNshi). What it pulled in
# were libc++, ffmpeg's five, zlib and zstd.

# ffmpeg, static, built here: clang64's own static ffmpeg is built against
# dav1d, x264, libvpx and more, none of which the core wants to carry. With
# --disable-autodetect nothing outside ffmpeg is picked up, which is also what
# keeps its link line down to a few Windows libraries.
#
# A Windows-style path, so that neither MSYS2's argument conversion nor the
# native CMake and clang have to make sense of /d/a/...
FFMPEG_PREFIX="$(cygpath -m "$PWD")/ffmpeg-static"
if [ ! -f "$FFMPEG_PREFIX/lib/libavcodec.a" ]; then
    git clone --quiet --depth 1 -b release/7.1 https://github.com/FFmpeg/FFmpeg.git ffmpeg-src
    (
        cd ffmpeg-src || exit 1
        ./configure                                   \
            --prefix="$FFMPEG_PREFIX"                 \
            --cc=clang --cxx=clang++                  \
            --enable-static --disable-shared          \
            --disable-autodetect                      \
            --disable-programs --disable-doc          \
            --disable-avdevice --disable-network
        make -j"$(nproc)"
        make install
    )
fi
# PKG_CONFIG_PATH is emptied because MSYS2 sets it to clang64's own
# pkgconfig directory, which would win with that ffmpeg and its whole
# dependency tree; and the directory is given in MSYS form, as pkgconf
# splits a search path on ':' and would cut "D:/..." in two.
FFMPEG_LIBS="$(PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$(cygpath -u "$FFMPEG_PREFIX")/lib/pkgconfig" \
    pkg-config --static --libs libavformat libavcodec libswscale libswresample libavutil | tr ' ' ';')"
echo "static ffmpeg: $FFMPEG_LIBS"
case "$FFMPEG_LIBS" in
    *ffmpeg-static*) ;;
    *) echo "pkg-config did not return the static ffmpeg built above"; exit 1 ;;
esac

# zlib and zstd come in through LLVM's CMake package, libc++ and libunwind
# through the compiler driver, and winpthread with abseil's threads, each as
# clang64's import library. winpthread has two of them: libwinpthread.dll.a
# and libpthread.dll.a, which is what -pthread (CMake's Threads::Threads)
# links, so both go. Neither lets the caller ask for the static archive
# instead, so the import libraries are swapped for the archives here, on the
# runner only: the linker goes by what is in the file, not by its name.
for lib in z zstd c++ unwind winpthread pthread; do
    cp "/clang64/lib/lib${lib}.a" "/clang64/lib/lib${lib}.dll.a"
done

mkdir -p build && cd build || exit 1

export CC="clang"
export CXX="clang++"
export LINKER=lld
export LINKER_FLAG="-fuse-ld=${LINKER}"
export AR="llvm-ar"
export RANLIB="llvm-ranlib"

cmake ..                                               \
    -DBUILD_LIBRETRO_CORE_ONLY=ON                      \
    -DCMAKE_BUILD_TYPE=Release                         \
    -DCMAKE_PREFIX_PATH=/clang64                       \
    -DUSE_NATIVE_INSTRUCTIONS=OFF                      \
    -DUSE_PRECOMPILED_HEADERS=OFF                      \
    -DCMAKE_EXE_LINKER_FLAGS="${LINKER_FLAG}"          \
    -DCMAKE_MODULE_LINKER_FLAGS="${LINKER_FLAG}"       \
    -DCMAKE_SHARED_LINKER_FLAGS="${LINKER_FLAG}"       \
    -DCMAKE_AR="$AR"                                   \
    -DCMAKE_RANLIB="$RANLIB"                           \
    -DUSE_SYSTEM_CURL=OFF                              \
    -DCURL_BROTLI=OFF                                  \
    -DCURL_ZSTD=OFF                                    \
    -DUSE_NGHTTP2=OFF                                  \
    -Dprotobuf_FORCE_FETCH_DEPENDENCIES=ON             \
    -DUSE_FAUDIO=OFF                                   \
    -DUSE_SDL=OFF                                      \
    -DUSE_SYSTEM_FFMPEG=ON                             \
    -DFFMPEG_INCLUDE_DIR="$FFMPEG_PREFIX/include"      \
    -DFFMPEG_LIBRARIES="$FFMPEG_LIBS"                  \
    -DUSE_SYSTEM_OPENCV=ON                             \
    -DUSE_SYSTEM_OPENAL=OFF                            \
    -DUSE_DISCORD_RPC=OFF                              \
    -DUSE_LTO=OFF                                      \
    -DOpenGL_GL_PREFERENCE=LEGACY                      \
    -DWITH_LLVM=ON                                     \
    -DLLVM_DIR=/clang64/lib/cmake/llvm                 \
    -DVulkan_LIBRARY=/clang64/lib/libvulkan-1.dll.a    \
    -DSTATIC_LINK_LLVM=ON                              \
    -G Ninja

ninja rpcs3_libretro

cd ..
mkdir -p artifacts
find build -name 'rpcs3_libretro.dll' -exec cp {} artifacts/rpcs3_libretro.dll \;
ls -la artifacts

# Strip, as the Linux and Android cores are: unstripped, the DLL carried the
# debug information of everything now linked into it and came to 200 MB.
# KEEP_SYMBOLS=1 leaves it for a debugger, as it does on Linux.
if [ "${KEEP_SYMBOLS:-0}" = "1" ]; then
    echo "KEEP_SYMBOLS=1: leaving the symbols in the DLL"
else
    echo "before: $(stat -c %s artifacts/rpcs3_libretro.dll) bytes"
    llvm-strip --strip-unneeded artifacts/rpcs3_libretro.dll
    echo "after:  $(stat -c %s artifacts/rpcs3_libretro.dll) bytes"
fi

# Fail here rather than on a tester's machine: anything the DLL imports beyond
# Windows itself, the Vulkan loader and OpenGL is a DLL nobody will have.
llvm-objdump -p artifacts/rpcs3_libretro.dll | awk '/DLL Name:/ { print $3 }' | sort -u > imports.txt
cat imports.txt
if grep -viE '^(api-ms-win-crt-.*|kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|ws2_32|iphlpapi|bcrypt|crypt32|pdh|dbghelp|ntdll|winmm|secur32|shlwapi|version|setupapi|opengl32|vulkan-1)\.dll$' imports.txt; then
    echo "The core imports DLLs that are not part of Windows (above)."
    exit 1
fi
