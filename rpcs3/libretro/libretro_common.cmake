# libretro-common, as RetroArch has it: libretro-common/ is a plain sync of
# the files of RetroArch's libretro-common/ the core uses, never edited here.
# A file added to the sync is listed here in the same commit.

set(LIBRETRO_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/libretro-common)

add_library(libretro_common STATIC
    ${LIBRETRO_COMMON_DIR}/compat/compat_posix_string.c
    ${LIBRETRO_COMMON_DIR}/compat/compat_strcasestr.c
    ${LIBRETRO_COMMON_DIR}/compat/compat_strl.c
    ${LIBRETRO_COMMON_DIR}/compat/fopen_utf8.c
    ${LIBRETRO_COMMON_DIR}/crypto/crypto.c
    ${LIBRETRO_COMMON_DIR}/crypto/kdf.c
    ${LIBRETRO_COMMON_DIR}/crypto/pk.c
    ${LIBRETRO_COMMON_DIR}/crypto/x509.c
    ${LIBRETRO_COMMON_DIR}/encodings/encoding_base64.c
    ${LIBRETRO_COMMON_DIR}/encodings/encoding_crc32.c
    ${LIBRETRO_COMMON_DIR}/encodings/encoding_deflate.c
    ${LIBRETRO_COMMON_DIR}/encodings/encoding_rzstd.c
    ${LIBRETRO_COMMON_DIR}/encodings/encoding_utf.c
    ${LIBRETRO_COMMON_DIR}/encodings/encoding_xxh64.c
    ${LIBRETRO_COMMON_DIR}/features/features_cpu.c
    ${LIBRETRO_COMMON_DIR}/file/file_path.c
    ${LIBRETRO_COMMON_DIR}/file/file_path_io.c
    ${LIBRETRO_COMMON_DIR}/file/retro_dirent.c
    ${LIBRETRO_COMMON_DIR}/formats/dds/rdds.c
    ${LIBRETRO_COMMON_DIR}/formats/gif/rgif.c
    ${LIBRETRO_COMMON_DIR}/formats/image/image_yuv_blit.c
    ${LIBRETRO_COMMON_DIR}/formats/jpeg/rjpeg.c
    ${LIBRETRO_COMMON_DIR}/formats/json/rjson.c
    ${LIBRETRO_COMMON_DIR}/formats/png/rpng.c
    ${LIBRETRO_COMMON_DIR}/formats/png/rpng_rows.c
    ${LIBRETRO_COMMON_DIR}/formats/xml/rxml.c
    ${LIBRETRO_COMMON_DIR}/formats/yaml/ryaml.c
    ${LIBRETRO_COMMON_DIR}/formats/yaml/ryaml_writer.c
    ${LIBRETRO_COMMON_DIR}/gfx/font/rtt.c
    ${LIBRETRO_COMMON_DIR}/gfx/scaler/pixconv.c
    ${LIBRETRO_COMMON_DIR}/gfx/scaler/scaler.c
    ${LIBRETRO_COMMON_DIR}/gfx/scaler/scaler_filter.c
    ${LIBRETRO_COMMON_DIR}/gfx/scaler/scaler_int.c
    ${LIBRETRO_COMMON_DIR}/hash/lrc_hash.c
    ${LIBRETRO_COMMON_DIR}/lists/string_list.c
    ${LIBRETRO_COMMON_DIR}/net/net_compat.c
    ${LIBRETRO_COMMON_DIR}/net/net_http.c
    ${LIBRETRO_COMMON_DIR}/net/net_socket.c
    ${LIBRETRO_COMMON_DIR}/net/net_socket_ssl_retro.c
    ${LIBRETRO_COMMON_DIR}/net/net_upnp.c
    ${LIBRETRO_COMMON_DIR}/streams/file_stream.c
    ${LIBRETRO_COMMON_DIR}/streams/trans_stream.c
    ${LIBRETRO_COMMON_DIR}/streams/trans_stream_deflate.c
    ${LIBRETRO_COMMON_DIR}/streams/trans_stream_pipe.c
    ${LIBRETRO_COMMON_DIR}/string/rstrtod.c
    ${LIBRETRO_COMMON_DIR}/string/stdstring.c
    ${LIBRETRO_COMMON_DIR}/time/rtime.c
    ${LIBRETRO_COMMON_DIR}/utils/md5.c
    ${LIBRETRO_COMMON_DIR}/vfs/vfs_implementation.c
)

set_target_properties(libretro_common PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(libretro_common PUBLIC ${LIBRETRO_COMMON_DIR}/include)

# net_http over libretro-common's own TLS client (net_socket_ssl_retro.c on
# crypto/), for the clans server; the defines change only the library's
# own sources, not its headers
target_compile_definitions(libretro_common PRIVATE HAVE_SSL HAVE_RETROSSL HAVE_CRYPTO)

# net_socket and net_http
if(WIN32)
    target_link_libraries(libretro_common PRIVATE ws2_32)
endif()
