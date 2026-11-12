#pragma once

#include "Emu/RSX/GSFrameBase.h"
#include "libretro.h"

#include <functional>
#include <atomic>
#include <condition_variable>
#include <mutex>

// Initialize libretro video subsystem
bool libretro_video_init(retro_hw_get_current_framebuffer_t get_fb, retro_hw_get_proc_address_t get_proc,
    retro_environment_t environ_cb);

// Deinitialize libretro video subsystem
void libretro_video_deinit();

// Get the current framebuffer
uintptr_t libretro_get_current_framebuffer();

// Get OpenGL proc address
void* libretro_get_proc_address(const char* sym);

// Clean up GL state before returning control to frontend
void libretro_cleanup_gl_state();

// Get the actual FBO dimensions from RetroArch
int libretro_get_fbo_width();
int libretro_get_fbo_height();

// The renderer's thread: the FBO to present the next frame into, over one of
// the textures shared with the frontend's context (libretro_gl_frames.c)
unsigned int libretro_get_rsx_fbo();

// The size the renderer draws the next frame at
int libretro_get_shared_texture_width();
int libretro_get_shared_texture_height();

// Ensure shared texture is large enough for the given resolution
// Called when game resolution changes (e.g., resolution scaling)
void libretro_ensure_render_size(int width, int height);

// retro_run: the newest frame finished, blitted into RetroArch's FBO at its
// own size (*width x *height); false, with nothing blitted, when none has
// been finished since the last
bool libretro_blit_to_frontend(unsigned* width, unsigned* height);

// Software present path: hands out the last frame the renderer produced, if
// one has arrived since the previous call. The pointer stays valid until the
// next frame replaces it, which is long enough for video_cb.
bool libretro_take_software_frame(const void** data, u32* width, u32* height, u32* pitch);

// LibretroGSFrame - GSFrameBase implementation for libretro
class LibretroGSFrame : public GSFrameBase
{
private:
    draw_context_t m_context = nullptr;
    int m_width = 1280;
    int m_height = 720;
    bool m_shown = false;

    // Get actual FBO dimensions from OpenGL
    void update_dimensions_from_fbo();

public:
    LibretroGSFrame();
    ~LibretroGSFrame() override;

    void close() override;
    void reset() override;
    bool shown() override;
    void hide() override;
    void show() override;
    void toggle_fullscreen() override;

    void delete_context(draw_context_t ctx) override;
    draw_context_t make_context() override;
    void set_current(draw_context_t ctx) override;
    void flip(draw_context_t ctx, bool skip_frame = false) override;

    int client_width() override;
    int client_height() override;

    // Update dimensions (call when FBO might have changed)
    void set_dimensions(int w, int h);
    f64 client_display_rate() override;
    bool has_alpha() override;

    display_handle_t handle() const override;

    bool can_consume_frame() const override;
    void present_frame(std::vector<u8>&& data, u32 pitch, u32 width, u32 height, bool is_bgra) const override;
    void take_screenshot(std::vector<u8>&& sshot_data, u32 sshot_width, u32 sshot_height, bool is_bgra) override;
    // The frontend owns the window and its title.
    void update_title(double /*fps*/ = 0.0) override {}
};
