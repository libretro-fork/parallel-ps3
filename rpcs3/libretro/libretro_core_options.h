#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include "libretro.h"

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition
 * - Translations live in libretro_core_options_intl.h, which is
 *   generated from Crowdin - edit only the English texts here
 */

struct retro_core_option_v2_category option_cats_us[] = {
    { "cpu", "CPU", "PPU and SPU decoders, accuracy and CPU emulation options." },
    { "gpu", "GPU", "Renderer, resolution, shaders and graphics accuracy options." },
    { "threads", "Scheduling", "How the emulator's threads are scheduled, and how many it runs and compiles with: what a CPU with few cores, or a phone, may want changed." },
    { "network", "Network", "Network, PSN and RPCN options." },
    { "debug", "Debug", "Options from RPCS3's Debug tab: for diagnosing problems, rarely for playing." },
    { "core", "Core", "Language, region, overlay, frame pacing, volume and other options of the core itself." },
    { NULL, NULL, NULL }
};

struct retro_core_option_v2_definition option_defs_us[] = {
    // ==================== CPU ====================
    {
        "rpcs3_ppu_decoder", "PPU Decoder", NULL,
        "PPU (main CPU) decoder. LLVM Recompiler is fastest.",
        NULL, "cpu",
        { {"llvm", "Recompiler (LLVM)"}, {"interpreter", "Interpreter (static)"}, {NULL, NULL} },
        "llvm"
    },
    {
        "rpcs3_spu_decoder", "SPU Decoder", NULL,
        "SPU (co-processor) decoder. LLVM Recompiler is fastest.",
        NULL, "cpu",
        { {"llvm", "Recompiler (LLVM)"}, {"asmjit", "Recompiler (ASMJIT)"}, {"dynamic", "Interpreter (dynamic)"}, {"interpreter", "Interpreter (static)"}, {NULL, NULL} },
        "llvm"
    },
    {
        "rpcs3_spu_xfloat_accuracy", "SPU XFloat Accuracy", NULL,
        "SPU floating-point accuracy level.",
        NULL, "cpu",
        { {"accurate", "Accurate"}, {"approximate", "Approximate"}, {"relaxed", "Relaxed"}, {"inaccurate", "Inaccurate"}, {NULL, NULL} },
        "approximate"
    },
    {
        "rpcs3_spu_block_size", "SPU Block Size", NULL,
        "SPU recompiler block size. Mega/Giga may improve performance.",
        NULL, "cpu",
        { {"safe", "Safe"}, {"mega", "Mega"}, {"giga", "Giga"}, {NULL, NULL} },
        "safe"
    },
    {
        "rpcs3_accurate_rsx_reservation", "Accurate RSX Reservation Access", NULL,
        "Synchronize RSX memory accesses with the reservations of the CPU threads. Needed by a few games, slower.",
        NULL, "cpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_spu_accurate_dma", "Accurate SPU DMA", NULL,
        "Accurately processes SPU DMA operations, as in RPCS3. Fixes a few games, at a large cost in speed.",
        NULL, "cpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_spu_accurate_reservations", "Accurate SPU Reservations", NULL,
        "Accurately processes SPU reservations, as in RPCS3 (on by default there too). Turning it off can make some games faster and break others.",
        NULL, "cpu",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_debug_console_mode", "Debug Console Mode", NULL,
        "Emulates a PS3 debug console (more memory and debug features) instead of a retail one. Only for software that needs it; not recommended otherwise.",
        NULL, "cpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_mfc_delay_command", "Delay Each Odd MFC Command", NULL,
        "Delays every second MFC (SPU DMA) command, as in RPCS3. Only for the few games that need it.",
        NULL, "cpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_disable_getllar_spin_opt", "Disable SPU GETLLAR Spin Optimization", NULL,
        "Turn off the detection of SPU busy-wait loops on GETLLAR. A few games need it off.",
        NULL, "cpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_emulate_hdd_speed", "Emulate HDD Read Speed", NULL,
        "Reads from the emulated hard disk at the speed of the PS3's, for games that break when loading is instant.",
        NULL, "cpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_emulate_bdvd_speed", "Emulate BD-ROM Read Speed", NULL,
        "Reads from the disc at the PS3 drive's 2x speed (9 MB/s), for games that stream movies from disc and show corrupted video when loading is instant. Loading takes longer.",
        NULL, "cpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_ppu_reservation_priority", "PPU Reservation Priority", NULL,
        "Gives PPU reservations priority over the SPUs', as in RPCS3. Can help games that stall on them.",
        NULL, "cpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_llvm_precompilation", "PPU/SPU LLVM Precompilation", NULL,
        "Precompile PPU modules at boot for faster subsequent loads.",
        NULL, "cpu",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_sleep_timers_accuracy", "Sleep Timers Accuracy", NULL,
        "Sleep timers accuracy level.",
        NULL, "cpu",
        { {"auto", "Automatic"}, {"as_host", "As Host"}, {"usleep", "Usleep Only"}, {"all_timers", "All Timers"}, {NULL, NULL} },
        "auto"
    },
    {
        "rpcs3_clocks_scale", "Clocks Scale", NULL,
        "Scale PS3 clock speed percentage.",
        NULL, "cpu",
        { {"50", "50%"}, {"75", "75%"}, {"100", "100%"}, {"150", "150%"}, {"200", "200%"}, {"300", "300%"}, {NULL, NULL} },
        "100"
    },
    // ==================== GPU ====================
    {
        "rpcs3_renderer", "Renderer", NULL,
        "Vulkan renders on the frontend's Vulkan device and hands it each finished frame as an image; when the frontend is not running its Vulkan video driver, the frame is copied back through memory instead. OpenGL draws straight into the frontend's OpenGL context. (Restart required)",
        NULL, "gpu",
        { {"opengl", "OpenGL"}, {"vulkan", "Vulkan"}, {"null", "Null (No Video)"}, {NULL, NULL} },
        "vulkan"
    },
    {
        "rpcs3_aspect_ratio", "Aspect Ratio", NULL,
        "Changes output aspect ratio. Only some PS3 games support native 4:3. (Restart required)",
        NULL, "gpu",
        { {"16:9", "16:9 (default)"}, {"4:3", NULL}, {NULL, NULL} },
        "16:9"
    },
    {
        "rpcs3_anisotropic_filter", "Anisotropic Filtering", NULL,
        "Texture filtering quality.",
        NULL, "gpu",
        { {"auto", "Auto"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}, {NULL, NULL} },
        "auto"
    },
    {
        "rpcs3_msaa", "Anti-Aliasing", NULL,
        "Multi-sample anti-aliasing where the game asks for it, as in RPCS3.",
        NULL, "gpu",
        { {"auto", "Auto"}, {"disabled", NULL}, {NULL, NULL} },
        "auto"
    },
    {
        "rpcs3_zcull_accuracy", "ZCULL Accuracy", NULL,
        "How exactly occlusion queries are answered, as in RPCS3. Precise is correct and the default there; Approximate and Relaxed are faster, and can break effects such as lens flares. Relaxed is worth trying on phones.",
        NULL, "gpu",
        { {"precise", "Precise (Default)"}, {"approximate", "Approximate"}, {"relaxed", "Relaxed (Fastest)"}, {NULL, NULL} },
        "precise"
    },
    {
        "rpcs3_shader_quality", "Shader Quality", NULL,
        "Precision of the shaders RPCS3 generates. Low is fastest, Ultra most accurate.",
        NULL, "gpu",
        { {"low", "Low"}, {"high", "High"}, {"ultra", "Ultra"}, {NULL, NULL} },
        "high"
    },
    {
        "rpcs3_default_resolution", "Default Resolution", NULL,
        "DO NOT CHANGE THIS OPTION TO UPSCALE THE GAME. Use Resolution Scale instead. Only use 1080p if the game supports it. Rarely due to emulation bugs some games will only render at resolutions below 720p.",
        NULL, "gpu",
        { {"720p", "720p (Default)"}, {"1080p", "1080p"}, {"480p", "480p"}, {"576p", "576p"}, {NULL, NULL} },
        "720p"
    },
    {
        "rpcs3_resolution_scale", "Resolution Scale", NULL,
        "Internal rendering resolution as a percentage of the game's own. 200% of a 720p game is 2560x1440, of a 1080p one 3840x2160.",
        NULL, "gpu",
        { {"25", "25%"}, {"50", "50%"}, {"66", "66%"}, {"75", "75%"}, {"100", "100% (Native)"}, {"150", "150%"}, {"200", "200%"}, {"250", "250%"}, {"300", "300%"}, {"400", "400%"}, {"500", "500%"}, {"600", "600%"}, {"700", "700%"}, {"800", "800%"}, {NULL, NULL} },
        "100"
    },
    {
        "rpcs3_scale_threshold", "Resolution Scale Threshold", NULL,
        "Render targets smaller than this are not scaled. Some games need a different value for the scaling to look right.",
        NULL, "gpu",
        { {"1", "1x1"}, {"16", "16x16 (Default)"}, {"64", "64x64"}, {"128", "128x128"}, {"160", "160x160"}, {"256", "256x256"}, {"320", "320x320"}, {"512", "512x512"}, {"592", "592x592"}, {"640", "640x640"}, {"1024", "1024x1024"}, {NULL, NULL} },
        "16"
    },
    {
        "rpcs3_shader_mode", "Shader Mode", NULL,
        "How shaders are compiled. Async compiles in the background but the frame that first needs a pipeline waits for it, which is what a long freeze on a new scene usually is. Async with Shader Interpreter draws that frame through the interpreter instead and swaps in the compiled shader when it is ready: no freeze, lower speed while it catches up.",
        NULL, "gpu",
        { {"async", "Async (Recommended)"}, {"async_interpreter", "Async with Shader Interpreter (no stalls)"}, {"async_recompiler", "Async with Recompiler"}, {"interpreter", "Shader Interpreter only"}, {"sync", "Synchronous"}, {NULL, NULL} },
        "async"
    },
    {
        "rpcs3_write_color_buffers", "Write Color Buffers", NULL,
        "Write color buffers to main memory. Fixes some effects.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_strict_rendering", "Strict Rendering Mode", NULL,
        "Enable strict rendering for accuracy.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_stretch_to_display", "Stretch to Display Area", NULL,
        "Stretch game output to fill the display, reported to RetroArch at this aspect ratio: pick the display's for games patched to a wider one (21:9, 32:9). Needs RetroArch's aspect ratio set to Core Provided.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", "16:9"}, {"16:10", NULL}, {"21:9", NULL}, {"32:9", NULL}, {"4:3", NULL}, {"5:4", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_multithreaded_rsx", "Multithreaded RSX", NULL,
        "Moves part of the RSX work to a second thread. Off by default, as in RPCS3; it can help on CPUs with few fast cores, phones included.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_async_texture_streaming", "Asynchronous Texture Streaming", NULL,
        "Enable asynchronous texture streaming.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_host_gpu_labels", "Allow Host GPU Labels", NULL,
        "Lets the host GPU signal RSX labels itself instead of the CPU waiting on them. Faster in some games, unstable in others.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_disable_blit_upscaling", "Disable Blit Engine Upscaling", NULL,
        "As in RPCS3: copies made by the RSX blit engine are left at the game's own resolution instead of the Resolution Scale. Fixes some games' effects at higher scales, at the cost of a blurrier picture where they are used.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_disable_vertex_cache", "Disable Vertex Cache", NULL,
        "Turn off the cache of vertex data, as in RPCS3. Slower; only for games that show broken or stale geometry with it.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_emulate_depth_compare", "Emulate Special Depth Comparison", NULL,
        "Emulates the depth comparison modes the PS3 has and the host GPU lacks. Fixes some shadows, at a cost in speed.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_force_hw_msaa_resolve", "Force Hardware MSAA Resolve", NULL,
        "Resolves anti-aliased surfaces on the GPU's own hardware instead of a shader. Faster, but some games show artifacts with it.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_handle_tiled_memory", "Handle RSX Memory Tiling", NULL,
        "Emulates the RSX's tiled memory, for games that read their tiled surfaces back. Costs speed.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_read_depth_buffers", "Read Depth Buffers", NULL,
        "Read depth buffers from main memory.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_read_color_buffers", "Read Color Buffers", NULL,
        "Read color buffers from main memory.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_use_rebar", "Use Re-BAR Memory for GPU Uploads", NULL,
        "Uploads to the GPU through resizable BAR memory where the Vulkan driver offers it, as in RPCS3 (on by default there too). Turn off if it causes problems.",
        NULL, "gpu",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_write_depth_buffers", "Write Depth Buffers", NULL,
        "Write depth buffers to main memory.",
        NULL, "gpu",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_rsx_fifo_accuracy", "RSX FIFO Accuracy", NULL,
        "RSX FIFO command accuracy level.",
        NULL, "gpu",
        { {"fast", "Fast"}, {"atomic", "Atomic"}, {"atomic_ordered", "Ordered & Atomic"}, {"as_ps3", "PS3"}, {NULL, NULL} },
        "atomic"
    },
    {
        "rpcs3_driver_wakeup_delay", "Driver Wake-Up Delay", NULL,
        "Driver wake-up delay in microseconds. 0 by default, as in RPCS3; raise it only when a game needs it.",
        NULL, "gpu",
        { {"0", "0 (Default)"}, {"20", "20"}, {"50", "50"}, {"100", "100"}, {"200", "200"}, {"400", "400"}, {"800", "800"}, {NULL, NULL} },
        "0"
    },
    {
        "rpcs3_vblank_rate", "VBlank Frequency", NULL,
        "The PS3's refresh rate in Hz. It is also the frame rate the core asks RetroArch for, which takes effect when content is loaded; games that can run above 60 FPS need a display that refreshes that fast. \"From List\" doubles Vblank rate for supported games listed in vblank_unlock.txt found in system/parallel-ps3. You may need to apply per-game patches.",
        NULL, "gpu",
        { {"50", "50 Hz (PAL)"}, {"60", "60 Hz (NTSC)"}, {"120", "120 Hz"}, {"144", "144 Hz"}, {"240", "240 Hz"}, {"list", "From List"}, {NULL, NULL} },
        "60"
    },
    // ==================== SCHEDULING ====================
    {
        "rpcs3_thread_scheduler", "Thread Scheduler", NULL,
        "Who places the emulator's threads on the CPU's cores, as in RPCS3. Operating System leaves it to the system; the RPCS3 schedulers pin PPU, SPU and RSX threads to cores of their own, laid out for a few desktop CPUs. Has no effect on Android, where the core cannot pin threads.",
        NULL, "threads",
        { {"os", "Operating System"}, {"old", "RPCS3 Scheduler"}, {"alt", "RPCS3 Alternative Scheduler"}, {NULL, NULL} },
        "os"
    },
    {
        "rpcs3_preferred_spu_threads", "Preferred SPU Threads", NULL,
        "Number of SPU threads. Auto recommended.",
        NULL, "threads",
        { {"0", "Auto"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"}, {"6", "6"}, {NULL, NULL} },
        "0"
    },
    {
        "rpcs3_spu_loop_detection", "Enable SPU Loop Detection", NULL,
        "Detects SPU loops that only wait and skips through them. Faster, but some games break with it; off by default, as in RPCS3. Worth turning on for speed on phones.",
        NULL, "threads",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_llvm_threads", "PPU/SPU LLVM Compiler Threads", NULL,
        "How many threads compile PPU and SPU code with LLVM, as RPCS3's Max LLVM Compile Threads. Auto uses every core; fewer keeps a phone cooler and uses less memory while a game is compiled, and makes it take longer. On ARM CPUs with efficiency cores (most phones, some ARM laptops) the default leaves those out.",
        NULL, "threads",
        { {"0", "Auto"}, {"1", NULL}, {"2", NULL}, {"3", NULL}, {"4", NULL}, {"6", NULL}, {"8", NULL}, {NULL, NULL} },
        "0"
    },
    {
        "rpcs3_shader_compiler_threads", "Shader Compiler Threads", NULL,
        "How many threads compile shaders. Auto lets RPCS3 decide from the CPU it sees. On ARM CPUs with efficiency cores (most phones, some ARM laptops) the default leaves those out.",
        NULL, "threads",
        { {"auto", "Auto"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"6", "6"}, {"8", "8"}, {NULL, NULL} },
        "auto"
    },
    {
        "rpcs3_ppu_threads", "PPU Thread Count", NULL,
        "How many PPU threads run at once, as in RPCS3. 2 is the default and what nearly every game wants.",
        NULL, "threads",
        { {"1", NULL}, {"2", "2 (Default)"}, {"3", NULL}, {"4", NULL}, {"5", NULL}, {"6", NULL}, {"7", NULL}, {"8", NULL}, {NULL, NULL} },
        "2"
    },
    {
        "rpcs3_max_spurs_threads", "Maximum Number of SPURS Threads", NULL,
        "Maximum SPURS thread count. Lower may improve performance.",
        NULL, "threads",
        { {"auto", "Auto"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"}, {"6", "6"}, {NULL, NULL} },
        "auto"
    },
    {
        "rpcs3_max_preempt_count", "Max Power Saving CPU-Preemptions", NULL,
        "How many times a frame RPCS3 may pause the CPU threads to save power, as in RPCS3. 0 turns it off.",
        NULL, "threads",
        { {"0", "0 (Disabled)"}, {"10", NULL}, {"20", NULL}, {"50", NULL}, {"100", NULL}, {"200", NULL}, {"400", NULL}, {NULL, NULL} },
        "0"
    },
    // ==================== NETWORK ====================
    {
        "rpcs3_network_enabled", "Network Enabled", NULL,
        "Lets games reach the internet. Takes effect when content is loaded.",
        NULL, "network",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_psn_status", "PSN Status", NULL,
        "What games see of the PlayStation Network. Simulated makes them believe they are signed in, which some need to get past their menus; RPCN connects to RPCS3's own server and needs Network Enabled and an RPCN account: the core cannot create one, so set it up in standalone RPCS3 and copy its rpcn.yml into system/parallel-ps3/ (system/parallel-ps3/config/ on Windows). Takes effect when content is loaded.",
        NULL, "network",
        { {"disabled", NULL}, {"simulated", "Simulated"}, {"rpcn", "RPCN"}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_upnp", "UPnP", NULL,
        "Enable UPnP for automatic port forwarding.",
        NULL, "network",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_show_rpcn_popups", "Show RPCN Popups", NULL,
        "Show RPCN notification popups.",
        NULL, "network",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_show_trophy_popups", "Show Trophy Popups", NULL,
        "Show trophy unlock notifications.",
        NULL, "network",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_dns", "DNS Server", NULL,
        "DNS server address.",
        NULL, "network",
        { {"8.8.8.8", "Google DNS"}, {"1.1.1.1", "Cloudflare DNS"}, {"208.67.222.222", "OpenDNS"}, {NULL, NULL} },
        "8.8.8.8"
    },
    // ==================== DEBUG ====================
    {
        "rpcs3_disable_fifo_reordering", "Disable FIFO Reordering", NULL,
        "Turns off the reordering of RSX commands RPCS3 does for speed. For debugging.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_disable_hw_blending", "Disable Hardware Blending", NULL,
        "Forces programmable blending on renderers that support it, as in RPCS3's Debug tab. Purely a debugging option.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_disable_hw_colorspace", "Disable Hardware ColorSpace Remapping", NULL,
        "Remaps texture colour spaces in shaders instead of in the GPU's hardware. For debugging.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_disable_zcull_queries", "Disable ZCull Occlusion Queries", NULL,
        "Report occlusion queries as passed without running them. Faster; breaks games that rely on them (lens flares, visibility).",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_cpu_blit", "Force CPU Blit Emulation", NULL,
        "Force CPU blit emulation for certain effects.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_gpu_texture_scaling", "Force GPU Texture Scaling", NULL,
        "Scales textures on the GPU instead of the CPU. For debugging; can break games.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_spu_events_busy_loop", "Enable SPU Events Busy Loop", NULL,
        "Lets SPU threads spin while they wait for events instead of sleeping. Can help a few games, costs CPU time.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_hook_static_funcs", "Hook Static Functions", NULL,
        "Hook static functions for HLE.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_set_daz_ftz", "PPU Set DAZ and FTZ", NULL,
        "Sets the denormals-are-zero and flush-to-zero CPU flags for the PPU. For debugging.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_spu_cache_line_stores", "Accurate PPU/SPU Cache Line Stores", NULL,
        "Enable accurate cache line stores.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_ppu_set_fpcc", "Accurate PPU Float Condition Control", NULL,
        "Accurately set PPU FPCC bits.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_ppu_set_sat_bit", "Accurate PPU Saturation Bit", NULL,
        "Accurately set PPU saturation bit.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_ppu_nj_mode", "Accurate PPU Non-Java Mode", NULL,
        "PPU non-Java mode handling.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_ppu_accurate_vector_nan", "Accurate PPU Vector NaN Handling", NULL,
        "More accurate vector NaN handling.",
        NULL, "debug",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_accurate_ppu_128", "Accurate PPU 128 Reservations", NULL,
        "Processes 128-byte PPU reservation operations accurately: never, always, or for loops up to the given length. For debugging.",
        NULL, "debug",
        { {"0", "Never (Default)"}, {"-1", "Always"}, {"1", NULL}, {"2", NULL}, {"3", NULL}, {"4", NULL}, {"5", NULL}, {"6", NULL}, {"7", NULL}, {"8", NULL}, {"9", NULL}, {"10", NULL}, {"11", NULL}, {"12", NULL}, {"13", NULL}, {"14", NULL}, {NULL, NULL} },
        "0"
    },
    {
        "rpcs3_fb_aliasing_bias", "Framebuffer Aliasing Heuristic Bias", NULL,
        "Which way RPCS3 decides a surface used as both colour and depth. For debugging.",
        NULL, "debug",
        { {"auto", "Auto"}, {"color", "Prefer Color"}, {"depth", "Prefer Depth"}, {NULL, NULL} },
        "auto"
    },
    // ==================== CORE ====================
    {
        "rpcs3_database_override", "Database Settings Override", NULL,
        "Improves compatibility by automatically applying game-specific fixes: the settings RPCS3's database has for the game (as standalone RPCS3 applies them) take priority over the core options, global or per game, for the settings they name; every other setting stays as you set it. Off applies none of them. Takes effect the next time a game is started.",
        NULL, "core",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_perf_overlay", "Performance Overlay", NULL,
        "RPCS3's own performance overlay, drawn over the game: the game's frame rate as RPCS3 counts it, and at higher detail levels frame times and CPU and GPU load.",
        NULL, "core",
        { {"disabled", NULL}, {"minimal", "Frame rate only"}, {"low", "Low"}, {"medium", "Medium"}, {"high", "High"}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_master_volume", "Master Volume", NULL,
        "Master audio volume percentage.",
        NULL, "core",
        { {"0", "0%"}, {"10", "10%"}, {"20", "20%"}, {"30", "30%"}, {"40", "40%"}, {"50", "50%"}, {"60", "60%"}, {"70", "70%"}, {"80", "80%"}, {"90", "90%"}, {"100", "100%"}, {NULL, NULL} },
        "100"
    },
    {
        "rpcs3_audio_format", "Audio Format", NULL,
        "The speakers the PS3 tells games it has. Set it to RetroArch's Output Layout: a game picks its own stereo, 5.1 or 7.1 mix from what is offered, and a surround mix folded down by RetroArch does not always sound like the game's own stereo one. Needs a RetroArch with multi-channel audio; stereo otherwise. Takes effect when a game boots.",
        NULL, "core",
        { {"stereo", "Stereo"}, {"surround_5_1", "Surround 5.1"}, {"surround_7_1", "Surround 7.1"}, {NULL, NULL} },
        "stereo"
    },
    {
        "rpcs3_disk_cache_size", "Disk Cache Size", NULL,
        "Limits the cache games keep on dev_hdd1, which otherwise only grows. With a size, the games' caches used longest ago are deleted when a game is loaded or closed, until the rest fits. Wipe on Exit deletes all of it when a game is closed, as a PS3 does when it is switched off. A deleted cache is built again by its game, which can make that game's next start slower.",
        NULL, "core",
        { {"unlimited", "Unlimited"}, {"1gb", "1 GB"}, {"5gb", "5 GB"}, {"wipe", "Wipe on Exit"}, {NULL, NULL} },
        "1gb"
    },
    {
        "rpcs3_language", "System Language", NULL,
        "PS3 system language.",
        NULL, "core",
        { {"english", "English"}, {"japanese", "Japanese"}, {"french", "French"}, {"spanish", "Spanish"}, {"german", "German"}, {"italian", "Italian"}, {"dutch", "Dutch"}, {"portuguese", "Portuguese"}, {"russian", "Russian"}, {"korean", "Korean"}, {"chinese_trad", "Chinese (Traditional)"}, {"chinese_simp", "Chinese (Simplified)"}, {NULL, NULL} },
        "english"
    },
    {
        "rpcs3_license_area", "Console Region", NULL,
        "PS3 license region.",
        NULL, "core",
        { {"usa", "SCEA (Americas)"}, {"eu", "SCEE (Europe, Oceania)"}, {"jp", "SCEJ (Japan)"}, {"hk", "SCEH (Hong Kong, Southeast Asia)"}, {"kr", "SCEK (Korea)"}, {"cn", "SCH (China)"}, {NULL, NULL} },
        "usa"
    },
    {
        "rpcs3_enter_button", "Enter Button Assignment", NULL,
        "Button used for confirm actions.",
        NULL, "core",
        { {"cross", "Cross (Western)"}, {"circle", "Circle (Japanese)"}, {NULL, NULL} },
        "cross"
    },
    /* Default as standalone RPCS3: Fighting Games */
    {
        "rpcs3_usio_mode", "Arcade USIO Mode", NULL,
        "For the Namco System 357/369 arcade games: how the arcade board's buttons are read, which the two kinds of game do differently. Fighting Games for Tekken, Dragon Ball Zenkai Battle and the like; Shooter Games for Deadstorm Pirates, Dark Escape and the like, aimed with a light gun or the mouse. Shooter games take RetroArch's ports 3 and 4 as players 1 and 2, so in that mode ports 1-2 and 3-4 trade places. Taiko no Tatsujin works in either. Changing it applies at once.",
        NULL, "core",
        { {"fighting", "Fighting Games"}, {"shooter", "Shooter Games"}, {NULL, NULL} },
        "fighting"
    },
    {
        "rpcs3_show_ppu_compilation_hint", "Show PPU Compilation Hint", NULL,
        "Show hint when PPU modules are being compiled.",
        NULL, "core",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_show_shader_compilation_hint", "Show Shader Compilation Hint", NULL,
        "Show hint when shaders are being compiled.",
        NULL, "core",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_detailed_log", "Detailed Log", NULL,
        "Writes what the emulator logs to system/parallel-ps3/rpcs3_detailed.log, for a bug report. Off, nothing is logged at all, which is faster: every message costs the thread that makes it time. Takes effect when the next content is loaded.",
        NULL, "core",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    // ==================== HIDDEN ====================
    // Not in the menu, as RPCS3 keeps them out of its settings dialog (they
    // are config.yml only there); still read from the .opt file.
    {
        "rpcs3_spu_cache", "SPU Cache", NULL,
        "Enable SPU cache for faster subsequent loads.",
        NULL, "core",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_accurate_dfma", "Accurate DFMA", NULL,
        "Double-precision fused multiply-add done exactly, as in RPCS3 (on by default there too). Costs little on CPUs with FMA; turning it off can help on a slow phone, and breaks some games.",
        NULL, "core",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_spu_verification", "SPU Verification", NULL,
        "SPU code verification level.",
        NULL, "core",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_driver_recovery_timeout", "Driver Recovery Timeout", NULL,
        "GPU driver recovery timeout in milliseconds.",
        NULL, "core",
        { {"0", "Disabled"}, {"1000", "1 second"}, {"2000", "2 seconds"}, {"5000", "5 seconds"}, {"10000", "10 seconds"}, {NULL, NULL} },
        "1000"
    },
    {
        "rpcs3_mfc_shuffling", "MFC Commands Shuffling", NULL,
        "Shuffle MFC commands for accuracy.",
        NULL, "core",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    {
        "rpcs3_spu_delay_penalty", "SPU Delay Penalty", NULL,
        "SPU delay penalty for scheduling.",
        NULL, "core",
        { {"0", "0"}, {"1", "1"}, {"2", "2"}, {"3", "3 (Default)"}, {"4", "4"}, {"5", "5"}, {NULL, NULL} },
        "3"
    },
    {
        "rpcs3_vblank_ntsc", "VBlank NTSC Fixup", NULL,
        "Run the VBlank Frequency at the NTSC rate (59.94 Hz for 60) instead of the round number. On by default, as in RPCS3.",
        NULL, "core",
        { {"enabled", NULL}, {"disabled", NULL}, {NULL, NULL} },
        "enabled"
    },
    {
        "rpcs3_hle_lwmutex", "HLE lwmutex", NULL,
        "Use HLE implementation for lwmutex.",
        NULL, "core",
        { {"disabled", NULL}, {"enabled", NULL}, {NULL, NULL} },
        "disabled"
    },
    { NULL, NULL, NULL, NULL, NULL, NULL, {{NULL, NULL}}, NULL }
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us,       /* RETRO_LANGUAGE_ENGLISH */
   &options_ja,       /* RETRO_LANGUAGE_JAPANESE */
   &options_fr,       /* RETRO_LANGUAGE_FRENCH */
   &options_es,       /* RETRO_LANGUAGE_SPANISH */
   &options_de,       /* RETRO_LANGUAGE_GERMAN */
   &options_it,       /* RETRO_LANGUAGE_ITALIAN */
   &options_nl,       /* RETRO_LANGUAGE_DUTCH */
   &options_pt_br,    /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   &options_pt_pt,    /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   &options_ru,       /* RETRO_LANGUAGE_RUSSIAN */
   &options_ko,       /* RETRO_LANGUAGE_KOREAN */
   &options_cht,      /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   &options_chs,      /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   &options_eo,       /* RETRO_LANGUAGE_ESPERANTO */
   &options_pl,       /* RETRO_LANGUAGE_POLISH */
   &options_vn,       /* RETRO_LANGUAGE_VIETNAMESE */
   &options_ar,       /* RETRO_LANGUAGE_ARABIC */
   &options_el,       /* RETRO_LANGUAGE_GREEK */
   &options_tr,       /* RETRO_LANGUAGE_TURKISH */
   &options_sk,       /* RETRO_LANGUAGE_SLOVAK */
   &options_fa,       /* RETRO_LANGUAGE_PERSIAN */
   &options_he,       /* RETRO_LANGUAGE_HEBREW */
   &options_ast,      /* RETRO_LANGUAGE_ASTURIAN */
   &options_fi,       /* RETRO_LANGUAGE_FINNISH */
   &options_id,       /* RETRO_LANGUAGE_INDONESIAN */
   &options_sv,       /* RETRO_LANGUAGE_SWEDISH */
   &options_uk,       /* RETRO_LANGUAGE_UKRAINIAN */
   &options_cs,       /* RETRO_LANGUAGE_CZECH */
   &options_val,      /* RETRO_LANGUAGE_CATALAN_VALENCIA */
   &options_ca,       /* RETRO_LANGUAGE_CATALAN */
   &options_en,       /* RETRO_LANGUAGE_BRITISH_ENGLISH */
   &options_hu,       /* RETRO_LANGUAGE_HUNGARIAN */
   &options_be,       /* RETRO_LANGUAGE_BELARUSIAN */
   &options_gl,       /* RETRO_LANGUAGE_GALICIAN */
   &options_no,       /* RETRO_LANGUAGE_NORWEGIAN */
   &options_ga,       /* RETRO_LANGUAGE_IRISH */
};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static inline void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
