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
# as libretro_common.cmake builds it: net_http over its own TLS client
LRC_DEFS := -DHAVE_SSL -DHAVE_RETROSSL -DHAVE_CRYPTO

TESTS := test_zstd test_deflate test_rxml test_png_rows test_images test_http test_bcn test_tls_duplex test_yaml test_font

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

# stb is the oracle for rpng, rjpeg and rgif (stb_image, whose writer
# also makes the PNGs and JPEGs) and for rtt (stb_truetype): the commit
# RPCS3 pinned (3rdparty/stblib/stb, gone)
STB        := stb-013ac3b
STB_COMMIT := 013ac3beddff3dbffafd5177e7972067cd2b5083

$(STB)/stb_image.h:
	git clone -q https://github.com/nothings/stb.git $(STB)
	git -C $(STB) checkout -q $(STB_COMMIT)

test_images_LRC := \
	$(LRC)/formats/png/rpng.c \
	$(LRC)/formats/jpeg/rjpeg.c \
	$(LRC)/formats/gif/rgif.c \
	$(LRC)/streams/trans_stream.c \
	$(LRC)/streams/trans_stream_deflate.c \
	$(LRC)/streams/trans_stream_pipe.c \
	$(test_deflate_LRC)

# bcdec is the oracle for rdds's block decoders: the commit RPCS3's copy
# (3rdparty/bcdec/bcdec.hpp, gone) was based on
BCDEC        := bcdec-963c5e5
BCDEC_COMMIT := 963c5e56b7a335e066cff7d16a3de75f4e8ad366

$(BCDEC)/bcdec.h:
	git clone -q https://github.com/iOrange/bcdec.git $(BCDEC)
	git -C $(BCDEC) checkout -q $(BCDEC_COMMIT)

test_bcn_LRC := $(LRC)/formats/dds/rdds.c

# ryaml and ryaml_writer; rinflate for the gzipped fixtures
test_yaml_LRC := \
	$(LRC)/formats/yaml/ryaml.c \
	$(LRC)/formats/yaml/ryaml_writer.c \
	$(test_deflate_LRC)

# net_http and the TLS client, with what they need
test_http_LRC := \
	$(LRC)/net/net_http.c \
	$(LRC)/net/net_socket.c \
	$(LRC)/net/net_compat.c \
	$(LRC)/net/net_socket_ssl_retro.c \
	$(LRC)/crypto/crypto.c \
	$(LRC)/crypto/kdf.c \
	$(LRC)/crypto/pk.c \
	$(LRC)/crypto/x509.c \
	$(LRC)/hash/lrc_hash.c \
	$(LRC)/utils/md5.c \
	$(LRC)/encodings/encoding_base64.c \
	$(LRC)/encodings/encoding_utf.c \
	$(LRC)/streams/file_stream.c \
	$(LRC)/vfs/vfs_implementation.c \
	$(LRC)/file/file_path.c \
	$(LRC)/file/file_path_io.c \
	$(LRC)/file/retro_dirent.c \
	$(LRC)/string/rstrtod.c \
	$(LRC)/string/stdstring.c \
	$(LRC)/lists/string_list.c \
	$(LRC)/compat/fopen_utf8.c \
	$(LRC)/compat/compat_strl.c \
	$(LRC)/compat/compat_strcasestr.c \
	$(LRC)/compat/compat_posix_string.c \
	$(LRC)/features/features_cpu.c \
	$(LRC)/time/rtime.c

all: $(TESTS)

obj/%.o: $(LRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LRC_STD) $(LRC_DEFS) $(SANITIZE) -c -o $@ $<

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

obj/test_images.o: test_images.c $(STB)/stb_image.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LRC_STD) -I$(STB) $(SANITIZE) -c -o $@ $<

obj/stb_impl.o: stb_impl.c $(STB)/stb_image.h
	@mkdir -p $(@D)
	$(CC) -O2 -w $(LRC_STD) -I$(STB) -c -o $@ $<

test_images: obj/test_images.o obj/stb_impl.o $(call lrc_objs,$(test_images_LRC))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

obj/test_http.o: test_http.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LRC_STD) $(SANITIZE) -c -o $@ $<

test_http: obj/test_http.o $(call lrc_objs,$(sort $(test_http_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS) -lpthread

obj/bcdec_impl.o: bcdec_impl.c $(BCDEC)/bcdec.h
	@mkdir -p $(@D)
	$(CC) -O2 -w $(LRC_STD) -I$(BCDEC) -c -o $@ $<

obj/test_tls_duplex.o: test_tls_duplex.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) -D_GNU_SOURCE $(SANITIZE) -c -o $@ $<

test_tls_duplex: obj/test_tls_duplex.o $(call lrc_objs,$(sort $(test_http_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS) -lpthread

test_yaml: obj/test_yaml.o $(call lrc_objs,$(test_yaml_LRC))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# the overlays' packer is the core's own header, built here as C (gnu99,
# as stb_truetype is not C89)
test_font_LRC := \
	$(LRC)/gfx/font/rtt.c \
	$(test_deflate_LRC)

obj/test_font.o: test_font.c ../../Emu/RSX/Overlays/overlay_font_pack.h $(STB)/stb_image.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LRC_STD) -Wdeclaration-after-statement -isystem $(STB) $(SANITIZE) -c -o $@ $<

test_font: obj/test_font.o obj/stb_impl.o $(call lrc_objs,$(sort $(test_font_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_bcn: obj/test_bcn.o obj/bcdec_impl.o $(call lrc_objs,$(test_bcn_LRC))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# Each test also writes what the new code produces, for the reference
# tools to read back
check: all
	./test_zstd fixtures zstd_out.zst
	if command -v zstd >/dev/null; then zstd -q -t zstd_out.zst; fi
	./test_deflate fixtures deflate_out.gz
	gzip -t deflate_out.gz
	./test_rxml
	./test_png_rows
	./test_images
	./test_http
	./test_bcn
	./tls_duplex_test.sh ./test_tls_duplex
	./test_yaml fixtures
	./test_font fixtures

clean:
	rm -rf obj $(TESTS) *_out.*

.PHONY: all check clean
