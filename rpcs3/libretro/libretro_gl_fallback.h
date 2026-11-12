/* The renderer's OpenGL contexts for a frontend that does not make them
 * (no RETRO_ENVIRONMENT_GET_HW_SHARED_CONTEXT_INTERFACE): made the way the
 * core made them before that call existed, through the window system of
 * the frontend's current context - WGL on Windows, EGL or GLX elsewhere -
 * and handed out through the same interface the call gives, so the rest of
 * the core does not know which it has. Older frontends run the OpenGL
 * renderer as they always did.
 *
 * lr_gl_fallback_interface in context_reset, with the frontend's context
 * current: NULL when there is no window system it knows behind it. Its
 * create also only in context_reset. C89. */

#ifndef LIBRETRO_GL_FALLBACK_H
#define LIBRETRO_GL_FALLBACK_H

#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct retro_hw_shared_context_interface *lr_gl_fallback_interface(void);

#ifdef __cplusplus
}
#endif

#endif
