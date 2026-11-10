# Tests of the libretro-common pieces that replaced third-party libraries,
# against data the old libraries wrote. Run by the "Library replacement
# tests" workflow (.github/workflows/library-tests.yml):
#   make -C rpcs3/libretro/tests check

CC       ?= cc
LRC      := ../libretro-common
CFLAGS   ?= -O2
CFLAGS   += -Wall -I$(LRC)/include
SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer
LDLIBS   += -lm

# The tests are C89; libretro-common builds as RetroArch's samples build it
TEST_STD := -std=c89 -Wdeclaration-after-statement
LRC_STD  := -std=gnu99

TESTS := test_zstd test_deflate test_rxml test_png_rows

test_zstd_LRC := \
	$(LRC)/encodings/encoding_rzstd.c \
	$(LRC)/encodings/encoding_xxh64.c

test_deflate_LRC := \
	$(LRC)/encodings/encoding_deflate.c \
	$(LRC)/encodings/encoding_crc32.c \
	$(LRC)/features/features_cpu.c \
	$(LRC)/compat/compat_strl.c \
	$(LRC)/time/rtime.c

test_rxml_LRC := \
	$(LRC)/formats/xml/rxml.c

# libpng is the oracle here, never linked by the core: 1.6.59, the
# version RPCS3 built with, fetched and built from source, as distros
# ship older ones whose colour handling differs
test_png_rows_LRC := \
	$(LRC)/formats/png/rpng_rows.c \
	$(test_deflate_LRC)
LIBPNG     := libpng-1.6.59
LIBPNG_SRC := png pngerror pngget pngmem pngpread pngread pngrio pngrtran \
	pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil
LIBPNG_OBJ := $(patsubst %,obj/libpng/%.o,$(LIBPNG_SRC))
# plain C, no hardware filter code: the same arithmetic on every host
LIBPNG_DEF := -DPNG_ARM_NEON_OPT=0 -DPNG_MIPS_MSA_OPT=0 -DPNG_MIPS_MMI_OPT=0 \
	-DPNG_POWERPC_VSX_OPT=0 -DPNG_INTEL_SSE_OPT=0 -DPNG_LOONGARCH_LSX_OPT=0 \
	-DPNG_RISCV_RVV_OPT=0
PNG_CFLAGS := -I$(LIBPNG)

$(LIBPNG)/png.c:
	git clone -q --depth 1 -b v1.6.59 https://github.com/pnggroup/libpng.git $(LIBPNG)

$(LIBPNG)/pnglibconf.h: $(LIBPNG)/png.c
	cp $(LIBPNG)/scripts/pnglibconf.h.prebuilt $@

obj/libpng/%.o: $(LIBPNG)/pnglibconf.h
	@mkdir -p $(@D)
	$(CC) -O2 -w $(LIBPNG_DEF) -I$(LIBPNG) -c -o $@ $(LIBPNG)/$*.c

all: $(TESTS)

obj/%.o: $(LRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LRC_STD) $(SANITIZE) -c -o $@ $<

obj/%.o: %.c test_common.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

lrc_objs = $(patsubst $(LRC)/%.c,obj/%.o,$(1))

test_zstd: obj/test_zstd.o $(call lrc_objs,$(test_zstd_LRC))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_deflate: obj/test_deflate.o $(call lrc_objs,$(test_deflate_LRC))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_rxml: obj/test_rxml.o $(call lrc_objs,$(test_rxml_LRC))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

obj/cellPngDec_rows.o: ../../Emu/Cell/Modules/cellPngDec_rows.c ../../Emu/Cell/Modules/cellPngDec_rows.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_png_rows.o: test_png_rows.c test_common.h ../../Emu/Cell/Modules/cellPngDec_rows.h $(LIBPNG)/pnglibconf.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LRC_STD) $(PNG_CFLAGS) $(SANITIZE) -c -o $@ $<

test_png_rows: obj/test_png_rows.o obj/cellPngDec_rows.o $(call lrc_objs,$(test_png_rows_LRC)) $(LIBPNG_OBJ)
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) -lz $(LDLIBS)

# Each test also writes what the new code produces, for the reference
# tools to read back
check: all
	./test_zstd fixtures zstd_out.zst
	if command -v zstd >/dev/null; then zstd -q -t zstd_out.zst; fi
	./test_deflate fixtures deflate_out.gz
	gzip -t deflate_out.gz
	./test_rxml
	./test_png_rows

clean:
	rm -rf obj $(TESTS) *_out.*

.PHONY: all check clean
