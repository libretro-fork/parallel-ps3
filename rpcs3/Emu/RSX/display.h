#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

// The frontend has the window, and the core no window system: a handle to
// nothing
using display_handle_t = void*;

using draw_context_t = void*;
