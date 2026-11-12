#pragma once
// The core's: every GL function, OpenGL 1.0's included, is a pointer in the
// table, filled through the frontend's get_proc_address
// (libretro_video.cpp) on every platform - no system GL header or library,
// no GLEW, nothing of a window system. glcorearb.h declares no functions
// without GL_GLEXT_PROTOTYPES; glext.h after it adds the vendor extensions
// the table names.
#include <glcorearb.h>
#include <glext.h>

// OpenGL 1.0's, which neither has, and fbo.cpp names: a compatibility
// context's only
typedef void (APIENTRYP PFNGLDRAWPIXELSPROC)(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);

#define OPENGL_PROC(p, n) extern p gl##n
#define WGL_PROC(p, n)
#define OPENGL_PROC2(p, n, tn) OPENGL_PROC(p, n)
	#include "GLProcTable.h"
#undef OPENGL_PROC
#undef WGL_PROC
#undef OPENGL_PROC2

#ifndef GL_TEXTURE_BUFFER_BINDING
//During spec release, this enum was removed during upgrade from ARB equivalent
//See https://www.khronos.org/bugzilla/show_bug.cgi?id=844
#define GL_TEXTURE_BUFFER_BINDING 0x8C2A
#endif

namespace gl
{
	void init();
	void set_swapinterval(int interval);
}
