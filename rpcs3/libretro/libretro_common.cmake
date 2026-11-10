# libretro-common, as RetroArch has it: libretro-common/ is a plain sync of
# the files of RetroArch's libretro-common/ the core uses, never edited here.
# A file added to the sync is listed here in the same commit.

set(LIBRETRO_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/libretro-common)

add_library(libretro_common STATIC
    ${LIBRETRO_COMMON_DIR}/encodings/encoding_rzstd.c
    ${LIBRETRO_COMMON_DIR}/encodings/encoding_xxh64.c
)

set_target_properties(libretro_common PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(libretro_common PUBLIC ${LIBRETRO_COMMON_DIR}/include)
