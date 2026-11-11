/* vblank_unlock.txt, the games for VBlank Frequency "From List": which
 * block a title ID is listed in. C89. */

#ifndef LIBRETRO_VBLANK_LIST_H
#define LIBRETRO_VBLANK_LIST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 30 when @serial is under a "[30 ..." line of @text (@len bytes), 60 when
 * under a "[60 ...", 0 when it is in neither. A line's text after # is a
 * comment; spaces, tabs and a CR around a title ID are not part of it. */
unsigned vblank_list_block(const char *text, size_t len, const char *serial);

#ifdef __cplusplus
}
#endif

#endif
