#include "stdafx.h"
#include "OpenGL.h"

// The table's pointers, filled by libretro_video.cpp through the frontend
#define OPENGL_PROC(p, n) p gl##n = nullptr
#define WGL_PROC(p, n)
#define OPENGL_PROC2(p, n, tn) OPENGL_PROC(p, n)
	#include "GLProcTable.h"
#undef OPENGL_PROC
#undef OPENGL_PROC2
#undef WGL_PROC

void gl::init()
{
	// libretro_video.cpp has filled the table from the frontend
}

void gl::set_swapinterval(int)
{
	// The frontend presents
}
