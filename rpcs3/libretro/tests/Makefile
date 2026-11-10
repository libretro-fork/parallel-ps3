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

TESTS := test_zstd test_deflate test_rxml

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

# Each test also writes what the new code produces, for the reference
# tools to read back
check: all
	./test_zstd fixtures zstd_out.zst
	if command -v zstd >/dev/null; then zstd -q -t zstd_out.zst; fi
	./test_deflate fixtures deflate_out.gz
	gzip -t deflate_out.gz
	./test_rxml

clean:
	rm -rf obj $(TESTS) *_out.*

.PHONY: all check clean
