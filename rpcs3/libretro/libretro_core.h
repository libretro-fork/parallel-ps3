#pragma once

#include "libretro.h"

// Global libretro callback accessors
retro_environment_t get_environ_cb();
retro_video_refresh_t get_video_cb();
retro_audio_sample_batch_t get_audio_batch_cb();
retro_input_state_t get_input_state_cb();
retro_log_printf_t get_log_cb();

// Hardware render callback
const retro_hw_render_callback& get_hw_render();

// Core directories
const char* get_system_dir();
const char* get_save_dir();

#ifdef HAVE_VULKAN
// The Vulkan context negotiation the core gives the frontend: version 2
// (create_instance, create_device2) when the frontend says it has it,
// version 1 (create_device) otherwise
struct retro_hw_render_context_negotiation_interface_vulkan;
const struct retro_hw_render_context_negotiation_interface_vulkan* libretro_vulkan_negotiation(retro_environment_t cb);
#endif
