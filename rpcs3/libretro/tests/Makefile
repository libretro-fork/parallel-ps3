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
LRC_DEFS := -DHAVE_SSL -DHAVE_RETROSSL -DHAVE_CRYPTO \
	-DHAVE_RWAV -DHAVE_RFLAC -DHAVE_RVORBIS -DHAVE_ROPUS -DHAVE_RMP3 -DHAVE_RAAC \
	-DHAVE_RMP4 -DHAVE_RAC3 -DHAVE_RWMA -DHAVE_RWEBM -DHAVE_RATRAC3 -DHAVE_RATRAC3P

TESTS := test_zstd test_deflate test_rxml test_png_rows test_images test_jpg_rows test_http test_bcn test_tls_duplex test_yaml test_font test_np2 test_rsx_scale test_vpost test_atracx test_vdec test_media test_record test_config_database test_vblank_list test_video_sink test_video_sink_tsan test_vk_images test_vk_images_tsan test_sw_frames test_sw_frames_tsan test_gl_frames test_gl_fallback test_pad_values test_input_poll test_audio_handover test_audio_ring test_audio_ring_tsan test_audio_mix

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

# cellJpgDec's decode; its allocations counted by wrapping malloc
test_jpg_rows_LRC := \
	$(LRC)/formats/jpeg/rjpeg.c \
	$(LRC)/features/features_cpu.c \
	$(LRC)/compat/compat_strl.c

obj/cellJpgDec_rows.o: ../../Emu/Cell/Modules/cellJpgDec_rows.c ../../Emu/Cell/Modules/cellJpgDec_rows.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_jpg_rows.o: test_jpg_rows.c ../../Emu/Cell/Modules/cellJpgDec_rows.h $(STB)/stb_image.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LRC_STD) -I$(STB) $(SANITIZE) -c -o $@ $<

test_jpg_rows: obj/test_jpg_rows.o obj/cellJpgDec_rows.o obj/stb_impl.o $(call lrc_objs,$(sort $(test_jpg_rows_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) -Wl,--wrap=malloc,--wrap=calloc $(LDLIBS)

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

# RPCN's messages: the code the core builds is made again from the .proto
# here, checked against the one in the tree, and read and written against
# what the protobuf library made of the fixtures
CXX     ?= c++
NP2     := ../../Emu/NP/generated
NP2_GEN := obj/np2/np2_structs.h obj/np2/np2_structs.cpp obj/np2/np2_dump.h

$(NP2_GEN): $(NP2)/np2_structs.proto $(NP2)/np2_structs_gen.py
	@mkdir -p obj/np2
	python3 $(NP2)/np2_structs_gen.py --out obj/np2 --dump obj/np2/np2_dump.h
	cmp obj/np2/np2_structs.h $(NP2)/np2_structs.h
	cmp obj/np2/np2_structs.cpp $(NP2)/np2_structs.cpp

obj/np2/%.o: obj/np2/%.cpp $(NP2_GEN) ../../Emu/NP/pb_wire.h
	$(CXX) -O2 -std=c++17 -Wall -Wextra -I../.. $(SANITIZE) -c -o $@ $<

obj/test_np2.o: test_np2.cpp $(NP2_GEN) ../../Emu/NP/pb_wire.h
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Iobj/np2 -I../.. -I$(LRC)/include $(SANITIZE) -c -o $@ $<

test_np2: obj/test_np2.o obj/np2/np2_structs.o $(call lrc_objs,$(test_deflate_LRC))
	$(CXX) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_bcn: obj/test_bcn.o obj/bcdec_impl.o $(call lrc_objs,$(test_bcn_LRC))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# the RSX blit engine's scaled copy, on libretro-common's scaler; its
# allocations counted by wrapping malloc
test_rsx_scale_LRC := \
	$(LRC)/gfx/scaler/pixconv.c \
	$(LRC)/gfx/scaler/scaler.c \
	$(LRC)/gfx/scaler/scaler_filter.c \
	$(LRC)/gfx/scaler/scaler_int.c \
	$(LRC)/features/features_cpu.c

obj/rsx_scale.o: ../../Emu/RSX/Utils/rsx_scale.c ../../Emu/RSX/Utils/rsx_scale.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_rsx_scale.o: test_rsx_scale.c test_common.h ../../Emu/RSX/Utils/rsx_scale.h

test_rsx_scale: obj/test_rsx_scale.o obj/rsx_scale.o $(call lrc_objs,$(sort $(test_rsx_scale_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc $(LDLIBS)

# cellVpost's YUV to RGBA, on image_yuv_blit and the scaler
test_vpost_LRC := \
	$(LRC)/formats/image/image_yuv_blit.c \
	$(test_rsx_scale_LRC)

obj/cellVpost_conv.o: ../../Emu/Cell/Modules/cellVpost_conv.c ../../Emu/Cell/Modules/cellVpost_conv.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_vpost.o: test_vpost.c test_common.h ../../Emu/Cell/Modules/cellVpost_conv.h

test_vpost: obj/test_vpost.o obj/cellVpost_conv.o $(call lrc_objs,$(sort $(test_vpost_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc $(LDLIBS)

# cellAtracXdec's decode: ratrac3p, and the module's output conversion
test_atracx_LRC := $(LRC)/formats/atrac3p/ratrac3p.c

obj/cellAtracXdec_pcm.o: ../../Emu/Cell/Modules/cellAtracXdec_pcm.c ../../Emu/Cell/Modules/cellAtracXdec_pcm.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_atracx.o: test_atracx.c test_common.h ../../Emu/Cell/Modules/cellAtracXdec_pcm.h

test_atracx: obj/test_atracx.o obj/cellAtracXdec_pcm.o $(call lrc_objs,$(test_atracx_LRC))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# cellVdec's decoders and conversions; rinflate for the gzipped references
test_vdec_LRC := \
	$(LRC)/formats/h264/rh264.c \
	$(LRC)/formats/mpeg1/rmpeg1_video.c \
	$(LRC)/formats/mpeg4/rmpeg4_video.c \
	$(LRC)/formats/image/image_yuv_blit.c \
	$(test_deflate_LRC)

obj/cellVdec_codec.o: ../../Emu/Cell/Modules/cellVdec_codec.c ../../Emu/Cell/Modules/cellVdec_codec.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_vdec.o: test_vdec.c test_common.h ../../Emu/Cell/Modules/cellVdec_codec.h

test_vdec: obj/test_vdec.o obj/cellVdec_codec.o $(call lrc_objs,$(sort $(test_vdec_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS) -lpthread

# cellSearch's listings and cellMusicDecode's playback: audio_transfer and
# its decoders, rtags, the containers, the sinc resampler, filestream
test_media_LRC := \
	$(LRC)/formats/audio_transfer.c \
	$(LRC)/formats/wav/rwav.c \
	$(LRC)/formats/flac/rflac.c \
	$(LRC)/formats/vorbis/rvorbis.c \
	$(LRC)/formats/opus/ropus.c \
	$(LRC)/formats/mp3/rmp3.c \
	$(LRC)/formats/aac/raac.c \
	$(LRC)/formats/mp4/rmp4.c \
	$(LRC)/formats/ac3/rac3_frame.c \
	$(LRC)/formats/ac3/rac3_decode.c \
	$(LRC)/formats/wma/rwma_standard.c \
	$(LRC)/formats/wma/rwma_pro.c \
	$(LRC)/formats/wma/rwma_stream.c \
	$(LRC)/formats/atrac3/ratrac3.c \
	$(LRC)/formats/atrac3p/ratrac3p.c \
	$(LRC)/formats/tags/rtags.c \
	$(LRC)/formats/webm/rwebm.c \
	$(LRC)/formats/mpeg1/rmpeg1_ps.c \
	$(LRC)/formats/container/rcontainer.c \
	$(LRC)/audio/resampler/drivers/sinc_resampler.c \
	$(LRC)/memmap/memalign.c \
	$(LRC)/streams/file_stream.c \
	$(LRC)/vfs/vfs_implementation.c \
	$(LRC)/file/file_path.c \
	$(LRC)/compat/compat_strcasestr.c \
	$(LRC)/compat/fopen_utf8.c \
	$(LRC)/encodings/encoding_utf.c \
	$(LRC)/string/stdstring.c \
	$(LRC)/string/rstrtod.c \
	$(LRC)/file/file_path_io.c \
	$(test_deflate_LRC)

obj/media_decode.o: ../../util/media_decode.c ../../util/media_decode.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_media.o: test_media.c test_common.h ../../util/media_decode.h

test_media: obj/test_media.o obj/media_decode.o $(call lrc_objs,$(sort $(test_media_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS) -lpthread

# cellRec's recordings: the encoders, the MP4 muxer, the scaler and the
# RGB to 4:2:0 conversion, and media_decode to read them back
test_record_LRC := \
	$(LRC)/formats/mpeg4/rmpeg4_encode.c \
	$(LRC)/formats/h264/rh264_encode.c \
	$(LRC)/formats/jpeg/rjpeg_encode.c \
	$(LRC)/formats/aac/raac_encode.c \
	$(LRC)/formats/mp4/rmp4_mux.c \
	$(LRC)/formats/image/image_yuv_blit.c \
	$(LRC)/audio/conversion/float_to_ulaw.c \
	$(LRC)/gfx/scaler/pixconv.c \
	$(LRC)/gfx/scaler/scaler.c \
	$(LRC)/gfx/scaler/scaler_filter.c \
	$(LRC)/gfx/scaler/scaler_int.c \
	$(test_media_LRC)

obj/media_encode.o: ../../util/media_encode.c ../../util/media_encode.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_record.o: test_record.c test_common.h ../../util/media_encode.h ../../util/media_decode.h

test_record: obj/test_record.o obj/media_encode.o obj/media_decode.o $(call lrc_objs,$(sort $(test_record_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS) -lpthread

# the per-game settings database's lookup, against every entry of the copy
# the core carries
test_config_database_LRC := \
	$(LRC)/formats/json/rjson.c \
	$(LRC)/string/rstrtod.c \
	$(LRC)/compat/compat_strl.c

obj/config_database.o: ../config_database.c ../config_database.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_config_database.o: test_config_database.c test_common.h ../config_database.h

test_config_database: obj/test_config_database.o obj/config_database.o $(call lrc_objs,$(sort $(test_config_database_LRC)))
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)
# VBlank Frequency "From List": the block of vblank_unlock.txt a game is in
obj/vblank_list.o: ../vblank_list.c ../vblank_list.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o $@ $<

obj/test_vblank_list.o: test_vblank_list.c test_common.h ../vblank_list.h

test_vblank_list: obj/test_vblank_list.o obj/vblank_list.o
	$(CC) $(SANITIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# cellRec's lock-free queues (util/video_sink.h) on retro_spsc; again
# under ThreadSanitizer, which needs the ring built with it too
VIDEO_SINK_DEPS := test_video_sink.cpp ../../util/video_sink.h $(LRC)/queues/retro_spsc.c $(LRC)/include/retro_spsc.h $(LRC)/include/retro_atomic.h
VIDEO_SINK_FLAGS := -std=c++20 -O1 -g -Wall -Wextra -I../.. -I$(LRC)/include

test_video_sink: $(VIDEO_SINK_DEPS)
	$(CXX) $(VIDEO_SINK_FLAGS) $(SANITIZE) -o $@ test_video_sink.cpp -x c -std=gnu99 $(LRC)/queues/retro_spsc.c -x none -lpthread

test_video_sink_tsan: $(VIDEO_SINK_DEPS)
	$(CXX) $(VIDEO_SINK_FLAGS) -fsanitize=thread -o $@ test_video_sink.cpp -x c -std=gnu99 $(LRC)/queues/retro_spsc.c -x none -lpthread

# The libretro swapchain's image bookkeeping, also under ThreadSanitizer
VK_IMAGES_DEPS := test_vk_images.c test_common.h ../../Emu/RSX/VK/vkutils/libretro_images.h $(LRC)/include/retro_atomic.h

test_vk_images: $(VK_IMAGES_DEPS)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -o $@ test_vk_images.c $(LDFLAGS) $(LDLIBS) -lpthread

test_vk_images_tsan: $(VK_IMAGES_DEPS)
	$(CC) $(CFLAGS) $(TEST_STD) -fsanitize=thread -o $@ test_vk_images.c $(LDFLAGS) $(LDLIBS) -lpthread

# The software frames' triple buffer, also under ThreadSanitizer
SW_FRAMES_DEPS := test_sw_frames.c test_common.h ../libretro_sw_frames.c ../libretro_sw_frames.h $(LRC)/include/retro_atomic.h

test_sw_frames: $(SW_FRAMES_DEPS)
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -o $@ test_sw_frames.c ../libretro_sw_frames.c $(LDFLAGS) $(LDLIBS) -lpthread

test_sw_frames_tsan: $(SW_FRAMES_DEPS)
	$(CC) $(CFLAGS) $(TEST_STD) -fsanitize=thread -o $@ test_sw_frames.c ../libretro_sw_frames.c $(LDFLAGS) $(LDLIBS) -lpthread

# The OpenGL renderer's frames to the frontend, on Mesa's software OpenGL
# through surfaceless EGL (libegl-dev and Mesa's EGL and drivers)
test_gl_frames: test_gl_frames.c test_common.h ../libretro_gl_frames.c ../libretro_gl_frames.h $(LRC)/include/retro_atomic.h
	$(CC) $(CFLAGS) -std=gnu99 -I../../../3rdparty/GL $(SANITIZE) -o $@ test_gl_frames.c ../libretro_gl_frames.c $(LDFLAGS) $(LDLIBS) -lEGL -lpthread

test_gl_fallback: test_gl_fallback.c test_common.h ../libretro_gl_fallback.c ../libretro_gl_fallback.h ../libretro_gl_frames.h
	$(CC) $(CFLAGS) -std=gnu99 -I../../../3rdparty/GL $(SANITIZE) -o $@ test_gl_fallback.c ../libretro_gl_fallback.c $(LDFLAGS) $(LDLIBS) -lEGL -lpthread -ldl

test_pad_values: test_pad_values.c test_common.h ../libretro_pad_values.c ../libretro_pad_values.h
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -o $@ test_pad_values.c ../libretro_pad_values.c $(LDFLAGS) $(LDLIBS)

test_input_poll: test_input_poll.c test_common.h ../libretro_input.c ../libretro_input.h
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -o $@ test_input_poll.c ../libretro_input.c $(LDFLAGS) $(LDLIBS)

# The module and the test as C89; libretro-common's conversion as it is built
test_audio_handover: test_audio_handover.c test_common.h ../libretro_audio_out.c ../libretro_audio_out.h
	@mkdir -p obj
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o obj/test_audio_handover.o test_audio_handover.c
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o obj/libretro_audio_out.o ../libretro_audio_out.c
	$(CC) $(CFLAGS) -std=gnu99 -w $(SANITIZE) -o $@ obj/test_audio_handover.o obj/libretro_audio_out.o $(LRC)/audio/conversion/float_to_s16.c $(LRC)/features/features_cpu.c $(LDFLAGS) $(LDLIBS)

# The module and the test as C89; libretro-common's ring as it is built
AUDIO_RING_DEPS := test_audio_ring.c test_common.h ../libretro_audio_ring.c ../libretro_audio_ring.h $(LRC)/queues/retro_spsc.c $(LRC)/include/retro_spsc.h

test_audio_ring: $(AUDIO_RING_DEPS)
	@mkdir -p obj
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o obj/test_audio_ring.o test_audio_ring.c
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -c -o obj/libretro_audio_ring.o ../libretro_audio_ring.c
	$(CC) $(CFLAGS) -std=gnu99 $(SANITIZE) -o $@ obj/test_audio_ring.o obj/libretro_audio_ring.o $(LRC)/queues/retro_spsc.c $(LDFLAGS) $(LDLIBS) -lpthread

test_audio_ring_tsan: $(AUDIO_RING_DEPS)
	@mkdir -p obj
	$(CC) $(CFLAGS) $(TEST_STD) -fsanitize=thread -c -o obj/test_audio_ring_tsan.o test_audio_ring.c
	$(CC) $(CFLAGS) $(TEST_STD) -fsanitize=thread -c -o obj/libretro_audio_ring_tsan.o ../libretro_audio_ring.c
	$(CC) $(CFLAGS) -std=gnu99 -fsanitize=thread -o $@ obj/test_audio_ring_tsan.o obj/libretro_audio_ring_tsan.o $(LRC)/queues/retro_spsc.c $(LDFLAGS) $(LDLIBS) -lpthread

test_audio_mix: test_audio_mix.c test_common.h ../../Emu/Cell/Modules/cellAudio_mix.c ../../Emu/Cell/Modules/cellAudio_mix.h
	$(CC) $(CFLAGS) $(TEST_STD) $(SANITIZE) -o $@ test_audio_mix.c ../../Emu/Cell/Modules/cellAudio_mix.c $(LDFLAGS) $(LDLIBS)

# The core's own code, searched for host clocks and sleeps
CLOCK_FILES := $(filter-out ../libretro.h,$(wildcard ../*.c ../*.cpp ../*.h)) ../../Emu/RSX/VK/VKLibretro.cpp ../../Emu/RSX/VK/VKLibretro.h

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
	./test_jpg_rows
	./test_http
	./test_bcn
	./tls_duplex_test.sh ./test_tls_duplex
	./test_yaml fixtures
	./test_font fixtures
	./test_np2 fixtures
	./test_rsx_scale fixtures
	./test_vpost fixtures
	./test_atracx fixtures
	./test_vdec fixtures
	./test_media fixtures
	./rec_check.sh rec_out
	./test_config_database ../config_database.dat
	./test_vblank_list ../vblank_unlock.txt
	./test_video_sink
	./test_video_sink_tsan
	./test_vk_images
	./test_vk_images_tsan
	./test_sw_frames
	./test_sw_frames_tsan
	./test_gl_frames
	./test_gl_fallback
	./test_pad_values
	./test_input_poll
	./test_audio_handover
	./test_audio_ring
	./test_audio_ring_tsan
	./test_audio_mix
	./no_host_clock.sh $(CLOCK_FILES)
	@# The pads take no lock: pad::g_pad_mutex is gone, and a merge of
	@# upstream's code that takes it does not bring it back
	@if grep -rn "g_pad_mutex" ../../Emu ../../Input ../*.cpp ../*.h ../*.c; then echo "pads: pad::g_pad_mutex is back - the pads take no lock (AGENTS.md)"; exit 1; else echo "pads: no pad lock"; fi
	@# Nor does cellGem take one of its own for what it keeps
	@if grep -n "gem\.mtx\|lock(mtx)\|tracker\.mutex\|lock(mutex)" ../../Emu/Cell/Modules/cellGem.cpp; then echo "cellGem: a lock of its own is back - what cellGem and its tracker keep are records (AGENTS.md)"; exit 1; else echo "cellGem: no lock of its own"; fi
	@# cellAudio's ring is the one that lends
	@if grep -n "simple_ringbuf\|SetWriteCallback" ../../Emu/Cell/Modules/cellAudio.h ../../Emu/Cell/Modules/cellAudio.cpp; then echo "cellAudio: upstream's ring or its write callback is back - its audio is lent from libretro_audio_ring (AGENTS.md)"; exit 1; else echo "cellAudio: its ring lends"; fi
	@# A file's reads and writes share nothing between threads: no counters of the core's own
	@if grep -n "retro_atomic_size_t\|fetch_add_size" ../libretro_vfs.cpp; then echo "VFS: a counter every read or write adds to is back - threads on different files would wait on one another for it (CHANGELOG, 0091)"; exit 1; else echo "VFS: nothing counted"; fi
	@# Nor do the mice: no lock in the handler, none taken by those that read them
	@if grep -n "shared_mutex\|std::mutex\|lock_guard\|scoped_lock" ../../Emu/Io/MouseHandler.h ../../Emu/Io/MouseHandler.cpp ../libretro_mouse_handler.h ../libretro_mouse_handler.cpp || grep -rn "handler\.mutex\|take_pending()" ../../Emu/Cell/Modules/cellMouse.cpp ../../Emu/Cell/Modules/cellGem.cpp ../../Emu/Io/usio.cpp ../../Emu/Io/GunCon3.cpp ../../Emu/Io/TopShotElite.cpp ../../Emu/Io/TopShotFearmaster.cpp ../../Emu/Io/GameTablet.cpp; then echo "mice: the mouse handler's lock is back - a mouse is a record read whole (AGENTS.md)"; exit 1; else echo "mice: no mouse lock"; fi

# The C89 modules with a NEON path, built for ARM64 and run under emulation
# (gcc-aarch64-linux-gnu, qemu-user): what the x86 run cannot see of them.
# The module with the build's own standard for C (gnu99, with which GCC
# fuses a multiply and an add on ARM unless told otherwise), the test C89.
ARM64_CC  ?= aarch64-linux-gnu-gcc
ARM64_RUN ?= qemu-aarch64

check-arm64:
	@mkdir -p obj
	$(ARM64_CC) -O3 -std=gnu99 -Wall -I$(LRC)/include -c -o obj/cellAudio_mix.arm64.o ../../Emu/Cell/Modules/cellAudio_mix.c
	$(ARM64_CC) -O2 $(TEST_STD) -I$(LRC)/include -c -o obj/test_audio_mix.arm64.o test_audio_mix.c
	$(ARM64_CC) -static -o obj/test_audio_mix.arm64 obj/test_audio_mix.arm64.o obj/cellAudio_mix.arm64.o -lm
	$(ARM64_RUN) obj/test_audio_mix.arm64

clean:
	rm -rf obj $(TESTS) *_out.* rec_out

.PHONY: all check check-arm64 clean
