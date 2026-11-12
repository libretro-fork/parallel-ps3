// The OpenGL renderer's contexts (libretro_video.cpp): made through the
// frontend's RETRO_ENVIRONMENT_GET_HW_SHARED_CONTEXT_INTERFACE in
// context_reset, nothing of a window system in the core:
//
// - context_reset makes them all up front, on its own thread, and they are
//   handed out from any thread without a lock or an allocation: many
//   threads taking and giving back at once never hold the same one;
// - with all taken, a thread gets none rather than waiting;
// - a context given back is let go of on the thread that had it;
// - context_destroy destroys every one it made;
// - a frontend without the interface: context_reset says the renderer
//   cannot run, and nothing is made.
//
// A fake frontend: its contexts are counted, not real. Built by `make
// check` from the core's own objects and archives, the same link as the
// core's with this main.

#include "stdafx.h"
#include "libretro/libretro_video.h"
#include "Emu/RSX/GL/OpenGL.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static thread_local bool t_counting = false;
static std::atomic<usz> s_allocations{0};

void* operator new(std::size_t n)
{
	if (t_counting)
		s_allocations++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct retro_hw_shared_context
{
	std::atomic<int> current_on{0}; // threads it is current on
	bool destroyed = false;
};

static retro_hw_shared_context s_made[16];
static std::atomic<int> s_created{0};
static std::atomic<int> s_destroyed{0};
static std::atomic<int> s_double_current{0};
static std::thread::id s_frontend;
static std::atomic<int> s_off_thread_create{0};
static thread_local retro_hw_shared_context* t_current = nullptr;

static retro_hw_shared_context* RETRO_CALLCONV fake_create()
{
	if (std::this_thread::get_id() != s_frontend)
		s_off_thread_create++;
	const int i = s_created++;
	return i < 16 ? &s_made[i] : nullptr;
}

static bool RETRO_CALLCONV fake_make_current(retro_hw_shared_context* ctx, bool current)
{
	if (current)
	{
		if (ctx->current_on++ != 0)
			s_double_current++;
		t_current = ctx;
	}
	else if (t_current)
	{
		t_current->current_on--;
		t_current = nullptr;
	}
	return true;
}

static void RETRO_CALLCONV fake_destroy(retro_hw_shared_context* ctx)
{
	ctx->destroyed = true;
	s_destroyed++;
}

static const retro_hw_shared_context_interface s_iface = {
	RETRO_HW_SHARED_CONTEXT_INTERFACE_VERSION, fake_create, fake_make_current, fake_destroy
};

static bool s_offer_iface = true;

static bool fake_environment(unsigned cmd, void* data)
{
	if (cmd != RETRO_ENVIRONMENT_GET_HW_SHARED_CONTEXT_INTERFACE || !s_offer_iface)
		return false;
	*static_cast<const retro_hw_shared_context_interface**>(data) = &s_iface;
	return true;
}

static void APIENTRY fake_get_integerv(GLenum, GLint* v) { v[0] = v[1] = 0; v[2] = 1280; v[3] = 720; }

static retro_proc_address_t fake_get_proc(const char* sym)
{
	if (!std::strcmp(sym, "glGetIntegerv"))
		return reinterpret_cast<retro_proc_address_t>(fake_get_integerv);
	return nullptr;
}

static uintptr_t fake_get_fb() { return 0; }

int main()
{
	s_frontend = std::this_thread::get_id();

	// No interface: the renderer cannot run, nothing made
	s_offer_iface = false;
	CHECK(!libretro_video_init(fake_get_fb, fake_get_proc, fake_environment));
	CHECK(s_created == 0);
	libretro_video_deinit();

	// The interface: all made in context_reset, on its thread
	s_offer_iface = true;
	CHECK(libretro_video_init(fake_get_fb, fake_get_proc, fake_environment));
	const int made = s_created;
	CHECK(made >= 7); // the renderer and RPCS3's most compiler threads
	CHECK(s_off_thread_create == 0);

	LibretroGSFrame frame;

	// All taken: the next gets none, without waiting
	std::vector<draw_context_t> taken;
	for (int i = 0; i < made; i++)
		taken.push_back(frame.make_context());
	for (int i = 0; i < made; i++)
	{
		CHECK(taken[i] != nullptr);
		for (int j = 0; j < i; j++)
			CHECK(taken[i] != taken[j]);
	}
	CHECK(frame.make_context() == nullptr);
	for (draw_context_t ctx : taken)
		frame.delete_context(ctx);

	// Threads taking, using and giving back at once: never one context on
	// two threads, nothing allocated, nothing made after context_reset
	s_allocations = 0;
	{
		std::vector<std::thread> threads;
		std::atomic<int> got{0}, none{0};
		for (int t = 0; t < made + 4; t++)
		{
			threads.emplace_back([&]
			{
				t_counting = true;
				for (int i = 0; i < 20000; i++)
				{
					draw_context_t ctx = frame.make_context();
					if (!ctx)
					{
						none++;
						continue;
					}
					got++;
					frame.set_current(ctx);
					std::this_thread::yield();
					frame.delete_context(ctx);
				}
			});
		}
		for (auto& th : threads)
			th.join();
		std::printf("test_gl_contexts: %d taken and given back by %d threads, %d found none free, %d current on two threads\n",
			got.load(), made + 4, none.load(), s_double_current.load());
	}
	CHECK(s_allocations == 0);
	CHECK(s_double_current == 0);
	CHECK(s_created == made);
	for (int i = 0; i < made; i++)
		CHECK(s_made[i].current_on == 0);

	// context_destroy: every one destroyed
	libretro_video_deinit();
	CHECK(s_destroyed == made);
	CHECK(frame.make_context() == nullptr);

	std::printf("test_gl_contexts: %d contexts made in context_reset; %s\n", made, failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
