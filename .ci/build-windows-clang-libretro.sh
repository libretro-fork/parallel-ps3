#!/bin/sh -ex

# Builds the libretro core on Windows, under MSYS2's clang64 environment.
#
# Not MSVC: RPCS3 builds Windows two ways, msbuild over rpcs3.sln and this one,
# and the core exists only as a CMake target so the solution is no use. MSVC
# with CMake does not work either - the LLVM on the runner image is the client
# distribution, with no CMake package and no static libraries, so
# find_package(LLVM) finds nothing. clang64 has all of it as packages: LLVM
# with its CMake config, and OpenCV besides. Vulkan's headers come from the
# tree and its loader is opened at run time; GLEW is not used on Windows.

git config --global --add safe.directory '*'

# The ones clang64 provides as packages are left out, as is LLVM.
# shellcheck disable=SC2046
git submodule -q update --init --depth 1 $(awk '/path/ && !/llvm/ && !/opencv/ && !/FAudio/ { print $3 }' .gitmodules)

# The core has to be one DLL that needs nothing but Windows and OpenGL (the
# Vulkan loader is opened at run time, not imported). RetroArch loads it with LoadLibraryW on its full path, so Windows
# looks for the DLLs it depends on next to retroarch.exe, in System32 and on
# PATH - never in the cores folder - and a core that links clang64's shared
# libraries does not load at all: "missing modules" (NNshi). What it pulled in
# were libc++, zlib and zstd (and ffmpeg's five, before cellRec moved to
# libretro-common).

# zlib and zstd come in through LLVM's CMake package, libc++ and libunwind
# through the compiler driver, and winpthread, each as
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
    -DUSE_SYSTEM_OPENCV=ON                             \
    -DUSE_SYSTEM_OPENAL=OFF                            \
    -DUSE_LTO=OFF                                      \
    -DOpenGL_GL_PREFERENCE=LEGACY                      \
    -DWITH_LLVM=ON                                     \
    -DLLVM_DIR=/clang64/lib/cmake/llvm                 \
    -DSTATIC_LINK_LLVM=ON                              \
    -G Ninja

ninja rpcs3_libretro

cd ..
mkdir -p artifacts
find build -name 'parallel_ps3_libretro.dll' -exec cp {} artifacts/parallel_ps3_libretro.dll \;
ls -la artifacts

# Strip, as the Linux and Android cores are: unstripped, the DLL carried the
# debug information of everything now linked into it and came to 200 MB.
# KEEP_SYMBOLS=1 leaves it for a debugger, as it does on Linux.
if [ "${KEEP_SYMBOLS:-0}" = "1" ]; then
    echo "KEEP_SYMBOLS=1: leaving the symbols in the DLL"
else
    echo "before: $(stat -c %s artifacts/parallel_ps3_libretro.dll) bytes"
    llvm-strip --strip-unneeded artifacts/parallel_ps3_libretro.dll
    echo "after:  $(stat -c %s artifacts/parallel_ps3_libretro.dll) bytes"
fi

# Fail here rather than on a tester's machine: anything the DLL imports beyond
# Windows itself and OpenGL is a DLL nobody will have. vulkan-1 is not
# imported either: volk opens it at run time.
llvm-objdump -p artifacts/parallel_ps3_libretro.dll | awk '/DLL Name:/ { print $3 }' | sort -u > imports.txt
cat imports.txt
if grep -viE '^(api-ms-win-crt-.*|kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|ws2_32|iphlpapi|bcrypt|crypt32|pdh|dbghelp|ntdll|winmm|secur32|shlwapi|version|setupapi|opengl32)\.dll$' imports.txt; then
    echo "The core imports DLLs that are not part of Windows (above)."
    exit 1
fi
