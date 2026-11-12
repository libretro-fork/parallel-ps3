#!/bin/sh -ex

# Builds the libretro core for macOS on the libretro buildbot's Macs: Xcode's
# clang, cmake, ninja and git, and no Homebrew - so nothing the GitHub workflow
# takes from Homebrew is there. Instead:
# - LLVM is built from its submodule into a prefix of its own, which the
#   GitLab cache keeps between pipelines (a stamp names the submodule commit
#   it was built from, so a new one is built when the submodule moves);
# - so is clang, from the same submodule, for the machine: Xcode's clang 16
#   rejects RPCS3's C++ (fmt::throw_exception not taken as [[noreturn]],
#   SPULLVMRecompiler's deduced return types), and upstream builds macOS with
#   Homebrew's LLVM clang for the same reason;
# - MoltenVK is its release, for the Vulkan headers and the library the core
#   links; the core finds it at run time as @rpath/libMoltenVK.dylib, the one
#   RetroArch carries in its Frameworks;
# - OpenCV is left out, as it is optional.
#
# ARCH is arm64 or x86_64 (default: the machine's); the x86_64 slice is
# cross-built on Apple Silicon, as the buildbot runs it. Expects to run from
# the top of the source tree, with the submodule URLs already pointing at
# GitHub (see .gitlab-ci.yml). The core ends up as ./parallel_ps3_libretro.dylib.

ARCH="${ARCH:-$(uname -m)}"
case "$ARCH" in
    arm64)  LLVM_TARGET=AArch64; TRIPLE=arm64-apple-darwin ;;
    x86_64) LLVM_TARGET=X86;     TRIPLE=x86_64-apple-darwin ;;
    *) echo "ARCH must be arm64 or x86_64, not $ARCH"; exit 1 ;;
esac
SRC="$PWD"
DEPS="$SRC/deps-macos-$ARCH"
JOBS="${NUMPROC:-$(sysctl -n hw.ncpu)}"
[ "$JOBS" -ge 1 ] || JOBS=1
TARGET_OS=14.0
MOLTENVK=v1.4.2

# The template asks for an older macOS; RPCS3 needs 14.
export MACOSX_DEPLOYMENT_TARGET=$TARGET_OS
SDK="$(xcrun --sdk macosx --show-sdk-path)"
# Nothing from a package manager, should the machine have one.
IGNORE="/opt/homebrew;/usr/local;/opt/local"

git config --global --add safe.directory '*'

# Everything but the ones this build does not use.
# shellcheck disable=SC2046
git submodule -q update --init --force --depth 1 $(awk '/path/ && !/opencv/ && !/libsdl-org/ { print $3 }' .gitmodules)

mkdir -p "$DEPS"

# LLVM, static, for this slice only.
LLVM_COMMIT="$(git rev-parse HEAD:3rdparty/llvm/llvm)"
if [ "$(cat "$DEPS/llvm/stamp" 2>/dev/null)" != "$LLVM_COMMIT-$ARCH-$TARGET_OS" ]; then
    rm -rf "$DEPS/llvm" "$DEPS/llvm-build"
    cmake -S 3rdparty/llvm/llvm/llvm -B "$DEPS/llvm-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS/llvm" \
        -DCMAKE_OSX_ARCHITECTURES=$ARCH \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$TARGET_OS \
        -DCMAKE_OSX_SYSROOT="$SDK" \
        -DCMAKE_IGNORE_PREFIX_PATH="$IGNORE" \
        -DLLVM_HOST_TRIPLE=$TRIPLE \
        -DLLVM_TARGETS_TO_BUILD=$LLVM_TARGET \
        -DLLVM_BUILD_TOOLS=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_INCLUDE_DOCS=OFF \
        -DLLVM_ENABLE_ZSTD=OFF \
        -DLLVM_ENABLE_ZLIB=OFF \
        -DLLVM_ENABLE_LIBXML2=OFF \
        -DLLVM_ENABLE_LIBEDIT=OFF \
        -DLLVM_ENABLE_TERMINFO=OFF \
        -DLLVM_ENABLE_WARNINGS=OFF
    cmake --build "$DEPS/llvm-build" -j "$JOBS"
    cmake --install "$DEPS/llvm-build"
    rm -rf "$DEPS/llvm-build"
    echo "$LLVM_COMMIT-$ARCH-$TARGET_OS" > "$DEPS/llvm/stamp"
fi

# clang for this machine, the compiler the core is built with; it targets
# both slices, and takes the libc++ headers from the SDK.
HOST="$SRC/deps-macos-host"
CLANG="$HOST/clang/bin/clang"
if [ "$(cat "$HOST/clang/stamp" 2>/dev/null)" != "$LLVM_COMMIT" ]; then
    rm -rf "$HOST/clang" "$HOST/clang-build"
    cmake -S 3rdparty/llvm/llvm/llvm -B "$HOST/clang-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$HOST/clang" \
        -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
        -DCMAKE_OSX_SYSROOT="$SDK" \
        -DCMAKE_IGNORE_PREFIX_PATH="$IGNORE" \
        -DLLVM_ENABLE_PROJECTS=clang \
        -DLLVM_TARGETS_TO_BUILD="AArch64;X86" \
        -DLLVM_DISTRIBUTION_COMPONENTS="clang;clang-resource-headers" \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_INCLUDE_DOCS=OFF \
        -DCLANG_INCLUDE_TESTS=OFF \
        -DCLANG_INCLUDE_DOCS=OFF \
        -DLLVM_ENABLE_ZSTD=OFF \
        -DLLVM_ENABLE_ZLIB=OFF \
        -DLLVM_ENABLE_LIBXML2=OFF \
        -DLLVM_ENABLE_LIBEDIT=OFF \
        -DLLVM_ENABLE_TERMINFO=OFF \
        -DLLVM_ENABLE_WARNINGS=OFF
    cmake --build "$HOST/clang-build" --target install-distribution -j "$JOBS"
    rm -rf "$HOST/clang-build"
    echo "$LLVM_COMMIT" > "$HOST/clang/stamp"
fi
"$CLANG" --version
# It has to build a C++20 program for the slice with the SDK's libc++ before
# CMake gets it; say why when it can't.
printf '#include <atomic>\n#include <cstdio>\nint main() { std::atomic<int> a{0}; std::printf("%%d", a.load()); }\n' > "$DEPS/smoke.cpp"
"$CLANG++" -std=c++20 -arch $ARCH -isysroot "$SDK" -mmacosx-version-min=$TARGET_OS -v "$DEPS/smoke.cpp" -o "$DEPS/smoke"

# MoltenVK.
MVK="$DEPS/MoltenVK/MoltenVK"
if [ ! -f "$MVK/dynamic/dylib/macOS/libMoltenVK.dylib" ]; then
    rm -rf "$DEPS/MoltenVK"
    curl -sSL "https://github.com/KhronosGroup/MoltenVK/releases/download/$MOLTENVK/MoltenVK-macos.tar" | tar -x -C "$DEPS"
fi
ls "$MVK/include"

# The x86_64 slice on Apple Silicon is configured as a cross build: CMake
# otherwise takes CMAKE_SYSTEM_PROCESSOR from the machine it runs on (setting
# it alone does not stick), and RPCS3 picks its AArch64 sources by it. No
# C++20 module scanning: RPCS3 has no modules, and the clang built above has
# no clang-scan-deps.
CROSS=""
if [ "$ARCH" != "$(uname -m)" ]; then
    CROSS="-DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_SYSTEM_PROCESSOR=$ARCH"
fi
# shellcheck disable=SC2086
cmake -S . -B build -G Ninja $CROSS                    \
    -DCMAKE_BUILD_TYPE=Release                         \
    -DCMAKE_C_COMPILER="$CLANG"                        \
    -DCMAKE_CXX_COMPILER="$CLANG++"                    \
    -DCMAKE_OSX_ARCHITECTURES=$ARCH                    \
    -DCMAKE_CXX_SCAN_FOR_MODULES=OFF                   \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$TARGET_OS           \
    -DCMAKE_OSX_SYSROOT="$SDK"                         \
    -DCMAKE_IGNORE_PREFIX_PATH="$IGNORE"               \
    -DBUILD_LIBRETRO_CORE_ONLY=ON                      \
    -DWITH_LLVM=ON                                     \
    -DBUILD_LLVM=OFF                                   \
    -DSTATIC_LINK_LLVM=ON                              \
    -DLLVM_DIR="$DEPS/llvm/lib/cmake/llvm"             \
    -DUSE_NATIVE_INSTRUCTIONS=OFF                      \
    -DUSE_PRECOMPILED_HEADERS=OFF                      \
    -DUSE_SYSTEM_OPENCV=OFF                            \
    -DUSE_LTO=OFF                                      \
    -DUSE_SYSTEM_MVK=ON                                \
    -DVulkan_INCLUDE_DIR="$MVK/include"                \
    -DVulkan_LIBRARY="$MVK/dynamic/dylib/macOS/libMoltenVK.dylib" || true

# The checks that failed, with the compiler's own words, so a configure that
# goes wrong says why
awk '/^  -$/ { if (failed) printf "%s", entry; entry = ""; failed = 0 }
     { entry = entry $0 "\n" }
     /exitCode: [1-9]/ { failed = 1 }
     END { if (failed) printf "%s", entry }' build/CMakeFiles/CMakeConfigureLog.yaml 2>/dev/null | head -400 || true
[ -f build/build.ninja ] || exit 1

# -k 0 under KEEP_GOING=1, so a trial run reports every error, not the first.
cmake --build build --target rpcs3_libretro -j "$JOBS" -- ${KEEP_GOING:+-k 0}

CORE=parallel_ps3_libretro.dylib
cp "$(find build -name $CORE | head -1)" $CORE
strip -x $CORE
LINKED_MVK=$(otool -L $CORE | awk '/libMoltenVK/ {print $1}')
[ -z "$LINKED_MVK" ] || [ "$LINKED_MVK" = @rpath/libMoltenVK.dylib ] ||
    install_name_tool -change "$LINKED_MVK" @rpath/libMoltenVK.dylib $CORE
codesign -f -s - $CORE
lipo -info $CORE
# What the core needs at load time: the system's libraries and MoltenVK,
# nothing else.
otool -L $CORE
if otool -L $CORE | tail -n +2 | grep -v -e '^[[:space:]]*/usr/lib/' -e '^[[:space:]]*/System/' -e '@rpath/libMoltenVK.dylib' -e "@rpath/$CORE"; then
    echo "linked to something RetroArch does not have"
    exit 1
fi
ls -la $CORE
