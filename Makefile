# The core without CMake: make and a compiler. LLVM is built from the
# 3rdparty/llvm sources into the core, the GL and Vulkan headers are in
# the tree. What it builds is what the CMake
# build (BUILD_LIBRETRO_CORE_ONLY, as .ci/build-libretro.sh configures it)
# builds, from the same sources with the same flags: Makefile.common has
# the lists.
#
#   make -j$(nproc)                     the core for this machine
#   make platform=osx                   macOS (arm64 or x86_64)
#   make platform=win                   Windows, with MinGW-w64
#   make HAVE_LLVM=0                    without the LLVM recompilers
#   make SYSTEM_LLVM=1                  the system's LLVM (llvm-config, 21 or
#                                       newer) instead of the tree's
#   make EMBED_PATCHES=0                without downloading patch.yml
#   make DEBUG=1                        -O0 -g
#   make check                          the core, loaded as a frontend loads it
#
# Objects are written next to their sources; `make clean` removes them.

DEBUG          ?= 0
HAVE_LLVM      ?= 1
SYSTEM_LLVM    ?= 0
LLVM_CONFIG    ?= llvm-config
LLVM_STATIC    ?= 1
EMBED_PATCHES  ?= 1
LTO            ?= 0

CORE_DIR := .
TARGET_NAME := parallel_ps3

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

DEFINES := -DLIBRETRO_CORE -DHAVE_VULKAN -DVK_NO_PROTOTYPES -DAL_LIBTYPE_STATIC
LIBS    :=

# Unix
ifeq ($(platform), unix)
   TARGET  := $(TARGET_NAME)_libretro.so
   fpic    := -fPIC
   SHARED  := -shared -Wl,--no-undefined -Wl,--version-script=$(CORE_DIR)/rpcs3/libretro/make/link.T
   OS_LIBUSB   := $(SOURCES_LIBUSB_LINUX)
   OS_RPCS3    := $(SOURCES_RPCS3_GL)
   OS_LIBRETRO := $(SOURCES_LIBRETRO_GL)
   # As rpcs3/CMakeLists.txt has it, on Linux only (on Windows it is a type)
   DEFINES += -DDATADIR=\"/usr/local/share/rpcs3\"
   FLAGS_LIBUSB  += -DPLATFORM_POSIX=1 -DHAVE_CLOCK_GETTIME
   # No GL or window system library: OpenGL comes from the frontend
   LIBS += -ldl -lrt -lpthread -lm

# OS X
else ifeq ($(platform), osx)
   TARGET  := $(TARGET_NAME)_libretro.dylib
   fpic    := -fPIC
   SHARED  := -dynamiclib
   OS_LIBUSB   := $(SOURCES_LIBUSB_DARWIN)
   OS_RPCS3    := $(SOURCES_RPCS3_DARWIN)
   OS_LIBRETRO := $(SOURCES_LIBRETRO_NOGL)
   DEFINES += -DLIBRETRO_NO_GL -DHAVE_CLOCK_GETTIME
   FLAGS_LIBUSB  += -DPLATFORM_POSIX=1 -DHAVE_CLOCK_GETTIME
   # RtMidi's own default on macOS: CoreMIDI
   FLAGS_RTMIDI  += -D__MACOSX_CORE__
   LIBS += -liconv -framework CoreFoundation -framework Foundation \
      -framework IOKit -framework Security -framework QuartzCore -framework Metal -framework AppKit \
      -framework CoreMIDI -framework CoreAudio
   ifeq ($(IS_ARM64),1)
      ARCH_FLAGS := -march=armv8.4-a
   endif

# Windows (MinGW-w64)
else ifneq (,$(findstring win,$(platform)))
   TARGET  := $(TARGET_NAME)_libretro.dll
   # Statically, so the DLL needs Windows' own DLLs and nothing
   # else (volk opens vulkan-1.dll at run time); RETRO_API's dllexport is
   # what it exports
   SHARED  := -shared -static -static-libgcc -static-libstdc++ -Wl,--no-undefined
   OS_LIBUSB   := $(SOURCES_LIBUSB_WINDOWS)
   OS_RPCS3    := $(SOURCES_RPCS3_GL)
   OS_LIBRETRO := $(SOURCES_LIBRETRO_GL)
   DEFINES += -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00
   FLAGS_LIBUSB  += -DPLATFORM_WINDOWS=1
   # RtMidi's own default on Windows: Windows multimedia MIDI (winmm)
   FLAGS_RTMIDI  += -D__WINDOWS_MM__
   LIBS += -lws2_32 -liphlpapi -lwinmm -lpsapi -lgdi32 \
      -lsetupapi -ldbghelp -lpdh -lbcrypt -lole32 -loleaut32 -luuid -lshlwapi \
      -lstrmiids -luser32 -lwinpthread
else
   $(error platform $(platform) is not one this Makefile builds for: unix, osx or win)
endif

ifeq ($(IS_ARM64),1)
   OS_RPCS3 += $(SOURCES_RPCS3_AARCH64)
   ifeq ($(ARCH_FLAGS),)
      ARCH_FLAGS := -march=armv8.1-a
   endif
else
   ARCH_FLAGS := -msse -msse2 -mcx16
endif

# GCC's warnings, or Clang's
ifneq (,$(findstring clang,$(shell $(CXX) --version 2>/dev/null)))
   WARNINGS_RPCS3 += $(WARNINGS_CLANG)
else
   WARNINGS_RPCS3 += $(WARNINGS_GCC)
endif

# LLVM, for the PPU and SPU recompilers. By default the LLVM the core needs is
# built from the sources in 3rdparty/llvm/llvm, which are part of this
# repository, and linked into the core: nothing to install or fetch. The lists it is
# built from, and the headers LLVM's configuration would write, are in
# rpcs3/libretro/make/llvm. SYSTEM_LLVM=1 takes the system's through
# llvm-config instead; HAVE_LLVM=0 builds without the recompilers.
ifeq ($(HAVE_LLVM),auto)
   override HAVE_LLVM := 1
endif
ifeq ($(HAVE_LLVM),0)
   override SYSTEM_LLVM := 0
endif

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

ifeq ($(SYSTEM_LLVM),1)
   LLVM_MAJOR := $(shell $(LLVM_CONFIG) --version 2>/dev/null | cut -d. -f1)
   LLVM_HOST  := $(shell $(LLVM_CONFIG) --host-target 2>/dev/null)
   ifeq ($(LLVM_MAJOR),)
      LLVM_NO := $(LLVM_CONFIG) was not found
   else ifeq ($(strip $(foreach w,$(LLVM_FOR),$(findstring $(w),$(LLVM_HOST)))),)
      LLVM_NO := $(LLVM_CONFIG) is an LLVM for $(LLVM_HOST), not for a $(platform) core
   else ifneq ($(shell [ $(LLVM_MAJOR) -ge $(LLVM_MIN) ] && echo yes),yes)
      LLVM_NO := $(LLVM_CONFIG) is LLVM $(LLVM_MAJOR); $(LLVM_MIN) or newer is needed
   endif
   ifneq ($(LLVM_NO),)
      $(error SYSTEM_LLVM=1, but $(LLVM_NO))
   endif
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
   LLVM_LIBS := $(shell $(LLVM_CONFIG) $(LLVM_LINK) --ldflags --libs $(LLVM_COMPONENTS) --system-libs)
   # The shared one on Windows is an import library, as vulkan-1 is
   ifneq (,$(findstring win,$(platform)))
      ifeq ($(LLVM_LINK),--link-shared)
         LLVM_LIBS := -Wl,-Bdynamic $(LLVM_LIBS) -Wl,-Bstatic
      endif
   endif
   LIBS      := $(LLVM_LIBS) $(LIBS)
endif


ifeq ($(HAVE_LLVM)$(SYSTEM_LLVM),10)
   LLVM_SRC := $(CORE_DIR)/3rdparty/llvm/llvm/llvm
   # The sources are in the tree (3rdparty/llvm/llvm): the part of LLVM the
   # core is built from, and the core's own to change. Nothing is fetched.
   ifeq ($(wildcard $(LLVM_SRC)/include/llvm/IR/Module.h),)
      $(error 3rdparty/llvm/llvm is incomplete: it is part of this repository, check the checkout - or build with HAVE_LLVM=0)
   endif

   # The system and processor LLVM is built for, and those of the machine
   # that runs the build, for TableGen
   LLVM_OS   := $(if $(findstring win,$(platform)),windows,$(if $(filter osx,$(platform)),darwin,linux))
   LLVM_ARCH := $(if $(filter 1,$(IS_ARM64)),aarch64,x86)
   HOST_UNAME := $(shell uname -s 2>/dev/null)
   HOST_OS   := $(if $(filter MINGW% MSYS% CYGWIN%,$(HOST_UNAME)),windows,$(if $(filter Darwin,$(HOST_UNAME)),darwin,linux))
   HOST_ARCH := $(if $(filter aarch64 arm64,$(shell uname -m 2>/dev/null)),aarch64,x86)
   HOST_EXE  := $(if $(filter windows,$(HOST_OS)),.exe)
   HOST_CXX  ?= $(if $(filter $(LLVM_OS)-$(LLVM_ARCH),$(HOST_OS)-$(HOST_ARCH)),$(CXX),c++)

   LLVM_OUT      := $(GEN_DIR)/llvm
   LLVM_GEN      := $(LLVM_OUT)/gen
   LLVM_OBJ      := $(LLVM_OUT)/$(LLVM_OS)-$(LLVM_ARCH)
   LLVM_HOST_OBJ := $(LLVM_OUT)/host-$(HOST_OS)-$(HOST_ARCH)
   LLVM_MIN_TBLGEN := $(LLVM_HOST_OBJ)/llvm-min-tblgen$(HOST_EXE)
   LLVM_TBLGEN     := $(LLVM_HOST_OBJ)/llvm-tblgen$(HOST_EXE)
   LLVM_CFG        := $(CORE_DIR)/rpcs3/libretro/make/llvm/include
   LLVM_CONFIG_INC      = $(LLVM_CFG)/$(LLVM_OS) -I$(LLVM_CFG)/$(LLVM_ARCH) -I$(LLVM_CFG)/common
   LLVM_HOST_CONFIG_INC = $(LLVM_CFG)/$(HOST_OS) -I$(LLVM_CFG)/$(HOST_ARCH) -I$(LLVM_CFG)/common

   llvm_objs = $(patsubst $(LLVM_SRC)/%,$(1)/%.o,$(2))
   include $(CORE_DIR)/rpcs3/libretro/make/llvm/llvm.mk

   # The core's components (3rdparty/llvm/CMakeLists.txt): the common ones,
   # the target, and the perf listener on Linux
   LLVM_SET  := $(if $(filter 1,$(IS_ARM64)),AARCH64,X86)
   LLVM_SRCS := $(LLVM_LIB_COMMON_SRCS) $(LLVM_LIB_$(LLVM_SET)_SRCS) \
      $(if $(filter linux,$(LLVM_OS)),$(LLVM_LIB_PERFJITEVENTS_SRCS))
   LLVM_OBJS := $(call llvm_objs,$(LLVM_OBJ),$(LLVM_SRCS))
   LLVM_TBLGEN_OUT := $(LLVM_TBLGEN_COMMON) $(LLVM_TBLGEN_$(LLVM_SET))
   LLVM_MIN_OBJS := $(call llvm_objs,$(LLVM_HOST_OBJ),$(LLVM_LLVM_MIN_TBLGEN_SRCS))
   LLVM_TBLGEN_OBJS := $(call llvm_objs,$(LLVM_HOST_OBJ),$(LLVM_LLVM_TBLGEN_SRCS))
   LLVM_ARCHIVE := $(CORE_DIR)/libllvm.a

   # As LLVM's release builds compile it
   llvm_defs = $(if $(filter linux,$(1)),-D_GNU_SOURCE,$(if $(filter windows,$(1)),-D_FILE_OFFSET_BITS=64))
   LLVM_CXXFLAGS := -std=c++17 -O2 -DNDEBUG -fno-exceptions -fno-rtti -funwind-tables \
      -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -w -pthread $(fpic) $(VISIBILITY) $(ARCH_FLAGS)
   LLVM_CFLAGS   := -O2 -DNDEBUG -ffunction-sections -fdata-sections -w $(fpic) $(VISIBILITY) $(ARCH_FLAGS)
   LLVM_HOST_CXXFLAGS := -std=c++17 -O2 -DNDEBUG -fno-exceptions -fno-rtti -w -pthread
   LLVM_HOST_LIBS := $(if $(filter windows,$(HOST_OS)),-lpsapi -lshell32 -lole32 -luuid -ladvapi32 -lws2_32 -lntdll,$(if $(filter darwin,$(HOST_OS)),,-lpthread -ldl -lm))

   $(LLVM_MIN_OBJS) $(LLVM_TBLGEN_OBJS): LLVM_CONFIG_INC = $(LLVM_HOST_CONFIG_INC)
   # llvm-tblgen's own sources include what llvm-min-tblgen writes
   $(filter-out $(LLVM_MIN_OBJS),$(LLVM_TBLGEN_OBJS)): | $(LLVM_TBLGEN_TOOL)

   DEFINES   += -DLLVM_AVAILABLE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS
   LLVM_INC  := -isystem $(LLVM_SRC)/include -isystem $(LLVM_GEN)/include \
      -isystem $(LLVM_CFG)/$(LLVM_OS) -isystem $(LLVM_CFG)/$(LLVM_ARCH) -isystem $(LLVM_CFG)/common
   LIBS      := $(if $(filter windows,$(LLVM_OS)),-lpsapi -lshell32 -lole32 -luuid -ladvapi32 -lws2_32 -lntdll) $(LIBS)
endif

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
	$(DEFINES) $(INCLUDES_RPCS3) $(LLVM_INC) $(WARNINGS_RPCS3)
FLAGS_LIBRETRO := $(FLAGS_RPCS3) $(INCLUDES_LIBRETRO) -fexceptions -Wno-error=old-style-cast \
	-DRPCS3_LIBRETRO_VERSION='"$(LIBRETRO_VERSION)"'

# The C files among RPCS3's sources (C89 modules) with C flags, not C++'s
FLAGS_RPCS3_C := -std=gnu99 $(ARCH_FLAGS) $(DEFINES) $(INCLUDES_RPCS3) -Wall

# Each library's objects with its own flags
objs = $(patsubst %.mm,%.o,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(1))))

OBJ_LRC      := $(call objs,$(SOURCES_LRC))
OBJ_LIBUSB   := $(call objs,$(SOURCES_LIBUSB) $(OS_LIBUSB))
OBJ_ASMJIT   := $(call objs,$(SOURCES_ASMJIT))
OBJ_RTMIDI   := $(call objs,$(SOURCES_RTMIDI))
OBJ_FUSION   := $(call objs,$(SOURCES_FUSION))
OBJ_VOLK     := $(call objs,$(SOURCES_VOLK))
OBJ_LIBRETRO := $(call objs,$(SOURCES_LIBRETRO) $(SOURCES_GENERATED) $(OS_LIBRETRO))
OBJ_RPCS3    := $(call objs,$(SOURCES_RPCS3) $(OS_RPCS3))

OBJECTS := $(OBJ_LRC) $(OBJ_LIBUSB) $(OBJ_ASMJIT) $(OBJ_RTMIDI) $(OBJ_FUSION) \
	$(OBJ_VOLK) $(OBJ_RPCS3) $(OBJ_LIBRETRO)

$(OBJ_LRC):      FLAGS := $(FLAGS_LRC)
# What atomic_t's wait and notify park through: built as threaded code, which
# the rest of libretro-common here is not asked to be
$(CORE_DIR)/rpcs3/libretro/libretro-common/rthreads/retro_waitaddr.o: FLAGS := $(FLAGS_LRC) -DHAVE_THREADS
$(OBJ_LIBUSB):   FLAGS := $(FLAGS_LIBUSB)
$(OBJ_ASMJIT):   FLAGS := $(FLAGS_ASMJIT)
$(OBJ_RTMIDI):   FLAGS := $(FLAGS_RTMIDI)
$(OBJ_FUSION):   FLAGS := $(FLAGS_FUSION)
$(OBJ_VOLK):     FLAGS := $(FLAGS_VOLK)
$(OBJ_RPCS3):    FLAGS := $(FLAGS_RPCS3)
$(call objs,$(filter %.c,$(SOURCES_RPCS3) $(OS_RPCS3))): FLAGS := $(FLAGS_RPCS3_C)
$(OBJ_LIBRETRO): FLAGS := $(FLAGS_LIBRETRO)

# The core's 4 KiB stack frame, held to by the core's own code and the RPCS3
# code reworked for it: a frame past it does not build
STACK_CHECKED := $(filter-out %/libretro_ui_icons.o %/libretro_config_database.o %/libretro_patches.o %/libretro_vblank_unlock.o,$(OBJ_LIBRETRO)) \
	$(addprefix $(CORE_DIR)/rpcs3/,Emu/Cell/Modules/cellRec.o util/media_utils.o util/video_provider.o Emu/RSX/VK/VKLibretro.o Emu/Io/MouseHandler.o)
$(STACK_CHECKED): FLAGS += -Wframe-larger-than=4096 -Werror=frame-larger-than=4096
$(call objs,$(filter %.c,$(SOURCES_LIBRETRO) $(OS_LIBRETRO))): FLAGS := $(FLAGS_RPCS3_C)

# Built without RTTI, as LLVM is (rpcs3/Emu/CMakeLists.txt)
$(addprefix $(CORE_DIR)/,rpcs3/Emu/Cell/PPUTranslator.o Utilities/JITLLVM.o Utilities/JITASM.o): FLAGS += -fno-rtti

# rpcs3_version.cpp reads git-version.h
$(CORE_DIR)/rpcs3/rpcs3_version.o: $(CORE_DIR)/rpcs3/git-version.h

# Where the build writes objects: the folders of its sources, on every
# platform, and nothing else. A search of the whole tree would also find
# the *.o and *.d files that are data in other projects' trees, such as
# the fake toolchains in LLVM's clang tests, and delete them.
OBJ_DIRS := $(sort $(dir \
	$(SOURCES_LRC) $(SOURCES_ASMJIT) $(SOURCES_LIBUSB) $(SOURCES_RTMIDI) \
	$(SOURCES_FUSION) $(SOURCES_VOLK) $(SOURCES_RPCS3) $(SOURCES_RPCS3_GL) $(SOURCES_LIBRETRO) \
	$(SOURCES_LIBUSB_LINUX) \
	$(SOURCES_LIBUSB_DARWIN) $(SOURCES_LIBUSB_WINDOWS) $(SOURCES_RPCS3_AARCH64) \
	$(SOURCES_RPCS3_DARWIN) $(SOURCES_LIBRETRO_GL) $(SOURCES_LIBRETRO_NOGL) \
	$(SOURCES_GENERATED) $(CORE_DIR)/rpcs3/libretro/tests/x.cpp))
# Those that are there: a generated one goes with `make clean`
REMOVE_OBJECTS = $(if $(wildcard $(OBJ_DIRS)),find $(wildcard $(OBJ_DIRS)) -maxdepth 1 -type f \( -name '*.o' -o -name '*.d' \) -delete,true)

# Objects are written next to their sources, and their names do not say
# which toolchain made them: a build for another platform or processor in
# the same checkout clears the last one's first, at parse time, before make
# reads their dependency files. Not for `make clean`, which removes them all.
# Nor for `make llvm`, which the CMake build runs with its own compilers and
# its own place for LLVM's objects: it must not clear this build's.
BUILD_TAG := $(CC)|$(CXX)|$(platform)|$(arch)|$(HAVE_LLVM)|$(SYSTEM_LLVM)|$(if $(filter 1,$(SYSTEM_LLVM)),$(shell $(LLVM_CONFIG) --version))|$(DEBUG)|$(LTO)
ifeq ($(filter clean llvm,$(MAKECMDGOALS)),)
BUILD_TAG_STATUS := $(shell \
	if [ ! -f .build-tag ] || [ "`cat .build-tag 2>/dev/null`" != "$(BUILD_TAG)" ]; then \
		if [ -f .build-tag ]; then \
			$(REMOVE_OBJECTS); \
			echo cleaned; \
		fi; \
		echo "$(BUILD_TAG)" > .build-tag; \
	fi)
endif
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
	$(CORE_DIR)/libasmjit.a \
	$(CORE_DIR)/libusb.a \
	$(CORE_DIR)/librtmidi.a \
	$(CORE_DIR)/libfusion.a \
	$(CORE_DIR)/libvolk.a \
	$(LLVM_ARCHIVE)

$(CORE_DIR)/librpcs3_emu.a: $(OBJ_RPCS3)
$(CORE_DIR)/liblibretro_common.a: $(OBJ_LRC)
$(CORE_DIR)/libasmjit.a: $(OBJ_ASMJIT)
$(CORE_DIR)/libusb.a: $(OBJ_LIBUSB)
$(CORE_DIR)/librtmidi.a: $(OBJ_RTMIDI)
$(CORE_DIR)/libfusion.a: $(OBJ_FUSION)
$(CORE_DIR)/libvolk.a: $(OBJ_VOLK)

$(filter-out $(LLVM_ARCHIVE),$(ARCHIVES)):
	$(SAY) AR $@
	$(Q)rm -f $@
	$(Q)$(AR) rcs $@ $^

ifneq ($(LLVM_ARCHIVE),)
# ---- LLVM, from 3rdparty/llvm/llvm ----

# LLVM alone: its archive and the headers TableGen writes. For the CMake
# build, which has this Makefile build LLVM rather than describe it a second
# time (3rdparty/llvm/CMakeLists.txt): LLVM_OUT and LLVM_ARCHIVE say where.
llvm: $(LLVM_ARCHIVE) $(LLVM_TBLGEN_OUT)


# Its objects through a list, too many for a command line on Windows
$(LLVM_ARCHIVE): $(LLVM_OBJS)
	$(SAY) AR $@
	$(Q)rm -f $@
	$(file >$@.list,$^)
	$(Q)$(AR) rcs $@ @$@.list
	$(Q)rm -f $@.list

$(LLVM_OBJ)/%.cpp.o: $(LLVM_SRC)/%.cpp | $(LLVM_TBLGEN_OUT)
	@mkdir -p $(@D)
	$(SAY) CXX $<
	$(Q)$(CXX) -MMD -MP -c -o $@ $< $(LLVM_CXXFLAGS) $(call llvm_defs,$(LLVM_OS)) $(LLVM_CPPFLAGS) $(CPPFLAGS)

$(LLVM_OBJ)/%.c.o: $(LLVM_SRC)/%.c | $(LLVM_TBLGEN_OUT)
	@mkdir -p $(@D)
	$(SAY) CC $<
	$(Q)$(CC) -MMD -MP -c -o $@ $< $(LLVM_CFLAGS) $(call llvm_defs,$(LLVM_OS)) $(LLVM_CPPFLAGS) $(CPPFLAGS)

# TableGen, for the machine that runs the build
$(LLVM_HOST_OBJ)/%.cpp.o: $(LLVM_SRC)/%.cpp
	@mkdir -p $(@D)
	$(SAY) HOSTCXX $<
	$(Q)$(HOST_CXX) -MMD -MP -c -o $@ $< $(LLVM_HOST_CXXFLAGS) $(call llvm_defs,$(HOST_OS)) $(LLVM_CPPFLAGS)

$(LLVM_HOST_OBJ)/%.c.o: $(LLVM_SRC)/%.c
	@mkdir -p $(@D)
	$(SAY) HOSTCC $<
	$(Q)$(HOST_CC) -MMD -MP -c -o $@ $< -O2 -DNDEBUG -w $(call llvm_defs,$(HOST_OS)) $(LLVM_CPPFLAGS)

$(LLVM_MIN_TBLGEN): $(LLVM_MIN_OBJS)
	$(SAY) LINK $@
	$(Q)$(HOST_CXX) -o $@ $^ -pthread $(LLVM_HOST_LIBS)

$(LLVM_TBLGEN): $(LLVM_TBLGEN_OBJS)
	$(SAY) LINK $@
	$(Q)$(HOST_CXX) -o $@ $^ -pthread $(LLVM_HOST_LIBS)

# What each of LLVM's objects includes, written as it is compiled: a header
# of LLVM's that is changed rebuilds what includes it. (LLVM's sources are
# the tree's own to change; without this only an object's own source did.)
-include $(LLVM_OBJS:.o=.d) $(LLVM_MIN_OBJS:.o=.d) $(LLVM_TBLGEN_OBJS:.o=.d)

# The core's own sources include LLVM's generated headers
$(OBJ_RPCS3) $(OBJ_LIBRETRO): | $(LLVM_TBLGEN_OUT)
endif

ifneq ($(platform),osx)
   GROUP_START := -Wl,--start-group
   GROUP_END   := -Wl,--end-group
endif

$(TARGET): $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(fpic) $(SHARED) $(LIBS)

# The C rule first, and a C++ source that is gone is not an error: a source
# converted from C++ to C is still named in the .d file of a tree built
# before, and that tree would stop at "No rule to make target". Its object
# is rebuilt from the .c.
%.o: %.c
	$(SAY) CC $<
	$(Q)$(CC) -c -o $@ $< $(COMMON) $(FLAGS)

%.o: %.cpp
	$(SAY) CXX $<
	$(Q)$(CXX) -c -o $@ $< $(COMMON) $(FLAGS)

%.cpp: ;

# A dependency file is written by the compile, never made: without this,
# make's built-in "program from a .cpp" rule and the rule above together try
# to compile X.d.cpp for every .d file a fresh tree does not have yet.
%.d: ;

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

# embed rewrites a source only when what it holds changed, so the source can
# stay older than embed and its inputs; a stamp beside it records the run, and
# the source follows the stamp. Without it each of these ran on every build -
# the game patches fetched from rpcs3.net every time.
EMBED_STAMPS := $(addprefix $(GEN_DIR)/,libretro_ui_icons.stamp libretro_config_database.stamp libretro_vblank_unlock.stamp libretro_patches.stamp)

$(GEN_DIR)/%.cpp: $(GEN_DIR)/%.stamp
	@[ -f $@ ] || { rm -f $<; $(MAKE) $<; }

# Kept: made by the chain above, they are intermediates to make, which
# deleted them at the end of a first build - and the next build, whose .d
# files name them, made them again and fetched the patches again
.SECONDARY: $(EMBED_STAMPS:.stamp=.cpp)

$(GEN_DIR)/libretro_ui_icons.stamp: $(EMBED) $(addprefix $(ICON_DIR)/,$(ICON_NAMES))
	$(EMBED) icons $(GEN_DIR)/libretro_ui_icons.cpp $(ICON_DIR) $(ICON_NAMES)
	@touch $@

$(GEN_DIR)/libretro_config_database.stamp: $(EMBED) $(CORE_DIR)/rpcs3/libretro/config_database.dat
	$(EMBED) bytes $(GEN_DIR)/libretro_config_database.cpp g_libretro_config_database g_libretro_config_database_size config_database.dat $(CORE_DIR)/rpcs3/libretro/config_database.dat
	@touch $@

$(GEN_DIR)/libretro_vblank_unlock.stamp: $(EMBED) $(CORE_DIR)/rpcs3/libretro/vblank_unlock.txt
	$(EMBED) bytes $(GEN_DIR)/libretro_vblank_unlock.cpp g_libretro_vblank_unlock g_libretro_vblank_unlock_size vblank_unlock.txt $(CORE_DIR)/rpcs3/libretro/vblank_unlock.txt
	@touch $@

# RPCS3's game patches for this patch engine, fetched once as the CMake
# build fetches them at configure time (rpcs3/libretro/embed_patches.cmake):
# from rpcs3.net, else the GitHub mirror, else none. `make patches` fetches
# them again.
PATCH_ENGINE := $(shell sed -n 's/.*patch_engine_version = "\([0-9.]*\)".*/\1/p' $(CORE_DIR)/Utilities/bin_patch.h)

$(GEN_DIR)/libretro_patches.stamp: $(EMBED)
ifeq ($(EMBED_PATCHES),1)
	-curl -fsSL --max-time 60 -o $(GEN_DIR)/patch.json "https://rpcs3.net/compatibility?patch&api=v1&v=$(PATCH_ENGINE)" || rm -f $(GEN_DIR)/patch.json
	-[ -s $(GEN_DIR)/patch.json ] || curl -fsSL --max-time 60 -o $(GEN_DIR)/patch.yml "https://raw.githubusercontent.com/illusionyy/rpcs3-game-patch/main/patch.yml"
endif
	$(EMBED) patch $(GEN_DIR)/libretro_patches.cpp $(PATCH_ENGINE) $(GEN_DIR)/patch.json $(GEN_DIR)/patch.yml
	@rm -f $(GEN_DIR)/patch.json $(GEN_DIR)/patch.yml
	@touch $@

patches:
	rm -f $(GEN_DIR)/libretro_patches.cpp $(GEN_DIR)/libretro_patches.stamp
	$(MAKE) $(GEN_DIR)/libretro_patches.stamp

# git-version.h, as rpcs3/git-version.cmake and upstream_version.cmake
# write it: upstream's build and commit; rewritten only when it changes
GIT_VERSION_H = // This is a generated file.\n\n\#define RPCS3_GIT_VERSION "$(lastword $(subst -, ,$(UPSTREAM_VERSION)))-$(shell echo $(UPSTREAM_COMMIT) | cut -c1-8)"\n\#define RPCS3_GIT_BRANCH "$(CORE_BRANCH)"\n\#define RPCS3_GIT_FULL_BRANCH "$(if $(BUILD_REPOSITORY_NAME),$(BUILD_REPOSITORY_NAME)/$(BUILD_SOURCEBRANCHNAME),local_build)"\n\n// If you do not want this file to update/recompile, change to 1.\n\#define RPCS3_GIT_VERSION_NO_UPDATE 0\n

$(CORE_DIR)/rpcs3/git-version.h: FORCE
	@printf '$(GIT_VERSION_H)' > $@.tmp; \
	if grep -q 'RPCS3_GIT_VERSION_NO_UPDATE 1' $@ 2>/dev/null || cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

# The core, loaded with every symbol resolved and asked what it is; and
# in-memory save states, written and read through RPCS3's serializer by a
# program linked as the core is
SAVESTATE_TEST := $(GEN_DIR)/test_savestate_memory
OBJ_SAVESTATE_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_savestate_memory.o
$(OBJ_SAVESTATE_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(SAVESTATE_TEST): $(OBJ_SAVESTATE_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_SAVESTATE_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The decoders a build without LLVM gives a game in place of the LLVM
# recompilers, linked the same way
DECODERS_TEST := $(GEN_DIR)/test_decoders
OBJ_DECODERS_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_decoders.o
$(OBJ_DECODERS_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(DECODERS_TEST): $(OBJ_DECODERS_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_DECODERS_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# RPCS3's game patches the core carries (patch.yml), read by its patch
# engine as the Patch Manager reads them, linked the same way
PATCHES_TEST := $(GEN_DIR)/test_game_patches
OBJ_PATCHES_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_game_patches.o
$(OBJ_PATCHES_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(PATCHES_TEST): $(OBJ_PATCHES_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_PATCHES_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The game's audio to the frontend (libretro_audio.cpp), linked the same way
AUDIO_TEST := $(GEN_DIR)/test_audio_frontend
OBJ_AUDIO_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_audio_frontend.o
$(OBJ_AUDIO_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(AUDIO_TEST): $(OBJ_AUDIO_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_AUDIO_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The Vulkan frame handoff (Emu/RSX/VK/VKLibretro.cpp), linked the same way
VK_HANDOFF_TEST := $(GEN_DIR)/test_vk_handoff
OBJ_VK_HANDOFF_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_vk_handoff.o
$(OBJ_VK_HANDOFF_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(VK_HANDOFF_TEST): $(OBJ_VK_HANDOFF_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_VK_HANDOFF_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The pads' handover to the game's threads (Emu/Io/pad_types.h), linked the
# same way
PAD_HANDOFF_TEST := $(GEN_DIR)/test_pad_handoff
OBJ_PAD_HANDOFF_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_pad_handoff.o
$(OBJ_PAD_HANDOFF_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(PAD_HANDOFF_TEST): $(OBJ_PAD_HANDOFF_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_PAD_HANDOFF_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The light guns' handover to the PS3 mice (libretro_mouse_handler.cpp),
# linked the same way
MOUSE_HANDOFF_TEST := $(GEN_DIR)/test_mouse_handoff
OBJ_MOUSE_HANDOFF_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_mouse_handoff.o
$(OBJ_MOUSE_HANDOFF_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(MOUSE_HANDOFF_TEST): $(OBJ_MOUSE_HANDOFF_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_MOUSE_HANDOFF_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The OpenGL renderer's contexts from the frontend (libretro_video.cpp),
# linked the same way; only where the core has the GL renderer
GL_CONTEXTS_TEST := $(if $(filter %libretro_video.cpp,$(OS_LIBRETRO)),$(GEN_DIR)/test_gl_contexts)
OBJ_GL_CONTEXTS_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_gl_contexts.o
$(OBJ_GL_CONTEXTS_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(GEN_DIR)/test_gl_contexts: $(OBJ_GL_CONTEXTS_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_GL_CONTEXTS_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The guest's clock slaved to retro_run (Emu/Cell/lv2/sys_time.cpp), linked
# the same way
GUEST_CLOCK_TEST := $(GEN_DIR)/test_guest_clock
OBJ_GUEST_CLOCK_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_guest_clock.o
$(OBJ_GUEST_CLOCK_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(GUEST_CLOCK_TEST): $(OBJ_GUEST_CLOCK_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_GUEST_CLOCK_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# fs::file over the frontend's VFS from several threads (libretro_vfs.cpp),
# linked the same way
VFS_POSITIONAL_TEST := $(GEN_DIR)/test_vfs_positional
OBJ_VFS_POSITIONAL_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_vfs_positional.o
$(OBJ_VFS_POSITIONAL_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(VFS_POSITIONAL_TEST): $(OBJ_VFS_POSITIONAL_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_VFS_POSITIONAL_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# cellPad from several of the game's threads with no lock
# (Emu/Cell/Modules/cellPad.cpp), linked the same way
PAD_GUEST_TEST := $(GEN_DIR)/test_pad_guest
OBJ_PAD_GUEST_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_pad_guest.o
$(OBJ_PAD_GUEST_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(PAD_GUEST_TEST): $(OBJ_PAD_GUEST_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_PAD_GUEST_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The Vulkan context negotiation with the frontend (libretro_core.cpp),
# linked the same way; only where the core has the Vulkan renderer
VK_NEGOTIATION_TEST := $(if $(filter -DHAVE_VULKAN,$(FLAGS_LIBRETRO) $(DEFINES)),$(GEN_DIR)/test_vk_negotiation)
OBJ_VK_NEGOTIATION_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_vk_negotiation.o
$(OBJ_VK_NEGOTIATION_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(GEN_DIR)/test_vk_negotiation: $(OBJ_VK_NEGOTIATION_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_VK_NEGOTIATION_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# An emulated controller's buttons from the pad's
# (Emu/Io/emulated_pad_config.h), linked the same way
PAD_BUTTONS_TEST := $(GEN_DIR)/test_pad_buttons
OBJ_PAD_BUTTONS_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_pad_buttons.o
$(OBJ_PAD_BUTTONS_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(PAD_BUTTONS_TEST): $(OBJ_PAD_BUTTONS_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_PAD_BUTTONS_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# cellGem without its lock (Emu/Cell/Modules/cellGem.cpp, compiled into the
# test: its state is private to that file), linked the same way
GEM_SHARED_TEST := $(GEN_DIR)/test_gem_shared
OBJ_GEM_SHARED_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_gem_shared.o
$(OBJ_GEM_SHARED_TEST): FLAGS := $(FLAGS_LIBRETRO)
$(OBJ_GEM_SHARED_TEST): $(CORE_DIR)/rpcs3/Emu/Cell/Modules/cellGem.cpp

$(GEM_SHARED_TEST): $(OBJ_GEM_SHARED_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_GEM_SHARED_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# sys_io's event queue behind its gate (Emu/Cell/Modules/sys_io_.cpp, compiled
# into the test: its state is private to that file), linked the same way
SYS_IO_GATE_TEST := $(GEN_DIR)/test_sys_io_gate
OBJ_SYS_IO_GATE_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_sys_io_gate.o
$(OBJ_SYS_IO_GATE_TEST): FLAGS := $(FLAGS_LIBRETRO)
$(OBJ_SYS_IO_GATE_TEST): $(CORE_DIR)/rpcs3/Emu/Cell/Modules/sys_io_.cpp

$(SYS_IO_GATE_TEST): $(OBJ_SYS_IO_GATE_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_SYS_IO_GATE_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# An lv2 event queue's events without the heap (Emu/Cell/lv2/sys_event.h),
# linked the same way
LV2_EVENTS_TEST := $(GEN_DIR)/test_lv2_events
OBJ_LV2_EVENTS_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_lv2_events.o
$(OBJ_LV2_EVENTS_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(LV2_EVENTS_TEST): $(OBJ_LV2_EVENTS_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_LV2_EVENTS_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The plain-data queues on retro_deque (USB figures, P2P backlog, RSX's
# histories), linked the same way
PLAIN_QUEUES_TEST := $(GEN_DIR)/test_plain_queues
OBJ_PLAIN_QUEUES_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_plain_queues.o
$(OBJ_PLAIN_QUEUES_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(PLAIN_QUEUES_TEST): $(OBJ_PLAIN_QUEUES_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_PLAIN_QUEUES_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The timer thread's list and cellVoice's queues to notify on retro_deque;
# sys_timer.cpp is compiled into the test, the rest linked the same way
HELD_QUEUES_TEST := $(GEN_DIR)/test_held_queues
OBJ_HELD_QUEUES_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_held_queues.o
$(OBJ_HELD_QUEUES_TEST): FLAGS := $(FLAGS_LIBRETRO)
$(OBJ_HELD_QUEUES_TEST): $(CORE_DIR)/rpcs3/Emu/Cell/lv2/sys_timer.cpp

$(HELD_QUEUES_TEST): $(OBJ_HELD_QUEUES_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_HELD_QUEUES_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# cellVdec's frame queues on retro_deque; cellVdec.cpp is compiled into the
# test, the rest linked the same way
VDEC_FRAMES_TEST := $(GEN_DIR)/test_vdec_frames
OBJ_VDEC_FRAMES_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_vdec_frames.o
$(OBJ_VDEC_FRAMES_TEST): FLAGS := $(FLAGS_LIBRETRO)
$(OBJ_VDEC_FRAMES_TEST): $(CORE_DIR)/rpcs3/Emu/Cell/Modules/cellVdec.cpp

$(VDEC_FRAMES_TEST): $(OBJ_VDEC_FRAMES_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_VDEC_FRAMES_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# sys_rsxaudio's audio through the lock-free ring to the backend's
# callback, linked the same way
RSXAUDIO_RING_TEST := $(GEN_DIR)/test_rsxaudio_ring
OBJ_RSXAUDIO_RING_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_rsxaudio_ring.o $(CORE_DIR)/rpcs3/libretro/tests/reference_ringbuf.o
$(OBJ_RSXAUDIO_RING_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(RSXAUDIO_RING_TEST): $(OBJ_RSXAUDIO_RING_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_RSXAUDIO_RING_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The Vulkan renderer's shader sources gathered on a Vulkan driver (a
# software one will do); SPIRVCommon.cpp is compiled into the test, and
# shader_runner.c (C89) runs the modules
SHADER_CORPUS_TEST := $(GEN_DIR)/test_shader_corpus
OBJ_SHADER_CORPUS_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_shader_corpus.o
OBJ_SHADER_RUNNER := $(CORE_DIR)/rpcs3/libretro/tests/shader_runner.o
$(OBJ_SHADER_CORPUS_TEST): FLAGS := $(FLAGS_LIBRETRO)
$(OBJ_SHADER_CORPUS_TEST): $(CORE_DIR)/rpcs3/Emu/RSX/Program/SPIRVCommon.cpp $(CORE_DIR)/rpcs3/libretro/tests/shader_runner.h
$(OBJ_SHADER_RUNNER): FLAGS := -std=gnu99 -Wall -Wextra -Wdeclaration-after-statement -Werror -DVK_NO_PROTOTYPES \
	-isystem $(CORE_DIR)/3rdparty/Vulkan-Headers/include -isystem $(CORE_DIR)/3rdparty/volk
$(OBJ_SHADER_RUNNER): $(CORE_DIR)/rpcs3/libretro/tests/shader_runner.h

$(SHADER_CORPUS_TEST): $(OBJ_SHADER_CORPUS_TEST) $(OBJ_SHADER_RUNNER) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_SHADER_CORPUS_TEST) $(OBJ_SHADER_RUNNER) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# retro_run without a game (libretro_core.cpp), linked the same way
FAILED_BOOT_TEST := $(GEN_DIR)/test_failed_boot
OBJ_FAILED_BOOT_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_failed_boot.o
$(OBJ_FAILED_BOOT_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(FAILED_BOOT_TEST): $(OBJ_FAILED_BOOT_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_FAILED_BOOT_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# The emulator's waits on its state and on retro_run (Emu/System.h,
# libretro_frontend.cpp), linked the same way
EMU_WAITS_TEST := $(GEN_DIR)/test_emu_waits
OBJ_EMU_WAITS_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_emu_waits.o
$(OBJ_EMU_WAITS_TEST): FLAGS := $(FLAGS_LIBRETRO)

$(EMU_WAITS_TEST): $(OBJ_EMU_WAITS_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_EMU_WAITS_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

# LLVM, as the core is built with it, compiling and running code through
# RPCS3's JIT; only when the core has LLVM
LLVM_JIT_TEST := $(if $(filter 1,$(HAVE_LLVM)),$(GEN_DIR)/test_llvm_jit)
OBJ_LLVM_JIT_TEST := $(CORE_DIR)/rpcs3/libretro/tests/test_llvm_jit.o
$(OBJ_LLVM_JIT_TEST): FLAGS := $(FLAGS_LIBRETRO)
$(OBJ_LLVM_JIT_TEST): | $(LLVM_TBLGEN_OUT)

$(GEN_DIR)/test_llvm_jit: $(OBJ_LLVM_JIT_TEST) $(OBJ_LIBRETRO) $(ARCHIVES)
	$(SAY) LINK $@
	$(Q)$(CXX) -o $@ $(OBJ_LLVM_JIT_TEST) $(OBJ_LIBRETRO) $(GROUP_START) $(ARCHIVES) $(GROUP_END) $(LDFLAGS) $(LIBS)

check: $(TARGET) $(SAVESTATE_TEST) $(DECODERS_TEST) $(PATCHES_TEST) $(AUDIO_TEST) $(VK_HANDOFF_TEST) $(VK_NEGOTIATION_TEST) $(PAD_HANDOFF_TEST) $(PAD_GUEST_TEST) $(PAD_BUTTONS_TEST) $(GEM_SHARED_TEST) $(MOUSE_HANDOFF_TEST) $(SYS_IO_GATE_TEST) $(LV2_EVENTS_TEST) $(PLAIN_QUEUES_TEST) $(HELD_QUEUES_TEST) $(VDEC_FRAMES_TEST) $(RSXAUDIO_RING_TEST) $(SHADER_CORPUS_TEST) $(EMU_WAITS_TEST) $(FAILED_BOOT_TEST) $(GUEST_CLOCK_TEST) $(VFS_POSITIONAL_TEST) $(GL_CONTEXTS_TEST) $(LLVM_JIT_TEST)
	@$(MAKE) -q $(EMBED_STAMPS) || { echo "check: the embedded sources are made again on every build"; exit 1; }
	@for f in $(EMBED_STAMPS:.stamp=.cpp); do [ -f $$f ] || { echo "check: $$f was deleted after the build"; exit 1; }; done
	$(HOST_CC) -O2 -o $(GEN_DIR)/load_test $(CORE_DIR)/rpcs3/libretro/make/load_test.c -ldl
	$(GEN_DIR)/load_test ./$(TARGET)
	$(SAVESTATE_TEST)
	$(DECODERS_TEST)
	$(PATCHES_TEST)
	$(AUDIO_TEST)
	$(VK_HANDOFF_TEST)
	$(if $(VK_NEGOTIATION_TEST),$(VK_NEGOTIATION_TEST) && $(VK_NEGOTIATION_TEST) 1)
	$(PAD_HANDOFF_TEST)
	$(PAD_GUEST_TEST)
	$(PAD_BUTTONS_TEST)
	$(GEM_SHARED_TEST)
	$(MOUSE_HANDOFF_TEST)
	$(SYS_IO_GATE_TEST)
	$(LV2_EVENTS_TEST)
	$(PLAIN_QUEUES_TEST)
	$(HELD_QUEUES_TEST)
	$(VDEC_FRAMES_TEST)
	$(RSXAUDIO_RING_TEST)
	$(SHADER_CORPUS_TEST)
	$(EMU_WAITS_TEST)
	$(FAILED_BOOT_TEST)
	$(GUEST_CLOCK_TEST)
	$(VFS_POSITIONAL_TEST)
	$(if $(GL_CONTEXTS_TEST),$(GL_CONTEXTS_TEST))
	$(if $(LLVM_JIT_TEST),$(LLVM_JIT_TEST))

-include $(OBJECTS:.o=.d) $(OBJ_SAVESTATE_TEST:.o=.d) $(OBJ_DECODERS_TEST:.o=.d) $(OBJ_PATCHES_TEST:.o=.d) $(OBJ_AUDIO_TEST:.o=.d) $(OBJ_VK_HANDOFF_TEST:.o=.d) $(OBJ_VK_NEGOTIATION_TEST:.o=.d) $(OBJ_PAD_HANDOFF_TEST:.o=.d) $(OBJ_PAD_GUEST_TEST:.o=.d) $(OBJ_PAD_BUTTONS_TEST:.o=.d) $(OBJ_GEM_SHARED_TEST:.o=.d) $(OBJ_MOUSE_HANDOFF_TEST:.o=.d) $(OBJ_SYS_IO_GATE_TEST:.o=.d) $(OBJ_LV2_EVENTS_TEST:.o=.d) $(OBJ_PLAIN_QUEUES_TEST:.o=.d) $(OBJ_HELD_QUEUES_TEST:.o=.d) $(OBJ_VDEC_FRAMES_TEST:.o=.d) $(OBJ_RSXAUDIO_RING_TEST:.o=.d) $(OBJ_SHADER_CORPUS_TEST:.o=.d) $(OBJ_EMU_WAITS_TEST:.o=.d) $(OBJ_FAILED_BOOT_TEST:.o=.d) $(OBJ_GUEST_CLOCK_TEST:.o=.d) $(OBJ_VFS_POSITIONAL_TEST:.o=.d) $(OBJ_GL_CONTEXTS_TEST:.o=.d) $(OBJ_LLVM_JIT_TEST:.o=.d)

clean:
	@$(REMOVE_OBJECTS)
	rm -rf $(TARGET) $(ARCHIVES) $(GEN_DIR) .build-tag

FORCE:

.PHONY: all check clean patches llvm FORCE
