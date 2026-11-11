# The core without CMake: make, a compiler, and for the default build
# llvm-config, GLEW and the Vulkan loader. What it builds is what the CMake
# build (BUILD_LIBRETRO_CORE_ONLY, as .ci/build-libretro.sh configures it)
# builds, from the same sources with the same flags: Makefile.common has
# the lists.
#
#   make -j$(nproc)                     the core for this machine
#   make platform=osx                   macOS (arm64 or x86_64)
#   make platform=win                   Windows, with MinGW-w64
#   make HAVE_LLVM=0                    without the LLVM recompilers
#   make SYSTEM_FFMPEG=1                ffmpeg from pkg-config instead of
#                                       RPCS3's prebuilt one
#   make EMBED_PATCHES=0                without downloading patch.yml
#   make DEBUG=1                        -O0 -g
#   make check                          the core, loaded as a frontend loads it
#
# Objects are written next to their sources; `make clean` removes them.

DEBUG          ?= 0
HAVE_LLVM      ?= auto
LLVM_CONFIG    ?= llvm-config
LLVM_STATIC    ?= 1
SYSTEM_FFMPEG  ?= 0
EMBED_PATCHES  ?= 1
LTO            ?= 0

CORE_DIR := .
TARGET_NAME := rpcs3

# ECHO=1 (or E=1) prints each command in full
ifeq ($(ECHO),1)
   E := 1
endif
ifeq ($(E),1)
   Q :=
   SAY := @true
else
   Q := @
   SAY := @echo
endif

ifeq ($(platform),)
   platform = unix
   ifeq ($(shell uname -s),)
      platform = win
   else ifneq ($(findstring Darwin,$(shell uname -s)),)
      platform = osx
   else ifneq ($(findstring MINGW,$(shell uname -s)),)
      platform = win
   endif
endif

# The processor the core is for
ifeq ($(arch),)
   ifneq (,$(findstring win,$(platform)))
      arch := $(shell $(CC) -dumpmachine 2>/dev/null | cut -d- -f1)
   else
      arch := $(shell uname -m)
   endif
endif
ifneq (,$(filter aarch64 arm64,$(arch)))
   IS_ARM64 := 1
else
   IS_X86 := 1
endif

include Makefile.common

.DEFAULT_GOAL := all

HOST_CC ?= cc
PKG_CONFIG ?= pkg-config

DEFINES := -DLIBRETRO_CORE -DHAVE_VULKAN -DAL_LIBTYPE_STATIC -DASMJIT_STATIC
LIBS    :=

# Unix
ifeq ($(platform), unix)
   TARGET  := $(TARGET_NAME)_libretro.so
   fpic    := -fPIC
   SHARED  := -shared -Wl,--no-undefined -Wl,--version-script=$(CORE_DIR)/rpcs3/libretro/make/link.T
   OS_GLSLANG  := $(SOURCES_GLSLANG_UNIX)
   OS_LIBUSB   := $(SOURCES_LIBUSB_LINUX)
   OS_RPCS3    := $(SOURCES_RPCS3_GL)
   OS_LIBRETRO := $(SOURCES_LIBRETRO_GL)
   # As rpcs3/CMakeLists.txt has it, on Linux only (on Windows it is a type)
   DEFINES += -DGLEW_STATIC -DDATADIR=\"/usr/local/share/rpcs3\"
   FLAGS_GLSLANG += -DGLSLANG_OSINCLUDE_UNIX
   FLAGS_LIBUSB  += -DPLATFORM_POSIX=1 -DHAVE_CLOCK_GETTIME
   # The GL renderer's X11 parts, as the CMake build has them when X11 and
   # GLVND's libOpenGL are found
   ifeq ($(shell $(PKG_CONFIG) --exists x11 && echo yes),yes)
      DEFINES += -DHAVE_X11
      OS_LIBRETRO += $(SOURCES_LIBRETRO_X11)
   endif
   # GLEW statically, so the core does not need one libGLEW.so by soname
   GLEW_LIBS ?= -Wl,-Bstatic -lGLEW -Wl,-Bdynamic
   ifeq ($(shell $(PKG_CONFIG) --exists opengl && echo yes),yes)
      GL_LIBS ?= -lOpenGL
   else
      GL_LIBS ?= -lGL
   endif
   ifeq ($(shell $(PKG_CONFIG) --exists egl && echo yes),yes)
      GL_LIBS += -lEGL
   endif
   LIBS += $(GLEW_LIBS) $(GL_LIBS) -lvulkan -ldl -lrt -lpthread -lm
   FFMPEG_OS := linux

# OS X
else ifeq ($(platform), osx)
   TARGET  := $(TARGET_NAME)_libretro.dylib
   fpic    := -fPIC
   SHARED  := -dynamiclib
   OS_GLSLANG  := $(SOURCES_GLSLANG_UNIX)
   OS_LIBUSB   := $(SOURCES_LIBUSB_DARWIN)
   OS_RPCS3    := $(SOURCES_RPCS3_DARWIN)
   OS_LIBRETRO := $(SOURCES_LIBRETRO_NOGL)
   DEFINES += -DLIBRETRO_NO_GL -DHAVE_CLOCK_GETTIME
   FLAGS_GLSLANG += -DGLSLANG_OSINCLUDE_UNIX
   FLAGS_LIBUSB  += -DPLATFORM_POSIX=1 -DHAVE_CLOCK_GETTIME
   # RtMidi's own default on macOS: CoreMIDI
   FLAGS_RTMIDI  += -D__MACOSX_CORE__
   # Vulkan through MoltenVK
   VULKAN_LIBS ?= -lMoltenVK
   LIBS += $(VULKAN_LIBS) -liconv -framework CoreVideo -framework Foundation \
      -framework IOKit -framework Security -framework QuartzCore -framework Metal -framework AppKit \
      -framework CoreMIDI -framework CoreAudio
   ifeq ($(IS_ARM64),1)
      ARCH_FLAGS := -march=armv8.4-a
   endif
   FFMPEG_OS := macos

# Windows (MinGW-w64)
else ifneq (,$(findstring win,$(platform)))
   TARGET  := $(TARGET_NAME)_libretro.dll
   # Statically, so the DLL needs Windows' own DLLs, vulkan-1 and opengl32
   # and nothing else; RETRO_API's dllexport is what it exports
   SHARED  := -shared -static -static-libgcc -static-libstdc++ -Wl,--no-undefined
   OS_GLSLANG  := $(SOURCES_GLSLANG_WINDOWS)
   OS_LIBUSB   := $(SOURCES_LIBUSB_WINDOWS)
   OS_RPCS3    := $(SOURCES_RPCS3_GL)
   OS_LIBRETRO := $(SOURCES_LIBRETRO_GL)
   DEFINES += -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DGLEW_STATIC
   FLAGS_GLSLANG += -DGLSLANG_OSINCLUDE_WIN32
   FLAGS_LIBUSB  += -DPLATFORM_WINDOWS=1
   # RtMidi's own default on Windows: Windows multimedia MIDI (winmm)
   FLAGS_RTMIDI  += -D__WINDOWS_MM__
   VULKAN_LIBS ?= -lvulkan-1
   GLEW_LIBS ?= -lglew32
   LIBS += $(GLEW_LIBS) $(VULKAN_LIBS) -lopengl32 -lws2_32 -liphlpapi -lwinmm -lpsapi -lgdi32 \
      -lsetupapi -ldbghelp -lpdh -lbcrypt -lole32 -loleaut32 -luuid -lshlwapi \
      -lstrmiids -luser32 -lwinpthread
   FFMPEG_OS := mingw
else
   $(error platform $(platform) is not one this Makefile builds for: unix, osx or win)
endif

ifeq ($(IS_ARM64),1)
   OS_RPCS3 += $(SOURCES_RPCS3_AARCH64)
   ifeq ($(ARCH_FLAGS),)
      ARCH_FLAGS := -march=armv8.1-a
   endif
   FFMPEG_ARCH := arm64
else
   ARCH_FLAGS := -msse -msse2 -mcx16
   FFMPEG_ARCH := x64
endif

# GCC's warnings, or Clang's
ifneq (,$(findstring clang,$(shell $(CXX) --version 2>/dev/null)))
   WARNINGS_RPCS3 += $(WARNINGS_CLANG)
else
   WARNINGS_RPCS3 += $(WARNINGS_GCC)
endif

# LLVM, for the PPU and SPU recompilers: when llvm-config is there, unless
# HAVE_LLVM=0; linked statically, so the core is one file
# The JIT code uses LLVM 21's interfaces (the CI images have 22.1)
LLVM_MIN := 21

# In an MSYS2 shell, /usr/bin/llvm-config is MSYS's own LLVM, for the POSIX
# layer (x86_64-pc-windows-cygnus): its headers are MSYS's /usr/include,
# which do not go with MinGW's. MinGW's is under $MINGW_PREFIX.
ifneq (,$(findstring win,$(platform)))
   ifeq ($(origin LLVM_CONFIG),file)
      ifneq ($(MINGW_PREFIX),)
         ifneq ($(wildcard $(MINGW_PREFIX)/bin/llvm-config.exe $(MINGW_PREFIX)/bin/llvm-config),)
            LLVM_CONFIG := $(MINGW_PREFIX)/bin/llvm-config
         endif
      endif
   endif
endif

# An LLVM only for the system the core is for: its host triple has to name it
ifneq (,$(findstring win,$(platform)))
   LLVM_FOR := -w64- -windows-gnu mingw32
else ifeq ($(platform),osx)
   LLVM_FOR := -apple-
else
   LLVM_FOR := -linux
endif

ifneq ($(HAVE_LLVM),0)
   LLVM_MAJOR := $(shell $(LLVM_CONFIG) --version 2>/dev/null | cut -d. -f1)
   LLVM_HOST  := $(shell $(LLVM_CONFIG) --host-target 2>/dev/null)
   ifeq ($(LLVM_MAJOR),)
      LLVM_NO := $(LLVM_CONFIG) was not found
   else ifeq ($(strip $(foreach w,$(LLVM_FOR),$(findstring $(w),$(LLVM_HOST)))),)
      LLVM_NO := $(LLVM_CONFIG) is an LLVM for $(LLVM_HOST), not for a $(platform) core
   else ifneq ($(shell [ $(LLVM_MAJOR) -ge $(LLVM_MIN) ] && echo yes),yes)
      LLVM_NO := $(LLVM_CONFIG) is LLVM $(LLVM_MAJOR); $(LLVM_MIN) or newer is needed
   endif
   ifeq ($(HAVE_LLVM)$(LLVM_NO),auto)
      HAVE_LLVM := 1
   else ifeq ($(HAVE_LLVM),auto)
      $(warning $(LLVM_NO): building without the LLVM recompilers)
      HAVE_LLVM := 0
   else ifneq ($(LLVM_NO),)
      $(error HAVE_LLVM=1, but $(LLVM_NO))
   endif
endif
ifeq ($(HAVE_LLVM),1)
   LLVM_COMPONENTS := core executionengine mcjit passes
   # The profilers' JIT listeners on Linux, where this LLVM has them
   LLVM_HAS := $(shell $(LLVM_CONFIG) --components)
   ifeq ($(IS_ARM64),1)
      LLVM_COMPONENTS += aarch64
   else
      LLVM_COMPONENTS += x86
      ifeq ($(platform),unix)
         LLVM_COMPONENTS += $(filter inteljitevents,$(LLVM_HAS))
      endif
   endif
   ifeq ($(platform),unix)
      LLVM_COMPONENTS += $(filter perfjitevents,$(LLVM_HAS))
   endif
   # Statically when its archives are installed (as on the CI images), else
   # the shared libLLVM, which the core then needs beside it
   ifeq ($(LLVM_STATIC),1)
      ifneq ($(shell $(LLVM_CONFIG) --link-static --libs core >/dev/null 2>&1 && echo yes),yes)
         $(warning this LLVM has no static libraries: linking the shared one)
         LLVM_STATIC := 0
      endif
   endif
   LLVM_LINK := $(if $(filter 1,$(LLVM_STATIC)),--link-static,--link-shared)
   DEFINES   += -DLLVM_AVAILABLE $(filter -D%,$(shell $(LLVM_CONFIG) --cppflags))
   LLVM_INC  := -isystem $(shell $(LLVM_CONFIG) --includedir)
   LIBS      := $(shell $(LLVM_CONFIG) $(LLVM_LINK) --ldflags --libs $(LLVM_COMPONENTS) --system-libs) $(LIBS)
endif

# ffmpeg: RPCS3's prebuilt one, for the commit of 3rdparty/ffmpeg, as the
# CMake build downloads it; or the system's
ifeq ($(SYSTEM_FFMPEG),1)
   FFMPEG_INC  := $(shell $(PKG_CONFIG) --cflags libavformat libavcodec libavutil libswscale libswresample)
   FFMPEG_LIBS := $(shell $(PKG_CONFIG) --static --libs libavformat libavcodec libavutil libswscale libswresample)
   FFMPEG_DEP  :=
else
   FFMPEG_INC  := -isystem $(CORE_DIR)/3rdparty/ffmpeg/include
   FFMPEG_ZIP  := ffmpeg-$(FFMPEG_OS)-$(FFMPEG_ARCH).zip
   FFMPEG_DIR  := $(CORE_DIR)/3rdparty/ffmpeg/prebuilt/$(FFMPEG_OS)-$(FFMPEG_ARCH)
   FFMPEG_SHA  := $(shell git -C $(CORE_DIR)/3rdparty/ffmpeg rev-parse --short HEAD 2>/dev/null)
   FFMPEG_LIBS := $(addprefix $(FFMPEG_DIR)/lib/lib,avformat.a avcodec.a swscale.a swresample.a avutil.a)
   FFMPEG_DEP  := $(FFMPEG_DIR)/lib/libavcodec.a
   ifeq ($(platform),osx)
      FFMPEG_LIBS += -framework CoreServices -framework CoreFoundation -framework AudioUnit \
         -framework AudioToolbox -framework CoreAudio -framework CoreMedia \
         -framework VideoToolbox -framework CoreVideo
   endif
endif
LIBS := $(FFMPEG_LIBS) $(LIBS)

# The version: upstream's build this code is merged up to, and this commit
UPSTREAM_VERSION := $(shell sed -n 's/^RPCS3_VERSION=//p' $(CORE_DIR)/rpcs3/libretro/upstream.version)
UPSTREAM_COMMIT  := $(shell sed -n 's/^RPCS3_COMMIT=//p' $(CORE_DIR)/rpcs3/libretro/upstream.version)
CORE_COMMIT      := $(shell git -C $(CORE_DIR) rev-parse --short=7 HEAD 2>/dev/null)
CORE_BRANCH      := $(shell git -C $(CORE_DIR) rev-parse --abbrev-ref HEAD 2>/dev/null || echo local_build)
LIBRETRO_VERSION := $(UPSTREAM_VERSION)$(if $(CORE_COMMIT), (libretro core $(CORE_COMMIT)))

ifeq ($(DEBUG),1)
   OPT := -O0 -g
else
   OPT := -O3 -DNDEBUG
endif
ifeq ($(LTO),1)
   OPT += -flto
   LDFLAGS += -flto
   # GCC over-emits weak type information under LTO (rpcs3/Emu/CMakeLists.txt)
   $(CORE_DIR)/rpcs3/Emu/Cell/PPUThread.o $(CORE_DIR)/rpcs3/Emu/Cell/SPULLVMRecompiler.o: FLAGS += -fno-lto
endif

# Everything the core exports is its retro_* entry points (link.T); the
# rest is hidden, as the CMake build hides it with --exclude-libs
# Extra include paths and the like come in through CPPFLAGS, as a cross build
# needs them (.ci/build-windows-cross-libretro.sh's -idirafter)
ifeq (,$(findstring win,$(platform)))
   VISIBILITY := -fvisibility=hidden
endif
COMMON = $(OPT) $(fpic) $(VISIBILITY) -pthread -MMD -MP $(CPPFLAGS)

FLAGS_RPCS3 := -std=gnu++23 -fno-exceptions -fstack-protector $(ARCH_FLAGS) \
	$(DEFINES) $(INCLUDES_RPCS3) $(FFMPEG_INC) $(LLVM_INC) $(WARNINGS_RPCS3)
FLAGS_LIBRETRO := $(FLAGS_RPCS3) $(INCLUDES_LIBRETRO) -fexceptions -Wno-error=old-style-cast \
	-DRPCS3_LIBRETRO_VERSION='"$(LIBRETRO_VERSION)"'

# Each library's objects with its own flags
objs = $(patsubst %.mm,%.o,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(1))))

OBJ_LRC      := $(call objs,$(SOURCES_LRC))
OBJ_LIBUSB   := $(call objs,$(SOURCES_LIBUSB) $(OS_LIBUSB))
OBJ_GLSLANG  := $(call objs,$(SOURCES_GLSLANG) $(OS_GLSLANG))
OBJ_ASMJIT   := $(call objs,$(SOURCES_ASMJIT))
OBJ_RTMIDI   := $(call objs,$(SOURCES_RTMIDI))
OBJ_FUSION   := $(call objs,$(SOURCES_FUSION))
OBJ_LIBRETRO := $(call objs,$(SOURCES_LIBRETRO) $(SOURCES_GENERATED) $(OS_LIBRETRO))
OBJ_RPCS3    := $(call objs,$(SOURCES_RPCS3) $(OS_RPCS3))

OBJECTS := $(OBJ_LRC) $(OBJ_LIBUSB) $(OBJ_GLSLANG) $(OBJ_ASMJIT) $(OBJ_RTMIDI) $(OBJ_FUSION) \
	$(OBJ_RPCS3) $(OBJ_LIBRETRO)

$(OBJ_LRC):      FLAGS := $(FLAGS_LRC)
$(OBJ_LIBUSB):   FLAGS := $(FLAGS_LIBUSB)
$(OBJ_GLSLANG):  FLAGS := $(FLAGS_GLSLANG)
$(OBJ_ASMJIT):   FLAGS := $(FLAGS_ASMJIT)
$(OBJ_RTMIDI):   FLAGS := $(FLAGS_RTMIDI)
$(OBJ_FUSION):   FLAGS := $(FLAGS_FUSION)
$(OBJ_RPCS3):    FLAGS := $(FLAGS_RPCS3)
$(OBJ_LIBRETRO): FLAGS := $(FLAGS_LIBRETRO)

# Built without RTTI, as LLVM is (rpcs3/Emu/CMakeLists.txt)
$(addprefix $(CORE_DIR)/,rpcs3/Emu/Cell/PPUTranslator.o Utilities/JITLLVM.o Utilities/JITASM.o): FLAGS += -fno-rtti

# rpcs3_version.cpp reads git-version.h
$(CORE_DIR)/rpcs3/rpcs3_version.o: $(CORE_DIR)/rpcs3/git-version.h

# Objects are written next to their sources, and their names do not say
# which toolchain made them: a build for another platform or processor in
# the same checkout clears the last one's first, at parse time, before make
# reads their dependency files.
BUILD_TAG := $(CC)|$(CXX)|$(platform)|$(arch)|$(HAVE_LLVM)|$(if $(filter 1,$(HAVE_LLVM)),$(shell $(LLVM_CONFIG) --version))|$(SYSTEM_FFMPEG)|$(DEBUG)|$(LTO)
BUILD_TAG_STATUS := $(shell \
	if [ ! -f .build-tag ] || [ "`cat .build-tag 2>/dev/null`" != "$(BUILD_TAG)" ]; then \
		if [ -f .build-tag ]; then \
			find . -name '*.o' -not -path './build*' -delete; \
			find . -name '*.d' -not -path './build*' -delete; \
			echo cleaned; \
		fi; \
		echo "$(BUILD_TAG)" > .build-tag; \
	fi)
ifeq ($(BUILD_TAG_STATUS),cleaned)
   $(info toolchain changed, removed objects from the previous build)
endif

all: $(TARGET)

# RPCS3 and the libraries go into archives, as the CMake build has them, so
# the link takes only the objects the core reaches: some of RPCS3's are for
# the standalone application and name what only it defines
ARCHIVES := \
	$(CORE_DIR)/librpcs3_emu.a \
	$(CORE_DIR)/liblibretro_common.a \
	$(CORE_DIR)/libglslang.a \
	$(CORE_DIR)/libasmjit.a \
	$(CORE_DIR)/libusb.a \
	$(CORE_DIR)/librtmidi.a \
	$(CORE_DIR)/libfusion.a

$(CORE_DIR)/librpcs3_emu.a: $(OBJ_RPCS3)
$(CORE_DIR)/liblibretro_common.a: $(OBJ_LRC)
$(CORE_DIR)/libglslang.a: $(OBJ_GLSLANG)
$(CORE_DIR)/libasmjit.a: $(OBJ_ASMJIT)
$(CORE_DIR)/libusb.a: $(OBJ_LIBUSB)
$(CORE_DIR)/librtmidi.a: $(OBJ_RTMIDI)
$(CORE_DIR)/libfusion.a: $(OBJ_FUSION)

$(ARCHIVES):
	$(SAY) AR $@
	$(Q)rm -f $@
	$(Q)$(AR) rcs $@ $^

ifneq ($(platform),osx)
   GROUP_START := -Wl,--start-group
   GROUP_END   := -Wl,--end-group
endif

$(TARGET): $(OBJ_LIBRETRO) $(ARCHIVES) $(FFMPEG_DEP)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(fpic) $(SHARED) $(LIBS)

%.o: %.cpp
	$(SAY) CXX $<
	$(Q)$(CXX) -c -o $@ $< $(COMMON) $(FLAGS)

%.o: %.c
	$(SAY) CC $<
	$(Q)$(CC) -c -o $@ $< $(COMMON) $(FLAGS)

%.o: %.mm
	$(SAY) OBJCXX $<
	$(Q)$(CXX) -x objective-c++ -c -o $@ $< $(COMMON) $(FLAGS)

# ---- made by the build ----

EMBED := $(GEN_DIR)/embed$(if $(findstring win,$(platform)),.exe)
EMBED_LRC := $(addprefix $(CORE_DIR)/rpcs3/libretro/libretro-common/, \
	formats/json/rjson.c hash/lrc_hash.c string/rstrtod.c string/stdstring.c \
	compat/compat_strl.c compat/compat_strcasestr.c compat/fopen_utf8.c \
	features/features_cpu.c streams/file_stream.c vfs/vfs_implementation.c \
	encodings/encoding_utf.c file/file_path.c file/file_path_io.c time/rtime.c)

$(EMBED): $(CORE_DIR)/rpcs3/libretro/make/embed.c $(EMBED_LRC)
	@mkdir -p $(GEN_DIR)
	$(HOST_CC) -O2 -std=gnu99 -w -I$(CORE_DIR)/rpcs3/libretro/libretro-common/include -o $@ $^

ICON_DIR   := $(CORE_DIR)/bin/Icons/ui
ICON_NAMES := $(sort $(patsubst $(ICON_DIR)/%,%,$(wildcard $(ICON_DIR)/*.png $(ICON_DIR)/home/32/*.png)))

$(GEN_DIR)/libretro_ui_icons.cpp: $(EMBED) $(addprefix $(ICON_DIR)/,$(ICON_NAMES))
	$(EMBED) icons $@ $(ICON_DIR) $(ICON_NAMES)

$(GEN_DIR)/libretro_config_database.cpp: $(EMBED) $(CORE_DIR)/rpcs3/libretro/config_database.dat
	$(EMBED) bytes $@ g_libretro_config_database g_libretro_config_database_size config_database.dat $(CORE_DIR)/rpcs3/libretro/config_database.dat

$(GEN_DIR)/libretro_vblank_unlock.cpp: $(EMBED) $(CORE_DIR)/rpcs3/libretro/vblank_unlock.txt
	$(EMBED) bytes $@ g_libretro_vblank_unlock g_libretro_vblank_unlock_size vblank_unlock.txt $(CORE_DIR)/rpcs3/libretro/vblank_unlock.txt

# RPCS3's game patches for this patch engine, fetched once as the CMake
# build fetches them at configure time (rpcs3/libretro/embed_patches.cmake):
# from rpcs3.net, else the GitHub mirror, else none. `make patches` fetches
# them again.
PATCH_ENGINE := $(shell sed -n 's/.*patch_engine_version = "\([0-9.]*\)".*/\1/p' $(CORE_DIR)/Utilities/bin_patch.h)

$(GEN_DIR)/libretro_patches.cpp: $(EMBED)
ifeq ($(EMBED_PATCHES),1)
	-curl -fsSL --max-time 60 -o $(GEN_DIR)/patch.json "https://rpcs3.net/compatibility?patch&api=v1&v=$(PATCH_ENGINE)" || rm -f $(GEN_DIR)/patch.json
	-[ -s $(GEN_DIR)/patch.json ] || curl -fsSL --max-time 60 -o $(GEN_DIR)/patch.yml "https://raw.githubusercontent.com/illusionyy/rpcs3-game-patch/main/patch.yml"
endif
	$(EMBED) patch $@ $(PATCH_ENGINE) $(GEN_DIR)/patch.json $(GEN_DIR)/patch.yml
	@rm -f $(GEN_DIR)/patch.json $(GEN_DIR)/patch.yml

patches:
	rm -f $(GEN_DIR)/libretro_patches.cpp
	$(MAKE) $(GEN_DIR)/libretro_patches.cpp

# git-version.h, as rpcs3/git-version.cmake and upstream_version.cmake
# write it: upstream's build and commit; rewritten only when it changes
GIT_VERSION_H = // This is a generated file.\n\n\#define RPCS3_GIT_VERSION "$(lastword $(subst -, ,$(UPSTREAM_VERSION)))-$(shell echo $(UPSTREAM_COMMIT) | cut -c1-8)"\n\#define RPCS3_GIT_BRANCH "$(CORE_BRANCH)"\n\#define RPCS3_GIT_FULL_BRANCH "$(if $(BUILD_REPOSITORY_NAME),$(BUILD_REPOSITORY_NAME)/$(BUILD_SOURCEBRANCHNAME),local_build)"\n\n// If you do not want this file to update/recompile, change to 1.\n\#define RPCS3_GIT_VERSION_NO_UPDATE 0\n

$(CORE_DIR)/rpcs3/git-version.h: FORCE
	@printf '$(GIT_VERSION_H)' > $@.tmp; \
	if grep -q 'RPCS3_GIT_VERSION_NO_UPDATE 1' $@ 2>/dev/null || cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

# RPCS3's prebuilt ffmpeg
$(FFMPEG_DIR)/lib/libavcodec.a:
	@mkdir -p $(FFMPEG_DIR)
	curl -fL --retry 3 -o $(FFMPEG_DIR)/$(FFMPEG_ZIP) "https://github.com/RPCS3/ffmpeg-core/releases/download/$(FFMPEG_SHA)/$(FFMPEG_ZIP)"
	cd $(FFMPEG_DIR) && unzip -q -o $(FFMPEG_ZIP) && rm -f $(FFMPEG_ZIP)
	@touch $@

# The core, loaded with every symbol resolved and asked what it is
check: $(TARGET)
	$(HOST_CC) -O2 -o $(GEN_DIR)/load_test $(CORE_DIR)/rpcs3/libretro/make/load_test.c -ldl
	$(GEN_DIR)/load_test ./$(TARGET)

-include $(OBJECTS:.o=.d)

clean:
	@find . -name '*.o' -not -path './build*' -not -path './3rdparty/llvm/*' -delete
	@find . -name '*.d' -not -path './build*' -not -path './3rdparty/llvm/*' -delete
	rm -rf $(TARGET) $(ARCHIVES) $(GEN_DIR) .build-tag

FORCE:

.PHONY: all check clean patches FORCE
