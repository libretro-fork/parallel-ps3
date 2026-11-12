/* The frames the renderer reads back for a frontend without the hardware
 * Vulkan context, handed to retro_run without a lock: a triple buffer, the
 * renderer's thread filling one buffer, retro_run showing another, the
 * third the newest finished. A buffer grows only when a frame outgrows it,
 * so nothing is allocated from one frame to the next. One producer, one
 * consumer. C89. */

#ifndef LIBRETRO_SW_FRAMES_H
#define LIBRETRO_SW_FRAMES_H

#include <stddef.h>
#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The renderer's thread: a buffer of at least @size bytes to write the next
 * frame into; NULL when one that size cannot be had */
uint8_t *sw_frames_begin(size_t size);

/* The renderer's thread: the frame written since sw_frames_begin, finished */
void sw_frames_end(unsigned width, unsigned height, size_t pitch);

/* retro_run: the newest frame finished since the last take, valid until the
 * next take (the frontend may show it again until then); false when there is
 * none */
bool sw_frames_take(const void **data, unsigned *width, unsigned *height, size_t *pitch);

/* Every frame finished, for retro_run's stall report */
size_t sw_frames_presented(void);

/* The buffers freed; neither side may be using them */
void sw_frames_free(void);

#ifdef __cplusplus
}
#endif

#endif
