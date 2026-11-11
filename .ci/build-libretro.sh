#!/bin/sh -ex

# Builds the libretro core rather than the Qt application. Everything here
# mirrors build-linux-aarch64.sh - same images, same toolchain, same LLVM at
# /opt/llvm - with the GUI switched off and the core copied out at the end.
# LLVM is linked statically, so the core is a single file with nothing to
# place beside it.
#
# GLEW is linked statically. find_package(GLEW) otherwise hands back the
# shared libGLEW.so.2.2 from the build image, and the finished core then needs
# that exact soname on whatever machine it lands on - the first person to try
# this build could not load it for that reason. A core should carry what is
# not part of a normal system.
#
# TLS (RPCN, the clans server) is libretro-common's own client, and HTTP
# (UPnP, the clans server) its net_http: no wolfSSL and no libcurl, whose
# system copy on the images is Debian's libcurl-gnutls.so.4, which Fedora,
# Arch, the flatpak runtimes RetroArch is built on and most handheld
# distributions do not have.
#
# Wayland is disabled deliberately. With it found, rpcs3/Emu links OpenGL::EGL,
# an imported target created by a find_package down in 3rdparty/ and therefore
# visible only in that directory - the generate step then fails with "target
# was not found". The core has no use for Wayland anyway: the frontend hands it
# the context.

cd "${RPCS3_DIR:-rpcs3}" || exit 1

git config --global --add safe.directory '*'

# Pull all the submodules except some. LLVM is deliberately left out: the image
# already carries a built one at /opt/llvm.
# shellcheck disable=SC2046
git submodule -q update --init --force $(awk '/path/ && !/llvm/ && !/opencv/ && !/libsdl-org/ { print $3 }' .gitmodules)

mkdir -p build && cd build || exit 1

if [ "$COMPILER" = "gcc" ]; then
    # These are set in the dockerfile
    export CC="${GCC_BINARY}"
    export CXX="${GXX_BINARY}"
    export LINKER=gold
else
    export CC="${CLANG_BINARY}"
    export CXX="${CLANGXX_BINARY}"
    export LINKER="${LLD_BINARY}"
fi

export LINKER_FLAG="-fuse-ld=${LINKER}"

# Input and audio come from the frontend, so the handlers that would bring their
# own do not belong here - and each of them is a library the core would then
# name in NEEDED. libSDL3, libopenal and libevdev are all absent from the
# freedesktop runtime the RetroArch flatpak is built on, which is why a core
# that links them does not load there at all (reported on Bazzite). SDL goes
# with USE_SDL, FAudio with it (FAudio needs SDL3), libevdev with USE_LIBEVDEV,
# and OpenAL through BUILD_LIBRETRO_CORE_ONLY in 3rdparty/CMakeLists.txt.
cmake ..                                               \
    -DCMAKE_BUILD_TYPE=Release                         \
    -DBUILD_LIBRETRO_CORE_ONLY=ON                      \
    -DUSE_NATIVE_INSTRUCTIONS=OFF                      \
    -DUSE_PRECOMPILED_HEADERS=OFF                      \
    -DCMAKE_EXE_LINKER_FLAGS="${LINKER_FLAG}"          \
    -DCMAKE_MODULE_LINKER_FLAGS="${LINKER_FLAG}"       \
    -DCMAKE_SHARED_LINKER_FLAGS="${LINKER_FLAG}"       \
    -DUSE_SDL=OFF                                      \
    -DUSE_FAUDIO=OFF                                   \
    -DUSE_LIBEVDEV=OFF                                 \
    -DUSE_SYSTEM_FFMPEG=OFF                            \
    -DUSE_SYSTEM_OPENCV=ON                             \
    -DUSE_DISCORD_RPC=OFF                              \
    -DOpenGL_GL_PREFERENCE=LEGACY                      \
    -DCMAKE_DISABLE_FIND_PACKAGE_Wayland=ON            \
    -DGLEW_USE_STATIC_LIBS=ON                          \
    -DLLVM_DIR=/opt/llvm/lib/cmake/llvm                \
    -DSTATIC_LINK_LLVM=ON                              \
    -G Ninja

ninja rpcs3_libretro

CORE=rpcs3/libretro/parallel_ps3_libretro.so
test -f "$CORE"

mkdir -p "$ARTDIR"
cp "$CORE" "$ARTDIR/"
# Stripped, so the release is a download rather than a debug build. A tester
# chasing a hang needs the opposite - a backtrace off a stripped core is 26
# frames of "?? ()" - so KEEP_SYMBOLS=1 leaves the symbol table in.
if [ "${KEEP_SYMBOLS:-0}" = "1" ]; then
    echo "KEEP_SYMBOLS=1: leaving the symbol table in the core"
else
    "${STRIP_BINARY:-strip}" --strip-unneeded "$ARTDIR/parallel_ps3_libretro.so" || true
fi
ls -la "$ARTDIR"
