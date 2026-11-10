// RPCS3 libretro core implementation
// Focuses on OpenGL rendering backend

#include "stdafx.h"
#include <bit>
#include <deque>

#include "libretro.h"
#include "libretro_core.h"
#include "libretro_core_options.h"
#include "libretro_patch_manager.h"
#include "Loader/PSF.h"

#include <cstdlib>

#include "Emu/System.h"
#include "Emu/savestate_utils.hpp"
#include "Emu/system_config.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/Cell/Modules/cellSysutil.h"
#include "Emu/NP/rpcn_config.h"
#include "Crypto/unpkg.h"
#include "Emu/Cell/Modules/cellOskDialog.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/cellSysutil.h"
#include "Emu/RSX/Overlays/overlay_manager.h"
#include "Emu/RSX/Overlays/overlay_save_dialog.h"
#include "Emu/RSX/Overlays/overlay_perf_metrics.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"
#include "Emu/Cell/Modules/sceNp.h"
#include "Emu/Io/Null/null_camera_handler.h"
#include "Emu/Io/Null/null_music_handler.h"
#include "Emu/Io/Null/NullKeyboardHandler.h"
#include "Emu/Io/KeyboardHandler.h"
#include "Emu/Io/MouseHandler.h"
// The OpenGL renderer, where there is one to have. On Android there is no
// desktop GL and these headers reach for GL/glew.h, so the software path uses
// Vulkan instead - see g_libretro_software_present. macOS has no GL renderer
// either (LIBRETRO_NO_GL, from CMakeLists.txt): Vulkan, through MoltenVK.
#ifndef LIBRETRO_NO_GL
#include "Emu/RSX/GL/GLGSRender.h"
#endif
#ifdef HAVE_VULKAN
#include "Emu/RSX/VK/VKGSRender.h"
#include "Emu/RSX/VK/VKLibretro.h"
#include "Emu/RSX/VK/vkutils/instance.h"
#include "Emu/RSX/VK/vkutils/swapchain.h"
#include "libretro_vulkan.h"
#endif
#include "Emu/RSX/Null/NullGSRender.h"
#include "Emu/IdManager.h"
#include "Emu/VFS.h"
#include "Emu/emu_callbacks.h"
#include "Emu/system_progress.hpp"
#include "Emu/RSX/Overlays/overlay_utils.h"
#include <cstring>
#include "libretro_localized_strings.h"
#include <formats/rjson.h>
#include "Emu/Audio/audio_device_enumerator.h"
#include "Emu/RSX/RSXThread.h"
#include "Input/pad_thread.h"
#include "util/video_source.h"
#include "Emu/vfs_config.h"
#include "Emu/cache_utils.hpp"
#include "Utilities/File.h"
#include "Utilities/StrUtil.h"
#include "Utilities/stack_trace.h"

#include "libretro_audio.h"
#include "libretro_input.h"
#include "libretro_video.h"
#include "libretro_firmware.h"
#include "libretro_pad_handler.h"
#include "libretro_mouse_handler.h"
#include "libretro_vfs.h"
#include "libretro_ui_icons.h"
#include "Loader/ISO.h"

#include <clocale>
#include <chrono>
#include <ctime>
#include <atomic>
#include <thread>
#include <mutex>
#include <optional>
#include <string_view>
#include <functional>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <Windows.h>
#endif
#ifdef __linux__
#include <sys/prctl.h>
#endif
#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
#ifdef __APPLE__
#include <climits>
#endif

// The timer settings standalone RPCS3 makes at startup, in rpcs3.cpp - which
// is not part of the core, so until now nothing made them. On Windows the
// timer then ran at its 15.6 ms default, every sleep in the emulator lasted at
// least that long, and games crawled at a third of their speed while the
// emulator's threads spent most of their time waiting (NNshi: 30 fps games at
// about 10, audio padded with silence). Standalone asks for the finest
// resolution the system offers, 0.5 ms; the core does the same for as long as
// it is loaded and puts the old one back after.
#ifdef _WIN32
LOG_CHANNEL(sys_log, "SYS");

typedef LONG(NTAPI* lr_NtQueryTimerResolution_t)(PULONG, PULONG, PULONG);
typedef LONG(NTAPI* lr_NtSetTimerResolution_t)(ULONG, BOOLEAN, PULONG);
static bool s_timer_resolution_set = false;
static bool s_wsa_started = false;
static ULONG s_timer_resolution = 0;

static void lrcore_raise_timer_resolution()
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return;
    const auto query = reinterpret_cast<lr_NtQueryTimerResolution_t>(GetProcAddress(ntdll, "NtQueryTimerResolution"));
    const auto set = reinterpret_cast<lr_NtSetTimerResolution_t>(GetProcAddress(ntdll, "NtSetTimerResolution"));
    ULONG min_res = 0, max_res = 0, cur_res = 0;
    if (!query || !set || query(&min_res, &max_res, &cur_res) != 0)
        return;
    ULONG new_res = 0;
    if (set(max_res, TRUE, &new_res) == 0)
    {
        s_timer_resolution_set = true;
        s_timer_resolution = max_res;
        sys_log.notice("New timer resolution: %d us (old=%d us, min=%d us, max=%d us)", new_res / 10, cur_res / 10, min_res / 10, max_res / 10);
    }
    else
    {
        sys_log.error("Failed to set timer resolution!");
    }
}

static void lrcore_restore_timer_resolution()
{
    if (!s_timer_resolution_set)
        return;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto set = ntdll ? reinterpret_cast<lr_NtSetTimerResolution_t>(GetProcAddress(ntdll, "NtSetTimerResolution")) : nullptr;
    ULONG cur_res = 0;
    if (set)
        set(s_timer_resolution, FALSE, &cur_res);
    s_timer_resolution_set = false;
}
#endif


// Libretro callbacks
static retro_environment_t environ_cb = nullptr;
static retro_video_refresh_t video_cb = nullptr;
static retro_audio_sample_t audio_cb = nullptr;
static retro_audio_sample_batch_t audio_batch_cb = nullptr;
static retro_input_poll_t input_poll_cb = nullptr;
static retro_input_state_t input_state_cb = nullptr;
retro_log_printf_t log_cb = nullptr;

// Hardware render callback for OpenGL
static retro_hw_render_callback hw_render;

// No hardware context: the renderer finishes each frame into system memory and
// retro_run hands those pixels to the frontend. Android has no desktop OpenGL
// for this port to ask for, so there it is the only way to draw anything at
// all; everywhere else the hardware path is better and this stays off.
#ifdef ANDROID
bool g_libretro_software_present = true;
#else
bool g_libretro_software_present = false;
#endif
bool g_libretro_deferred_readback = false;

// Vulkan with the frontend's hardware context: the device is made on the
// frontend's instance during context negotiation and each finished frame goes
// to it as an image (vk::libretro). Off, Vulkan draws on a device of its own
// and the frame comes back through memory (g_libretro_software_present).
bool g_libretro_vulkan_hw = false;

// Core state
static bool core_initialized = false;
static bool game_loaded = false;
static bool pending_game_boot = false;
static u64 s_run_count = 0; // retro_run calls, for save states
static std::string game_path;
// The disc image a game was loaded from, empty for anything else: a reset
// mounts it again, as the emulator's stop drops it (Emulator::Kill).
static std::string s_iso_path;
static std::string system_dir;
static std::string save_dir;
static std::string content_dir;  // RetroArch's content/games directory for PKG installation

// Pad thread instance for libretro input
static std::unique_ptr<pad_thread> g_libretro_pad_thread;

// Libretro pad handler instance
static std::shared_ptr<LibretroPadHandler> g_libretro_pad_handler;

// Pause watchdog: RetroArch may stop calling retro_run() when paused.
// We pause/resume RPCS3 from a small watchdog thread based on retro_run call gaps.
static std::atomic<bool> s_pause_watchdog_running{false};
static std::atomic<bool> s_pause_watchdog_stop{false};
static std::atomic<bool> s_paused_by_watchdog{false};
static std::atomic<long long> s_last_retro_run_us{0};
static std::thread s_pause_watchdog_thread;

// Thread synchronization
static std::mutex emu_mutex;
static std::atomic<bool> frame_ready{false};

static inline long long lr_now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void stop_pause_watchdog()
{
#ifdef HAVE_VULKAN
    // Called before every stop of the emulator: no retro_run will take
    // frames now, so a flip must not wait for one
    vk::libretro::frontend_detached();
#endif

    if (!s_pause_watchdog_running.exchange(false))
        return;

    s_pause_watchdog_stop.store(true);
    if (s_pause_watchdog_thread.joinable())
    {
        s_pause_watchdog_thread.join();
    }
    s_pause_watchdog_stop.store(false);
    s_paused_by_watchdog.store(false);
    s_last_retro_run_us.store(0);
}

static void start_pause_watchdog()
{
    if (s_pause_watchdog_running.exchange(true))
        return;

    s_pause_watchdog_stop.store(false);
    s_paused_by_watchdog.store(false);
    s_last_retro_run_us.store(lr_now_us());

    s_pause_watchdog_thread = std::thread([]()
    {
        constexpr long long pause_threshold_us = 100'000;
        constexpr long long resume_threshold_us = 40'000;

        while (!s_pause_watchdog_stop.load())
        {
            if (!core_initialized || !game_loaded)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            const long long now_us = lr_now_us();
            const long long last_us = s_last_retro_run_us.load();
            if (last_us == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            const long long gap_us = now_us - last_us;

            if (gap_us > pause_threshold_us)
            {
                if (!s_paused_by_watchdog.load() && Emu.IsRunning())
                {
                    Emu.Pause(false, false);
                    s_paused_by_watchdog.store(true);
                }
            }
            else if (gap_us < resume_threshold_us)
            {
                if (s_paused_by_watchdog.load() && Emu.IsPaused())
                {
                    Emu.Resume();
                    s_paused_by_watchdog.store(false);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

// Helper to display libretro messages/notifications
static void libretro_show_message(const char* msg, unsigned frames = 180)
{
    if (!environ_cb)
        return;

    retro_message rm{};
    rm.msg = msg;
    rm.frames = frames;
    environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &rm);
}

// A picture that stops while the game runs on - GT5's menu froze with its
// music and cursor going on until a pause and resume, inFamous went black
// after its intro (ozzfreak) - left nothing in the log to say where the frames
// stopped. Say it when it happens: after a few seconds without a new frame
// while the emulator runs, log what moved since the last one. No RSX flips:
// the game or the renderer stopped. Flips but no frames handed over: the
// renderer flipped without copying a frame out. Frames handed over but none
// taken: the handoff to retro_run. VBLANKs posted behind those requested: the
// frontend-paced VBLANK thread.
static void libretro_check_frame_stall(bool new_frame)
{
    struct counters
    {
        u64 flips = 0, handed = 0, vblanks = 0, requested = 0;
    };

    const auto now_counters = []()
    {
        counters c{};
        if (rsx::thread* rsx = rsx::get_current_renderer())
        {
            c.flips = rsx->int_flip_index;
            c.vblanks = rsx->vblank_count;
        }
        c.handed = libretro_sw_frames_presented();
        c.requested = g_libretro_vblank_requests;
        return c;
    };

    static counters s_at_frame{};
    static u64 s_frame_us = 0;
    static u64 s_report_us = 0;
    static bool s_reported = false;

    const u64 now = lr_now_us();

    if (new_frame || !Emu.IsRunning() || !s_frame_us)
    {
        if (new_frame && s_reported)
            rsx_log.warning("libretro: frames again after %.1fs", (now - s_frame_us) / 1e6);
        s_at_frame = now_counters();
        s_frame_us = now;
        s_reported = false;
        return;
    }

    if (now - s_frame_us < 3'000'000 || now - s_report_us < 10'000'000)
        return;

    const counters c = now_counters();
    rsx_log.error("libretro: no new frame for %.1fs while running: RSX flips +%u, frames handed over +%u, "
        "VBLANKs +%u of +%u requested (frontend pacing %s), progress dialog '%s'",
        (now - s_frame_us) / 1e6, c.flips - s_at_frame.flips, c.handed - s_at_frame.handed,
        c.vblanks - s_at_frame.vblanks, c.requested - s_at_frame.requested,
        g_libretro_frontend_vblank ? "on" : "off", std::string(g_progr_text));
    s_report_us = now;
    s_reported = true;
}

// Check if a file path has a PKG extension
static bool is_pkg_file(const std::string& path)
{
    if (path.size() < 4)
        return false;

    std::string ext = path.substr(path.size() - 4);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    return ext == ".pkg";
}

// Install a PKG file and return the path to the installed EBOOT.BIN
// A PSN game's PKG installs without its license, but its EBOOT.BIN only
// decrypts with the .rap file, which RPCS3 looks for in
// dev_hdd0/home/<user>/exdata/ (standalone has its user drop it there). The
// .rap files next to the PKG - a PSN dump comes as both - are copied there with
// the install, unless one of that name is there already (sco).
static void install_rap_files(const std::string& pkg_path)
{
    const std::string pkg_dir = fs::get_parent_dir(pkg_path);
    const std::string exdata = g_cfg_vfs.get(g_cfg_vfs.dev_hdd0, system_dir + "/rpcs3/") + "home/00000001/exdata/";
    fs::dir dir(pkg_dir);
    if (!dir)
        return;
    for (const fs::dir_entry& entry : dir)
    {
        if (entry.is_directory || entry.name.size() <= 4 || fmt::to_lower(entry.name.substr(entry.name.size() - 4)) != ".rap")
            continue;
        // RPCS3 wants the extension in lower case
        const std::string dest = exdata + entry.name.substr(0, entry.name.size() - 4) + ".rap";
        if (fs::is_file(dest))
            continue;
        fs::create_path(exdata);
        if (fs::copy_file(pkg_dir + "/" + entry.name, dest, false))
        {
            if (log_cb)
                log_cb(RETRO_LOG_INFO, "RPCS3: license %s installed to %s\n", entry.name.c_str(), exdata.c_str());
        }
        else if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: could not copy %s to %s\n", entry.name.c_str(), exdata.c_str());
    }
}

// Installs the PKG; true when it went in. eboot_path is its EBOOT.BIN, empty
// for content with none (DLC, an update without a game).
static bool install_pkg_file(const std::string& pkg_path, std::string& eboot_path)
{

    libretro_show_message("Installing PKG file...", 300);

    // Determine install directory - use content_dir if available, otherwise system/rpcs3/dev_hdd0/game
    std::string install_base;
    if (!content_dir.empty())
    {
        install_base = content_dir + "/";
    }
    else if (!system_dir.empty())
    {
        // dev_hdd0 where vfs.yml puts it, which may be outside RetroArch's
        // folders (NNshi); system/rpcs3/dev_hdd0/ unless it says otherwise.
        g_cfg_vfs.load();
        g_cfg_vfs.emulator_dir.from_string(system_dir + "/rpcs3/");
        install_base = g_cfg_vfs.get(g_cfg_vfs.dev_hdd0, system_dir + "/rpcs3/") + "game/";
    }
    else
    {

        libretro_show_message("PKG installation failed: No install directory", 300);
        return false;
    }

    // Create install directory
    if (!fs::create_path(install_base))
    {
        libretro_show_message("PKG installation failed: Cannot create directory", 300);
        return false;
    }

    // Create deque for extraction - use emplace_back since package_reader is non-copyable
    std::deque<package_reader> readers;
    readers.emplace_back(pkg_path);

    // Validate the reader
    if (!readers.front().is_valid())
    {
        libretro_show_message("PKG installation failed: Invalid PKG file", 300);
        return false;
    }

    // Get PKG info
    const auto& header = readers.front().get_header();
    std::string title_id(header.title_id, strnlen(header.title_id, sizeof(header.title_id)));


    std::deque<std::string> bootable_paths;

    // Show progress updates during extraction
    char msg_buf[256];
    int last_progress = -1;

    // Start extraction in a separate thread so we can show progress
    std::atomic<bool> extraction_done{false};
    std::atomic<bool> extraction_success{false};
    std::thread extraction_thread([&]()
    {
        auto result = package_reader::extract_data(readers, bootable_paths, false);
        extraction_success = (result.error == package_install_result::error_type::no_error);
        extraction_done = true;
    });

    // Poll progress and show updates - show every 1% change for better feedback
    while (!extraction_done)
    {
        if (!readers.empty())
        {
            int progress = readers.front().get_progress(100);
            if (progress != last_progress)
            {
                snprintf(msg_buf, sizeof(msg_buf), "Installing PKG: %d%%", progress);
                libretro_show_message(msg_buf, 120);
                last_progress = progress;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    extraction_thread.join();

    if (!extraction_success)
    {

        libretro_show_message("PKG installation failed: Extraction error", 300);
        return false;
    }

    // Find the bootable EBOOT.BIN
    eboot_path.clear();
    if (!bootable_paths.empty())
    {
        eboot_path = bootable_paths.front();
    }
    else
    {
        // Try to find EBOOT.BIN in common locations
        std::vector<std::string> search_paths = {
            install_base + title_id + "/USRDIR/EBOOT.BIN",
            install_base + title_id + "/PS3_GAME/USRDIR/EBOOT.BIN",
        };

        for (const auto& path : search_paths)
        {
            if (fs::is_file(path))
            {
                eboot_path = path;
                break;
            }
        }
    }

    install_rap_files(pkg_path);
    return true;
}

static std::string get_option_value(const char* key, const char* default_val = "")
{
    if (!environ_cb)
        return default_val;
    retro_variable var{};
    var.key = key;
    var.value = nullptr;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        return var.value;
    return default_val;
}

// What the frontend was last told: the frame size, and the largest frame it
// made room for. Both start over whenever it asks for the AV info again.
static unsigned s_reported_width = 0;
static unsigned s_reported_height = 0;
static unsigned s_max_width = 0;
static unsigned s_max_height = 0;
// The aspect ratio the frontend is told: the Aspect Ratio option's, or what
// Stretch to Display Area names
static float s_display_aspect = 16.0f / 9.0f;

// VBlank Frequency "From List": vblank_unlock.txt in the core's folder of the
// system directory (embed_vblank_unlock.cmake) names the games whose frame
// rate doubles with a 120 Hz VBlank. The core writes the file when it is not
// there and never overwrites it, so it can be edited.
extern const unsigned char g_libretro_vblank_unlock[];
extern const std::size_t g_libretro_vblank_unlock_size;
static std::string game_serial(const std::string& path);

// The frames a second the core shows, when not the VBlank Frequency: 60 for a
// 30 fps game of the list, whose VBlank runs at 120. 0: the VBlank Frequency.
static u32 s_output_hz = 0;

static std::string vblank_unlock_path()
{
    return (system_dir.empty() ? fs::get_config_dir() : system_dir + "/rpcs3/") + "vblank_unlock.txt";
}

static void install_vblank_unlock()
{
    const std::string path = vblank_unlock_path();
    if (!g_libretro_vblank_unlock_size || fs::is_file(path))
        return;
    fs::create_path(fs::get_parent_dir(path));
    fs::file file(path, fs::rewrite);
    if (!file || file.write(g_libretro_vblank_unlock, g_libretro_vblank_unlock_size) != g_libretro_vblank_unlock_size)
    {
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: could not write %s\n", path.c_str());
    }
}

// The block of the list a title ID is in: 30, 60, or 0 when it is in neither
static u32 vblank_unlock_block(const std::string& serial)
{
    if (serial.empty())
        return 0;
    install_vblank_unlock();
    fs::file file(vblank_unlock_path());
    if (!file)
        return 0;

    u32 block = 0;
    std::istringstream lines(file.to_string());
    for (std::string line; std::getline(lines, line);)
    {
        if (const usz comment = line.find('#'); comment != umax)
            line.erase(comment);
        const usz first = line.find_first_not_of(" \t\r");
        if (first == umax)
            continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        if (line.front() == '[')
            block = line.starts_with("[30") ? 30 : line.starts_with("[60") ? 60 : 0;
        else if (block && line == serial)
            return block;
    }
    return 0;
}

// Disk Cache Size: RPCS3's own disk cache limit (dev_hdd1/caches, the oldest
// game's cache removed first), with Wipe on Exit as a limit of nothing.
// Standalone applies it once at its start; the core does when a game is
// loaded and again once it has stopped (NNshi).
static void libretro_limit_disk_cache()
{
    const std::string size = get_option_value("rpcs3_disk_cache_size", "1gb");
    g_cfg.vfs.limit_cache_size.set(size != "unlimited");
    g_cfg.vfs.cache_max_size.set(size == "1gb" ? 1024 : size == "wipe" ? 0 : 5120);
    if (g_cfg.vfs.limit_cache_size)
        rpcs3::cache::limit_cache_size();
}

static void libretro_apply_core_options()
{
    if (!environ_cb)
        return;

    // Only when the frontend has options to give. retro_set_environment()
    // applies them too, and RetroArch calls it again after it has brought up
    // the video driver, at a point where it answers no GET_VARIABLE at all -
    // every option then read as its default and overwrote what the user set:
    // the resolution scale went back to 100% moments after boot, which is why
    // no game ever came out scaled (NNshi). Nothing answered means nothing to
    // apply, not "apply the defaults".
    {
        retro_variable probe{"rpcs3_renderer", nullptr};
        if (!environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &probe) || !probe.value)
            return;
    }

    // ==================== CPU OPTIONS ====================
    // PPU Decoder
    std::string ppu_decoder = get_option_value("rpcs3_ppu_decoder", "llvm");
    if (ppu_decoder == "llvm")
        g_cfg.core.ppu_decoder.set(ppu_decoder_type::llvm);
    else
        g_cfg.core.ppu_decoder.set(ppu_decoder_type::_static);

    // SPU Decoder
    std::string spu_decoder = get_option_value("rpcs3_spu_decoder", "llvm");
    if (spu_decoder == "llvm")
        g_cfg.core.spu_decoder.set(spu_decoder_type::llvm);
    else if (spu_decoder == "asmjit")
        g_cfg.core.spu_decoder.set(spu_decoder_type::asmjit);
    else if (spu_decoder == "dynamic")
        g_cfg.core.spu_decoder.set(spu_decoder_type::dynamic);
    else
        g_cfg.core.spu_decoder.set(spu_decoder_type::_static);

    // SPU Block Size
    std::string spu_block = get_option_value("rpcs3_spu_block_size", "safe");
    if (spu_block == "mega")
        g_cfg.core.spu_block_size.set(spu_block_size_type::mega);
    else if (spu_block == "giga")
        g_cfg.core.spu_block_size.set(spu_block_size_type::giga);
    else
        g_cfg.core.spu_block_size.set(spu_block_size_type::safe);

    // Preferred SPU Threads
    std::string spu_threads = get_option_value("rpcs3_preferred_spu_threads", "0");
    g_cfg.core.preferred_spu_threads.set(std::stoi(spu_threads));

    // SPU Loop Detection
    g_cfg.core.spu_loop_detection.set(get_option_value("rpcs3_spu_loop_detection", "disabled") == "enabled");

    // SPU Cache
    g_cfg.core.spu_cache.set(get_option_value("rpcs3_spu_cache", "enabled") == "enabled");

    // LLVM Precompilation
    g_cfg.core.llvm_precompilation.set(get_option_value("rpcs3_llvm_precompilation", "enabled") == "enabled");

    // Accurate DFMA
    g_cfg.core.use_accurate_dfma.set(get_option_value("rpcs3_accurate_dfma", "enabled") == "enabled");

    // Clocks Scale
    std::string clocks = get_option_value("rpcs3_clocks_scale", "100");
    g_cfg.core.clocks_scale.set(std::stoi(clocks));

    // Max SPURS Threads
    std::string spurs = get_option_value("rpcs3_max_spurs_threads", "auto");
    if (spurs == "auto")
        g_cfg.core.max_spurs_threads.set(6);
    else
        g_cfg.core.max_spurs_threads.set(std::stoi(spurs));

    // ==================== GPU OPTIONS ====================
    // Default Resolution: what the game is told the display is. Only between
    // games - a game asks once, at boot, and the frame size the frontend was
    // given depends on it.
    if (Emu.IsStopped())
    {
        const std::string resolution = get_option_value("rpcs3_default_resolution", "720p");
        if (resolution == "1080p")
            g_cfg.video.resolution.set(video_resolution::_1080p);
        else if (resolution == "480p")
            g_cfg.video.resolution.set(video_resolution::_480p);
        else if (resolution == "576p")
            g_cfg.video.resolution.set(video_resolution::_576p);
        else
            g_cfg.video.resolution.set(video_resolution::_720p);
    }

    // Resolution Scale
    std::string res_scale = get_option_value("rpcs3_resolution_scale", "100");
    g_cfg.video.resolution_scale_percent.set(std::stoi(res_scale));

    // Resolution Scale Threshold
    g_cfg.video.min_scalable_dimension.set(std::stoi(get_option_value("rpcs3_scale_threshold", "16")));
    // No option for it: the frame goes to the frontend at its own size, so
    // nothing is scaled here and RPCS3's output scaling has nothing to do.
    // The frontend scales it to the screen, with its own filter (Video >
    // Scaling > Bilinear Filtering, or a shader).
    g_cfg.video.output_scaling.set(output_scaling_mode::bilinear);

    // ZCULL Accuracy, RPCS3's three settings as its own UI makes them
    const std::string zcull = get_option_value("rpcs3_zcull_accuracy", "precise");
    g_cfg.video.precise_zpass_count.set(zcull == "precise");
    g_cfg.video.relaxed_zcull_sync.set(zcull == "relaxed");

    // Frame Limit
    std::string limit = get_option_value("rpcs3_frame_limit", "auto");
    g_cfg.video.vsync.set(vsync_mode::off);  // Disable RPCS3 vsync, RetroArch controls timing

    // Auto is RPCS3's own default: at most one flip per PS3 refresh (the
    // VBlank Rate). Leaving the limiter off for it, as this used to, let
    // every game that does not wait for VBLANK itself run too fast (NNshi).
    // 144 and 240 have no frame_limit_type of their own; they go through the
    // second limit, which applies when the first one is unlimited.
    g_disable_frame_limit = false;
    g_cfg.video.second_frame_limit.set(0);

    if (limit == "off" || limit == "Off")
    {
        g_disable_frame_limit = true;
        g_cfg.video.frame_limit.set(frame_limit_type::none);
    }
    else if (limit == "ps3")
        g_cfg.video.frame_limit.set(frame_limit_type::_ps3);
    else if (limit == "30")
        g_cfg.video.frame_limit.set(frame_limit_type::_30);
    else if (limit == "50")
        g_cfg.video.frame_limit.set(frame_limit_type::_50);
    else if (limit == "60")
        g_cfg.video.frame_limit.set(frame_limit_type::_60);
    else if (limit == "120")
        g_cfg.video.frame_limit.set(frame_limit_type::_120);
    else if (limit == "144" || limit == "240")
    {
        g_cfg.video.frame_limit.set(frame_limit_type::infinite);
        g_cfg.video.second_frame_limit.set(std::stoi(limit));
    }
    else
        g_cfg.video.frame_limit.set(frame_limit_type::_auto);

    // Shader Mode
    std::string shader_mode = get_option_value("rpcs3_shader_mode", "async");
    if (shader_mode == "async_interpreter")
        g_cfg.video.shadermode.set(shader_mode::async_with_interpreter);
    else if (shader_mode == "interpreter")
        g_cfg.video.shadermode.set(shader_mode::interpreter_only);
    else if (shader_mode == "async" || shader_mode == "async_recompiler")
        g_cfg.video.shadermode.set(shader_mode::async_recompiler);
    else
        g_cfg.video.shadermode.set(shader_mode::recompiler);

    // Shader compiler threads. 0 lets RPCS3 pick from the CPU it sees; a fixed
    // count is here because that guess is made for a desktop where the emulator
    // owns the machine, and inside a frontend it does not.
    std::string shader_threads = get_option_value("rpcs3_shader_compiler_threads", "auto");
    if (shader_threads == "auto")
        g_cfg.video.shader_compiler_threads_count.set(0);
    else
        g_cfg.video.shader_compiler_threads_count.set(std::atoi(shader_threads.c_str()));

    // Anisotropic Filter
    std::string aniso = get_option_value("rpcs3_anisotropic_filter", "auto");
    if (aniso == "auto")
        g_cfg.video.anisotropic_level_override.set(0);
    else
        g_cfg.video.anisotropic_level_override.set(std::stoi(aniso));

    // Anti-Aliasing and Shader Quality were offered here but never applied
    g_cfg.video.antialiasing_level.set(get_option_value("rpcs3_msaa", "auto") == "disabled" ? msaa_level::none : msaa_level::_auto);

    const std::string shader_quality = get_option_value("rpcs3_shader_quality", "high");
    g_cfg.video.shader_precision.set(shader_quality == "low" ? gpu_preset_level::low :
        shader_quality == "ultra" ? gpu_preset_level::ultra : gpu_preset_level::high);

    g_cfg.video.disable_zcull_queries.set(get_option_value("rpcs3_disable_zcull_queries", "disabled") == "enabled");
    g_cfg.video.vblank_ntsc.set(get_option_value("rpcs3_vblank_ntsc", "enabled") == "enabled");
    g_cfg.core.rsx_accurate_res_access.set(get_option_value("rpcs3_accurate_rsx_reservation", "disabled") == "enabled");

    // These had core options since the port, which nothing ever read: setting
    // them changed nothing. Each goes to the RPCS3 setting it is named after.
    const auto enabled = [](const char* key, const char* def) { return get_option_value(key, def) == "enabled"; };

    g_cfg.core.spu_verification.set(enabled("rpcs3_spu_verification", "enabled"));
    g_cfg.core.accurate_cache_line_stores.set(enabled("rpcs3_spu_cache_line_stores", "disabled"));
    g_cfg.core.mfc_shuffling_in_steps.set(enabled("rpcs3_mfc_shuffling", "disabled"));
    g_cfg.core.spu_delay_penalty.set(std::clamp(std::atoi(get_option_value("rpcs3_spu_delay_penalty", "3").c_str()), 0, 16));
    g_cfg.core.ppu_use_nj_bit.set(enabled("rpcs3_ppu_nj_mode", "disabled"));
    g_cfg.core.ppu_set_sat_bit.set(enabled("rpcs3_ppu_set_sat_bit", "disabled"));
    g_cfg.core.ppu_set_vnan.set(enabled("rpcs3_ppu_accurate_vector_nan", "disabled"));
    g_cfg.core.ppu_set_fpcc.set(enabled("rpcs3_ppu_set_fpcc", "disabled"));
    g_cfg.core.hook_functions.set(enabled("rpcs3_hook_static_funcs", "disabled"));
    g_cfg.core.hle_lwmutex.set(enabled("rpcs3_hle_lwmutex", "disabled"));

    // The rest of RPCS3's settings dialog (NNshi's list)
    g_cfg.core.spu_accurate_dma.set(enabled("rpcs3_spu_accurate_dma", "disabled"));
    g_cfg.core.spu_accurate_reservations.set(enabled("rpcs3_spu_accurate_reservations", "enabled"));
    g_cfg.core.debug_console_mode.set(enabled("rpcs3_debug_console_mode", "disabled"));
    g_cfg.core.mfc_transfers_shuffling.set(enabled("rpcs3_mfc_delay_command", "disabled") ? 1 : 0);
    g_cfg.vfs.emulate_hdd_speed.set(enabled("rpcs3_emulate_hdd_speed", "disabled"));
    g_cfg.vfs.emulate_bdvd_speed.set(enabled("rpcs3_emulate_bdvd_speed", "disabled"));
    g_cfg.core.ppu_reservation_priority_over_spu.set(enabled("rpcs3_ppu_reservation_priority", "disabled"));
    g_cfg.video.host_label_synchronization.set(enabled("rpcs3_host_gpu_labels", "disabled"));
    g_cfg.video.emulate_depth_compare.set(enabled("rpcs3_emulate_depth_compare", "disabled"));
    g_cfg.video.force_hw_MSAA_resolve.set(enabled("rpcs3_force_hw_msaa_resolve", "disabled"));
    g_cfg.video.handle_tiled_memory.set(enabled("rpcs3_handle_tiled_memory", "disabled"));
    g_cfg.video.vk.use_rebar_upload_heap.set(enabled("rpcs3_use_rebar", "enabled"));
    g_cfg.core.max_cpu_preempt_count_per_frame.set(std::clamp(std::atoi(get_option_value("rpcs3_max_preempt_count", "0").c_str()), 0, 400));
    g_cfg.core.ppu_threads.set(std::clamp(std::atoi(get_option_value("rpcs3_ppu_threads", "2").c_str()), 1, 8));
    g_cfg.core.llvm_threads.set(std::clamp(std::atoi(get_option_value("rpcs3_llvm_threads", "0").c_str()), 0, 1024));
    {
        const std::string sched = get_option_value("rpcs3_thread_scheduler", "os");
        g_cfg.core.thread_scheduler.set(sched == "old" ? thread_scheduler_mode::old
            : sched == "alt" ? thread_scheduler_mode::alt : thread_scheduler_mode::os);
    }
    g_cfg.video.disable_FIFO_reordering.set(enabled("rpcs3_disable_fifo_reordering", "disabled"));
    // By what they do: RPCS3's dialog has these two the wrong way round, each
    // checkbox setting the other one's config entry
    g_cfg.video.disable_hardware_blending.set(enabled("rpcs3_disable_hw_blending", "disabled"));
    g_cfg.video.disable_hardware_texel_remapping.set(enabled("rpcs3_disable_hw_colorspace", "disabled"));
    g_cfg.video.use_gpu_texture_scaling.set(enabled("rpcs3_gpu_texture_scaling", "disabled"));
    g_cfg.core.spu_reservation_busy_waiting_enabled.set(enabled("rpcs3_spu_events_busy_loop", "disabled"));
    g_cfg.core.set_daz_and_ftz.set(enabled("rpcs3_set_daz_ftz", "disabled"));
    g_cfg.core.ppu_128_reservations_loop_max_length.set(std::clamp(std::atoi(get_option_value("rpcs3_accurate_ppu_128", "0").c_str()), -1, 14));
    {
        const std::string bias = get_option_value("rpcs3_fb_aliasing_bias", "auto");
        g_cfg.video.fb_aliasing_bias.set(bias == "color" ? framebuffer_aliasing_bias::prefer_color
            : bias == "depth" ? framebuffer_aliasing_bias::prefer_depth : framebuffer_aliasing_bias::_auto);
    }

    const std::string xfloat = get_option_value("rpcs3_spu_xfloat_accuracy", "approximate");
    g_cfg.core.spu_xfloat_accuracy.set(xfloat == "accurate" ? xfloat_accuracy::accurate :
        xfloat == "relaxed" ? xfloat_accuracy::relaxed :
        xfloat == "inaccurate" ? xfloat_accuracy::inaccurate : xfloat_accuracy::approximate);

    const std::string fifo = get_option_value("rpcs3_rsx_fifo_accuracy", "atomic");
    g_cfg.core.rsx_fifo_accuracy.set(fifo == "fast" ? rsx_fifo_mode::fast :
        fifo == "atomic_ordered" ? rsx_fifo_mode::atomic_ordered :
        fifo == "as_ps3" ? rsx_fifo_mode::as_ps3 : rsx_fifo_mode::atomic);

    // "Automatic" is RPCS3's own default, which depends on the host OS.
    const std::string sleep_timers = get_option_value("rpcs3_sleep_timers_accuracy", "auto");
    if (sleep_timers == "as_host")
        g_cfg.core.sleep_timers_accuracy.set(sleep_timers_accuracy_level::_as_host);
    else if (sleep_timers == "usleep")
        g_cfg.core.sleep_timers_accuracy.set(sleep_timers_accuracy_level::_usleep);
    else if (sleep_timers == "all_timers")
        g_cfg.core.sleep_timers_accuracy.set(sleep_timers_accuracy_level::_all_timers);
    else
        g_cfg.core.sleep_timers_accuracy.from_default();

    g_cfg.video.disable_vertex_cache.set(enabled("rpcs3_disable_vertex_cache", "disabled"));
    g_cfg.video.force_cpu_blit_processing.set(enabled("rpcs3_cpu_blit", "disabled"));
    g_cfg.video.disable_blit_engine_upscaling.set(enabled("rpcs3_disable_blit_upscaling", "disabled"));

    // The performance overlay is read when the RSX thread starts; a change
    // while a game runs is handed to it here.
    {
        const std::string level = get_option_value("rpcs3_perf_overlay", "disabled");
        const bool on = level != "disabled";
        const detail_level detail = level == "minimal" ? detail_level::minimal : level == "low" ? detail_level::low :
            level == "high" ? detail_level::high : detail_level::medium;
        const bool changed = g_cfg.video.perf_overlay.enabled.get() != on || (on && g_cfg.video.perf_overlay.level.get() != detail);
        g_cfg.video.perf_overlay.enabled.set(on);
        g_cfg.video.perf_overlay.level.set(detail);
        if (changed && !Emu.IsStopped())
            rsx::overlays::reset_performance_overlay();
    }
    {
        // Stretched, the picture fills what the frontend shows it in, and that
        // is the aspect ratio the core reports: a game patched for 21:9 needs
        // RetroArch to show it at 21:9 (NNshi). The frontend does not say what
        // its display is, so the option names it; "enabled" is 16:9, as before.
        // Not stretched, it is the aspect ratio the PS3 is set to, which a
        // game with a native 4:3 mode renders for. The frame handed over stays
        // 16:9 in pixels, and RPCS3 would fit a 4:3 picture into it with bars
        // at the sides, which RetroArch showing the frame at 4:3 then squeezed
        // a second time (NNshi): so with 4:3 the picture fills the frame, as
        // Stretch to Display Area does, and RetroArch gives it its shape.
        const bool four_by_three = get_option_value("rpcs3_aspect_ratio", "16:9") == "4:3";
        g_cfg.video.aspect_ratio.set(four_by_three ? video_aspect::_4_3 : video_aspect::_16_9);
        const std::string stretch = get_option_value("rpcs3_stretch_to_display", "disabled");
        g_cfg.video.stretch_to_display_area.set(stretch != "disabled" || four_by_three);
        float aspect = (four_by_three && stretch == "disabled") ? 4.0f / 3.0f : 16.0f / 9.0f;
        if (unsigned w = 0, h = 0; std::sscanf(stretch.c_str(), "%u:%u", &w, &h) == 2 && w && h)
            aspect = static_cast<float>(w) / static_cast<float>(h);
        if (aspect != s_display_aspect)
        {
            s_display_aspect = aspect;
            s_reported_width = s_reported_height = 0; // reported again with the next frame
        }
    }
    g_cfg.video.vk.asynchronous_texture_streaming.set(enabled("rpcs3_async_texture_streaming", "disabled"));
    // The option is in milliseconds, the setting in microseconds.
    g_cfg.video.driver_recovery_timeout.set(std::clamp(std::atoi(get_option_value("rpcs3_driver_recovery_timeout", "1000").c_str()), 0, 30000) * 1000);

    const std::string area = get_option_value("rpcs3_license_area", "usa");
    g_cfg.sys.license_area.set(area == "eu" ? CELL_SYSUTIL_LICENSE_AREA_E :
        area == "jp" ? CELL_SYSUTIL_LICENSE_AREA_J :
        area == "hk" ? CELL_SYSUTIL_LICENSE_AREA_H :
        area == "kr" ? CELL_SYSUTIL_LICENSE_AREA_K :
        area == "cn" ? CELL_SYSUTIL_LICENSE_AREA_C : CELL_SYSUTIL_LICENSE_AREA_A);

    g_cfg.misc.show_shader_compilation_hint.set(enabled("rpcs3_show_shader_compilation_hint", "disabled"));
    g_cfg.misc.show_ppu_compilation_hint.set(enabled("rpcs3_show_ppu_compilation_hint", "disabled"));
    g_cfg.misc.silence_all_logs.set(enabled("rpcs3_silence_all_logs", "disabled"));
    g_cfg.core.spu_getllar_spin_optimization_disabled.set(get_option_value("rpcs3_disable_getllar_spin_opt", "disabled") == "enabled");

    // Write Color Buffers
    g_cfg.video.write_color_buffers.set(get_option_value("rpcs3_write_color_buffers", "disabled") == "enabled");

    // Read Color Buffers
    g_cfg.video.read_color_buffers.set(get_option_value("rpcs3_read_color_buffers", "disabled") == "enabled");

    // Read Depth Buffers
    g_cfg.video.read_depth_buffer.set(get_option_value("rpcs3_read_depth_buffers", "disabled") == "enabled");

    // Write Depth Buffers
    g_cfg.video.write_depth_buffer.set(get_option_value("rpcs3_write_depth_buffers", "disabled") == "enabled");

    // Strict Rendering
    g_cfg.video.strict_rendering_mode.set(get_option_value("rpcs3_strict_rendering", "disabled") == "enabled");

    // Multithreaded RSX
    g_cfg.video.multithreaded_rsx.set(get_option_value("rpcs3_multithreaded_rsx", "disabled") == "enabled");

    // VBlank Rate. From List: 120 Hz for a game of vblank_unlock.txt - with
    // the core still at 60 frames a second for a 30 fps game, which then runs
    // at 60 - and 60 Hz for any other (NNshi)
    std::string vblank = get_option_value("rpcs3_vblank_rate", "60");
    s_output_hz = 0;
    if (vblank == "list")
    {
        const std::string serial = game_serial(game_path);
        const u32 block = vblank_unlock_block(serial);
        vblank = block ? "120" : "60";
        if (block == 30)
            s_output_hz = 60;
        static std::string s_logged;
        if (log_cb && !serial.empty() && s_logged != serial)
        {
            s_logged = serial;
            if (block)
                log_cb(RETRO_LOG_INFO, "RPCS3: VBlank Frequency from the list: %s is a %u fps game, VBlank at 120 Hz, %u frames a second shown\n",
                    serial.c_str(), block, block * 2);
            else
                log_cb(RETRO_LOG_INFO, "RPCS3: VBlank Frequency from the list: %s is not in vblank_unlock.txt, 60 Hz\n", serial.c_str());
        }
    }
    g_cfg.video.vblank_rate.set(std::stoi(vblank));

    g_libretro_frontend_vblank = get_option_value("rpcs3_frame_pacing", "emulator") == "frontend";

    // Immediate: the readback path is only the fallback without the
    // frontend's Vulkan device now, and no longer offered as an option.
    g_libretro_deferred_readback = false;

    // Driver Wake-Up Delay
    std::string driver_delay = get_option_value("rpcs3_driver_wakeup_delay", "0");
    g_cfg.video.driver_wakeup_delay.set(std::stoi(driver_delay));

    // ==================== AUDIO ====================
    // cellAudio's own buffer is off and not an option: RetroArch keeps the
    // buffer, and cellAudio's on top of it is only latency the pictures do
    // not have (NNshi, Project Diva). RetroArch's dynamic rate control keeps
    // the stream in step with what it plays, so the core has no time
    // stretching of its own.
    g_cfg.audio.enable_buffering.set(false);

    // Audio Format: the speakers the PS3 reports, which decides the mix a game
    // makes. A core cannot ask RetroArch for its Output Layout, and offering
    // everything left RetroArch folding 7.1 down even on stereo, which is not
    // always what the game's own stereo mix sounds like (NNshi). Surround
    // needs the frontend's multi-channel output, negotiated at load; a 7.1
    // setup takes 5.1 games too, as a PS3's does.
    const std::string audio_fmt = get_option_value("rpcs3_audio_format", "stereo");
    if (libretro_audio_multi_available() && audio_fmt == "surround_7_1")
    {
        g_cfg.audio.format.set(audio_format::manual);
        g_cfg.audio.formats.set(static_cast<u32>(audio_format_flag::lpcm_5_1_48khz) | static_cast<u32>(audio_format_flag::lpcm_7_1_48khz));
        g_cfg.audio.channel_layout.set(audio_channel_layout::surround_7_1);
    }
    else if (libretro_audio_multi_available() && audio_fmt == "surround_5_1")
    {
        g_cfg.audio.format.set(audio_format::manual);
        g_cfg.audio.formats.set(static_cast<u32>(audio_format_flag::lpcm_5_1_48khz));
        g_cfg.audio.channel_layout.set(audio_channel_layout::surround_5_1);
    }
    else
    {
        g_cfg.audio.format.set(audio_format::stereo);
        g_cfg.audio.channel_layout.set(audio_channel_layout::stereo);
    }

    // Master Volume
    std::string volume = get_option_value("rpcs3_master_volume", "100");
    g_cfg.audio.volume.set(std::stoi(volume));

    // ==================== NETWORK OPTIONS ====================
    // None of these were read before: the network stayed off whatever they
    // said. The connection is set up when a game boots, so only between games.
    if (Emu.IsStopped())
    {
        g_cfg.net.net_active.set(get_option_value("rpcs3_network_enabled", "disabled") == "enabled"
            ? np_internet_status::enabled : np_internet_status::disabled);
        const std::string psn = get_option_value("rpcs3_psn_status", "disabled");
        g_cfg.net.psn_status.set(psn == "simulated" ? np_psn_status::psn_fake
            : psn == "rpcn" ? np_psn_status::psn_rpcn : np_psn_status::disabled);
        g_cfg.net.upnp_enabled.set(get_option_value("rpcs3_upnp", "disabled") == "enabled");
        g_cfg.net.dns.from_string(get_option_value("rpcs3_dns", "8.8.8.8"));
    }
    g_cfg.misc.show_rpcn_popups.set(get_option_value("rpcs3_show_rpcn_popups", "enabled") == "enabled");
    g_cfg.misc.show_trophy_popups.set(get_option_value("rpcs3_show_trophy_popups", "enabled") == "enabled");

    // ==================== SYSTEM/CORE OPTIONS ====================
    // System Language
    std::string lang = get_option_value("rpcs3_language", "english");
    if (lang == "japanese") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_JAPANESE);
    else if (lang == "french") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_FRENCH);
    else if (lang == "spanish") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_SPANISH);
    else if (lang == "german") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_GERMAN);
    else if (lang == "italian") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_ITALIAN);
    else if (lang == "dutch") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_DUTCH);
    else if (lang == "portuguese") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_PORTUGUESE_PT);
    else if (lang == "russian") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_RUSSIAN);
    else if (lang == "korean") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_KOREAN);
    else if (lang == "chinese_trad") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_CHINESE_T);
    else if (lang == "chinese_simp") g_cfg.sys.language.set(CELL_SYSUTIL_LANG_CHINESE_S);
    else g_cfg.sys.language.set(CELL_SYSUTIL_LANG_ENGLISH_US);

    // Enter Button Assignment
    std::string enter_btn = get_option_value("rpcs3_enter_button", "cross");
    g_cfg.sys.enter_button_assignment.set(enter_btn == "circle" ? enter_button_assign::circle : enter_button_assign::cross);

    // USIO Mode: what the System 357/369 arcade board reports its buttons as,
    // read on every poll of the board. Shooter games also have the ports
    // trade places (libretro_input_set_usio_shooter).
    const bool usio_shooter = get_option_value("rpcs3_usio_mode", "fighting") == "shooter";
    g_cfg.io.usio_mode.set(usio_shooter ? usio_handler_mode::shooter_games : usio_handler_mode::fighting_games);
    libretro_input_set_usio_shooter(usio_shooter);
    // A mouse per player, as LibretroMouseHandler has: with the default
    // (basic, one mouse) the USIO board reads every player's gun from mouse 1
    g_cfg.io.mouse.set(mouse_handler::raw);

    // Avoid additional CPU-throttling heuristics
    g_cfg.core.max_cpu_preempt_count_per_frame.set(0);

}



// Forward declarations
static void init_emu_callbacks();
static void context_reset();
static void context_destroy();

static void libretro_apply_core_options();

#ifdef _WIN32
static void* g_lrcore_vectored_handler = nullptr;

static std::string lrcore_build_crash_log_path()
{
    std::string base;
    if (!save_dir.empty())
    {
        base = save_dir;
    }
    else if (!system_dir.empty())
    {
        base = system_dir;
    }
    else
    {
        base = ".";
    }
    return base + "/rpcs3_libretro_crash.log";
}

static void lrcore_write_crash_report(const std::string& header, const std::vector<std::string>& lines)
{
    const std::string path = lrcore_build_crash_log_path();
    const std::string dir = fs::get_parent_dir(path, 1);
    if (!dir.empty())
    {
        fs::create_path(dir);
    }
    std::string text;
    text.reserve(header.size() + 1 + lines.size() * 64);
    text += header;
    text += '\n';
    for (const auto& line : lines)
    {
        text += line;
        text += '\n';
    }
    fs::file f(path, fs::rewrite);
    if (f)
    {
        f.write(text);
        f.sync();
    }
}

static bool lrcore_is_fatal_exception(DWORD code)
{
    // NOTE: EXCEPTION_ACCESS_VIOLATION (0xc0000005) is NOT fatal in RPCS3!
    // It's expected during normal operation for VM memory mapping/signal handling.
    // Only log truly fatal exceptions that RPCS3 can't recover from.
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:         // 0xC0000005 - Expected for VM memory access
    case EXCEPTION_IN_PAGE_ERROR:            // 0xC0000006 - Expected for VM paging
        return false;  // Let RPCS3's internal handlers deal with these
    case EXCEPTION_STACK_OVERFLOW:           // 0xC00000FD
    case EXCEPTION_ILLEGAL_INSTRUCTION:      // 0xC000001D
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       // 0xC0000094
    case EXCEPTION_INT_OVERFLOW:             // 0xC0000095
    case EXCEPTION_PRIV_INSTRUCTION:         // 0xC0000096
    case EXCEPTION_INVALID_HANDLE:           // 0xC0000008
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    // 0xC000008C
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       // 0xC000008E
    case EXCEPTION_FLT_OVERFLOW:             // 0xC0000091
    case EXCEPTION_FLT_STACK_CHECK:          // 0xC0000092
    case EXCEPTION_FLT_UNDERFLOW:            // 0xC0000093
        return true;
    default:
        return false;
    }
}

static LONG CALLBACK lrcore_vectored_exception_handler(PEXCEPTION_POINTERS info)
{
    if (!info || !info->ExceptionRecord || !info->ContextRecord)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (!lrcore_is_fatal_exception(code))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const void* address = info->ExceptionRecord->ExceptionAddress;

    const auto stack = utils::get_backtrace_from_context(info->ContextRecord, 256);
    const auto lines = utils::get_backtrace_symbols(stack);
    for (usz i = 0; i < lines.size(); ++i)
    {

    }
    const std::string header = fmt::format("RPCS3 libretro crash: code=0x%08lx address=%p", static_cast<unsigned long>(code), address);
    lrcore_write_crash_report(header, lines);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void lrcore_install_crash_handler()
{
    if (g_lrcore_vectored_handler)
    {
        return;
    }
    g_lrcore_vectored_handler = AddVectoredExceptionHandler(1, lrcore_vectored_exception_handler);

}

static void lrcore_uninstall_crash_handler()
{

    if (g_lrcore_vectored_handler)
    {
        RemoveVectoredExceptionHandler(g_lrcore_vectored_handler);
        g_lrcore_vectored_handler = nullptr;
    }
}
#endif

namespace
{
    struct libretro_log_listener : public logs::listener
    {
        void log(u64 /*stamp*/, const logs::message& /*msg*/, std::string_view /*prefix*/, std::string_view /*text*/) override
        {
            // Logging disabled
        }
    };

    libretro_log_listener g_libretro_logs;
}

// The log listeners retro_init installs. They are taken out again in
// retro_deinit: the file logger owns the "Log Writer" thread, and a thread
// left running into a frontend's FreeLibrary wakes up in code that is no
// longer mapped - on Windows, about a second after closing content.
static bool s_logs_hooked = false;
static std::unique_ptr<logs::listener> s_file_logger;

// Save data lists. A PS3 game asks the system to show its save list and waits
// for the player's pick; RPCS3 answers with a Qt dialog the core does not have,
// and without one every list operation was cancelled, so games could not save
// or load (NNshi). The list is RPCS3's own in-game one, drawn into the picture
// like the PS3's and driven with the pad. Only when that cannot be shown does
// the core pick for the player: the first entry of the list the game asked
// for - overwriting it when saving, or making a new save when the list is
// empty and the game allows one; loading it, or nothing when the list is
// empty. Nothing is ever picked for deletion. The "save / load this data?"
// confirmation that follows is answered yes (g_cellsavedata_auto_confirm).
extern atomic_t<bool> g_cellsavedata_auto_confirm;

namespace
{
    class libretro_save_dialog : public SaveDialogBase
    {
    public:
        s32 ShowSaveDataList(const std::string& base_dir, std::vector<SaveDataEntry>& save_entries, s32 focused, u32 op, vm::ptr<CellSaveDataListSet> listSet, bool enable_overlay) override
        {
            constexpr u32 op_list_save = 4, op_list_load = 5, op_list_auto_save = 2, op_list_auto_load = 3;
            const s32 count = static_cast<s32>(save_entries.size());

            // What standalone RPCS3 does with its native interface enabled
            if (auto manager = g_fxo->try_get<rsx::overlays::display_manager>())
            {
                const bool use_end = sysutil_send_system_cmd(CELL_SYSUTIL_DRAWING_BEGIN, 0) >= 0;
                const s32 result = manager->create<rsx::overlays::save_dialog>()->show(base_dir, save_entries, focused, op, listSet, enable_overlay);
                if (use_end)
                    sysutil_send_system_cmd(CELL_SYSUTIL_DRAWING_END, 0);
                if (result != rsx::overlays::user_interface::selection_code::error)
                    return result;
            }

            if (op != op_list_save && op != op_list_load && op != op_list_auto_save && op != op_list_auto_load)
                return -2; // deleting needs the list, and it could not be shown

            const bool saving = op == op_list_save || op == op_list_auto_save;
            const bool loading = op == op_list_load || op == op_list_auto_load;
            s32 pick = -2; // cancel

            if (saving)
            {
                if (count > 0)
                    pick = 0;
                else if (listSet && listSet->newData)
                    pick = -1; // new save
            }
            else if (loading && count > 0)
            {
                pick = 0;
            }

            if (log_cb)
                log_cb(RETRO_LOG_INFO, "RPCS3: save data list could not be shown (%s, %d entr%s): %s\n",
                    saving ? "save" : loading ? "load" : "other", count, count == 1 ? "y" : "ies",
                    pick == -2 ? "nothing picked" : pick == -1 ? "new save" : "the first entry");
            return pick;
        }
    };
}

// RPCS3's in-game overlays look for their icons in the config dir first, and
// standalone copies them there from next to the executable. The core carries
// them itself; put them where they are looked for.
// Whether the file at path holds exactly these bytes. The size alone is not
// enough: a newer database or patch file can have the same size (sco). Only
// a file of the right size is read, once per start, which costs far less
// than writing it again every time.
static bool file_has_contents(const std::string &path, const void *data, std::size_t size)
{
    fs::stat_t info{};
    if (!fs::get_stat(path, info) || info.size != size)
        return false;
    fs::file file(path);
    if (!file)
        return false;
    std::vector<u8> current(size);
    return file.read(current.data(), size) == size && std::memcmp(current.data(), data, size) == 0;
}

static void install_ui_icons()
{
    const std::string dir = fs::get_config_dir() + "Icons/ui/";
    if (!fs::create_path(dir))
    {
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: could not create %s, in-game dialogs will lack their icons\n", dir.c_str());
        return;
    }

    for (std::size_t i = 0; i < g_libretro_ui_icon_count; i++)
    {
        const libretro_ui_icon& icon = g_libretro_ui_icons[i];
        const std::string path = dir + icon.name;

        // The home menu's are in a subdirectory (home/32/).
        if (const std::string parent = fs::get_parent_dir(path); !fs::is_dir(parent))
            fs::create_path(parent);
        if (file_has_contents(path, icon.data, icon.size))
            continue;

        if (!fs::write_file(path, fs::rewrite, icon.data, icon.size) && log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: could not write %s\n", path.c_str());
    }
}

// RPCS3's per-game settings database (embed_config_database.cmake). The copy in
// GuiConfigs is standalone's file, so a standalone pointed at the same folder
// sees it too; it is written when it is missing or older than the one in the
// core, by the "timestamp" both carry at their top. One of the same date or
// newer is left alone, so entries edited by hand stay (NNshi).
extern const unsigned char g_libretro_config_database[];
extern const std::size_t g_libretro_config_database_size;

static std::string config_database_path()
{
    return fs::get_config_dir() + "GuiConfigs/config_database.dat";
}

// The "timestamp" at the top of a database, or nothing when there is none
static std::optional<u64> config_database_timestamp(std::string_view head)
{
    static constexpr std::string_view key = "\"timestamp\"";
    usz pos = head.find(key);
    if (pos == umax)
        return std::nullopt;
    pos = head.find_first_of("0123456789", pos + key.size());
    if (pos == umax)
        return std::nullopt;
    u64 value = 0;
    for (; pos < head.size() && head[pos] >= '0' && head[pos] <= '9'; pos++)
        value = value * 10 + (head[pos] - '0');
    return value;
}

static void install_config_database()
{
    if (!g_libretro_config_database_size)
        return;
    const std::string path = config_database_path();
    // The timestamp is in the first lines of the file
    constexpr usz head_size = 512;
    const auto own = config_database_timestamp({reinterpret_cast<const char*>(g_libretro_config_database), std::min<usz>(g_libretro_config_database_size, head_size)});
    if (fs::file current{path})
    {
        std::string head(std::min<u64>(current.size(), head_size), '\0');
        head.resize(current.read(head.data(), head.size()));
        const auto present = config_database_timestamp(head);
        if (present && own && *present >= *own)
            return;
    }
    fs::create_path(fs::get_parent_dir(path));
    // Pointer and size: fs::write_file takes the size of an array argument, and
    // this one's is only known at link time
    fs::file file(path, fs::rewrite);
    if (!file || file.write(g_libretro_config_database, g_libretro_config_database_size) != g_libretro_config_database_size)
    {
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: could not write %s\n", path.c_str());
    }
}

// RPCS3's game patches (embed_patches.cmake), written to patches/patch.yml in
// the config dir as standalone's patch manager does, when missing or of
// another size. Which patches are on is patch_config.yml's, left alone.
extern const unsigned char g_libretro_patch_yml[];
extern const std::size_t g_libretro_patch_yml_size;

static void install_patches()
{
    if (!g_libretro_patch_yml_size)
        return;
    const std::string path = fs::get_config_dir() + "patches/patch.yml";
    if (file_has_contents(path, g_libretro_patch_yml, g_libretro_patch_yml_size))
        return;
    fs::create_path(fs::get_parent_dir(path));
    fs::file file(path, fs::rewrite);
    if (!file || file.write(g_libretro_patch_yml, g_libretro_patch_yml_size) != g_libretro_patch_yml_size)
    {
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: could not write %s\n", path.c_str());
    }
}

// The game's entry, as RPCS3's YAML config, or nothing. The file is JSON,
// read with libretro-common's rjson in one pass that stops at the entry: the
// "config" string of games -> <title id>.
static std::string s_database_config;

namespace
{
    struct database_lookup
    {
        std::string_view title_id;
        std::string_view keys[4]; // the member names on the way down
        unsigned depth = 0;       // objects and arrays open
        bool found = false;
        std::string* out;
    };
}

static std::string lookup_database_config(const std::string& title_id)
{
    s_database_config.clear();
    if (title_id.empty() || get_option_value("rpcs3_database_override", "enabled") != "enabled")
        return {};
    fs::file file(config_database_path());
    if (!file)
        return {};
    const std::string text = file.to_string();

    database_lookup lookup{.title_id = title_id, .out = &s_database_config};
    rjson_parse_quick(text.data(), text.size(), &lookup, RJSON_OPTION_ALLOW_UTF8BOM,
        [](void* ctx, const char* str, size_t len) // member name
        {
            auto& l = *static_cast<database_lookup*>(ctx);
            if (l.depth < 4)
                l.keys[l.depth] = std::string_view(str, len);
            return true;
        },
        [](void* ctx, const char* str, size_t len) // string
        {
            auto& l = *static_cast<database_lookup*>(ctx);
            if (l.depth == 3 && l.keys[1] == "games" && l.keys[2] == l.title_id && l.keys[3] == "config")
            {
                l.out->assign(str, len);
                l.found = true;
                return false; // done
            }
            return true;
        },
        nullptr,
        [](void* ctx) { auto& l = *static_cast<database_lookup*>(ctx); l.depth++; if (l.depth < 4) l.keys[l.depth] = {}; return true; },
        [](void* ctx) { static_cast<database_lookup*>(ctx)->depth--; return true; },
        [](void* ctx) { auto& l = *static_cast<database_lookup*>(ctx); l.depth++; if (l.depth < 4) l.keys[l.depth] = {}; return true; },
        [](void* ctx) { static_cast<database_lookup*>(ctx)->depth--; return true; },
        nullptr, nullptr,
        [](void* ctx, int line, int col, const char* err) // error, or "" when stopped at the entry
        {
            auto& l = *static_cast<database_lookup*>(ctx);
            if (!l.found && err && *err && log_cb)
                log_cb(RETRO_LOG_WARN, "RPCS3: config database is not readable: line %d, column %d: %s\n", line, col, err);
        });

    if (!lookup.found)
        return {};
    if (log_cb)
        log_cb(RETRO_LOG_INFO, "RPCS3: database settings for %s:\n%s\n", title_id.c_str(), s_database_config.c_str());
    return s_database_config;
}

static std::string find_firmware_pup()
{
    if (system_dir.empty())
        return {};

    // The name as Sony serves it is PS3UPDAT.PUP, but a copy can come in
    // other case (PS3UPDAT.pup, demifiend9), and Linux and Android file
    // systems tell them apart: any case will do
    for (const std::string dir : { system_dir + "/rpcs3", system_dir, system_dir + "/rpcs3/firmware" })
    {
        for (auto&& entry : fs::dir(dir))
        {
            if (entry.is_directory)
                continue;
            std::string name = entry.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name == "ps3updat.pup")
                return dir + "/" + entry.name;
        }
    }

    return {};
}

// On ARM CPUs with efficiency cores - most phones, and ARM laptops like
// Snapdragon Chromebooks - RPCS3's Auto compiles on every core, the slow
// little ones included, which makes a first boot slower than it need be and
// heats the device for nothing (NNshi). There the compiler threads default to
// the number of the other cores. Only the efficiency cores are left out, not
// every core below the fastest: a Dimensity 9600 (2 prime + 3 performance + 3
// efficiency) gets 5, a Snapdragon 8 Elite (2 prime + 6 performance, no
// efficiency cores) keeps Auto. A core is an efficiency core when the kernel
// rates it below 60 % of the fastest core's capacity (cpu_capacity, which ARM
// kernels have from their energy model), or, without that, when its top clock
// is below 75 % of the fastest's. CPUs without efficiency cores, and x86, keep
// Auto, and so does anyone who has set the options already.
static void libretro_big_little_defaults()
{
#if defined(__aarch64__) && !defined(_WIN32) && !defined(__APPLE__)
    std::vector<u64> capacity, max_freq;
    for (u32 cpu = 0; cpu < 256; cpu++)
    {
        const std::string dir = fmt::format("/sys/devices/system/cpu/cpu{}", cpu);
        if (!fs::is_dir(dir))
            break;
        const auto read = [&](const char *name) -> u64 {
            fs::file f(dir + name);
            return f ? std::strtoull(f.to_string().c_str(), nullptr, 10) : 0;
        };
        capacity.push_back(read("/cpu_capacity"));
        max_freq.push_back(read("/cpufreq/cpuinfo_max_freq"));
    }
    if (capacity.size() < 2)
        return;

    const bool by_capacity = std::find(capacity.begin(), capacity.end(), 0) == capacity.end();
    const std::vector<u64> &rating = by_capacity ? capacity : max_freq;
    if (std::find(rating.begin(), rating.end(), 0) != rating.end())
        return;
    const u64 top = *std::max_element(rating.begin(), rating.end());
    const u64 threshold = top * (by_capacity ? 60 : 75) / 100;
    const usz fast = std::count_if(rating.begin(), rating.end(), [&](u64 r) { return r >= threshold; });
    if (fast == rating.size())
        return;

    // The largest of the option's values that is not more than those cores
    static std::string value;
    for (const char *v : { "8", "6", "4", "3", "2", "1" })
    {
        if (static_cast<usz>(std::atoi(v)) <= fast)
        {
            value = v;
            break;
        }
    }
    for (retro_core_option_v2_definition &def : option_defs_us)
    {
        if (def.key && (!std::strcmp(def.key, "rpcs3_llvm_threads") || !std::strcmp(def.key, "rpcs3_shader_compiler_threads")))
            def.default_value = value.c_str();
    }
    if (log_cb)
        log_cb(RETRO_LOG_INFO, "RPCS3: %zu of %zu cores are not efficiency cores (by %s), compiler threads default to %s\n",
            fast, rating.size(), by_capacity ? "cpu_capacity" : "top clock", value.c_str());
#endif
}

// The options from libretro_core_options.h, then the loaded game's patches
// (libretro_patch_manager.cpp) in a category of their own. Declared again once the
// game is known; RetroArch keeps what the .opt file says for each key.
static bool RETRO_CALLCONV libretro_update_options_display()
{
    libretro_patches::update_display(environ_cb, [](const char* key) { return get_option_value(key, "disabled"); });
    return true;
}

static void publish_core_options(retro_environment_t cb)
{
    static const std::vector<retro_core_option_v2_definition> fixed = [] {
        std::vector<retro_core_option_v2_definition> defs;
        for (const auto& def : option_defs_us)
        {
            if (!def.key)
                break;
            defs.push_back(def);
        }
        return defs;
    }();
    static const std::vector<retro_core_option_v2_category> fixed_categories = [] {
        std::vector<retro_core_option_v2_category> cats;
        for (const auto& cat : option_cats_us)
        {
            if (!cat.key)
                break;
            cats.push_back(cat);
        }
        return cats;
    }();

    static std::vector<retro_core_option_v2_definition> all;
    static std::vector<retro_core_option_v2_category> categories;
    all = fixed;
    categories = fixed_categories;
    if (libretro_patches::any())
    {
        const auto& patches = libretro_patches::definitions();
        all.insert(all.end(), patches.begin(), patches.end());
        categories.push_back({"patches", "Patch Manager", "The game's patches from RPCS3's patch files, patch.yml and imported_patch.yml. A change applies at the next start of the game."});
    }
    all.push_back({});
    categories.push_back({});
    options_us.definitions = all.data();
    options_us.categories = categories.data();

    bool categories_supported = false;
    libretro_set_core_options(cb, &categories_supported);

    // Out of the menu, as RPCS3 keeps them out of its settings dialog; the
    // .opt file still sets them (the HIDDEN block in libretro_core_options.h)
    for (const char* key : {"rpcs3_spu_cache", "rpcs3_accurate_dfma", "rpcs3_spu_verification",
            "rpcs3_driver_recovery_timeout", "rpcs3_mfc_shuffling", "rpcs3_spu_delay_penalty",
            "rpcs3_vblank_ntsc", "rpcs3_hle_lwmutex"})
    {
        struct retro_core_option_display display{key, false};
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &display);
    }

    if (libretro_patches::any())
    {
        struct retro_core_options_update_display_callback update_display{libretro_update_options_display};
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &update_display);
        libretro_patches::update_display(cb, [](const char* key) { return get_option_value(key, "disabled"); });
    }
}

// The serial of the game in a folder (a disc dump's root or PS3_GAME, an HDD
// game's folder) or of the EBOOT.BIN in it, from its PARAM.SFO
static std::string game_serial(const std::string& path)
{
    std::string dir = fs::is_dir(path) ? path : fs::get_parent_dir(path);
    for (int i = 0; i < 4 && !dir.empty(); i++)
    {
        for (const std::string& sfo : {dir + "/PARAM.SFO", dir + "/PS3_GAME/PARAM.SFO"})
        {
            if (!fs::is_file(sfo))
                continue;
            const std::string serial = std::string(psf::get_string(psf::load_object(sfo), "TITLE_ID"));
            if (!serial.empty())
                return serial;
        }
        dir = fs::get_parent_dir(dir);
    }
    return {};
}

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    // Get log interface
    struct retro_log_callback logging;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
    {
        log_cb = logging.log;
    }

    // Request VFS interface (API v3 for directory operations)
    struct retro_vfs_interface_info vfs_info;
    vfs_info.required_interface_version = 3;
    vfs_info.iface = nullptr;

    if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_info) && vfs_info.iface)
    {
        libretro_vfs::set_vfs_interface(vfs_info.iface);

    }
    else
    {
        // Only log on first call - retro_set_environment may be called multiple times

    }

    // The definitions, and the translations RetroArch picks from by its
    // language, live in libretro_core_options.h, laid out the way
    // libretro's Crowdin scripts read them. Frontends without v2 get v1 or
    // the flat v0 list generated from the same definitions.
    libretro_big_little_defaults();
    publish_core_options(cb);

    // We don't support no-game
    bool support_no_game = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &support_no_game);

    // Set up input descriptors for PS3 controller mappings
    // This tells RetroArch what buttons map to which PS3 buttons
    libretro_input_set_descriptors(cb);

    // Set up controller info so RetroArch knows what controllers we support
    libretro_input_set_controller_info(cb);

    // Enable joypad bitmasks if supported by frontend (more reliable button polling)
    bool bitmasks_supported = false;
    if (cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, &bitmasks_supported))
    {
        libretro_input_set_bitmask_supported(bitmasks_supported);

    }

    if (!libretro_input_init_rumble(cb))
    {
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: the frontend has no rumble interface, so the controller will not vibrate\n");
    }

    // Initialize sensor interface for gyro/accelerometer support
    if (libretro_input_init_sensors(cb))
    {
    }
    else
    {
    }



    // Apply defaults for core options early
    libretro_apply_core_options();
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info* info)
{
    info->library_name = "RPCS3";
    // The upstream RPCS3 build this core is merged up to, from upstream.version
    info->library_version = RPCS3_LIBRETRO_VERSION;
    info->valid_extensions = "bin|self|elf|pkg|iso|sfo|sfb";
    // The path, never the bytes. retro_load_game() reads game->path and hands
    // it to RPCS3, which opens the file itself - it never looks at game->data.
    // With need_fullpath false the frontend loads the whole file into memory
    // first and that buffer is then dropped unread, which on a disc image is
    // the size of the image: a 20 GB ISO showed up as 20 GB of private memory
    // in RetroArch, read off the user's NAS for nothing.
    info->need_fullpath = true;
    info->block_extract = false;
}

// The renderer draws at the game's resolution times the Resolution Scale
// option, and hands the frontend frames of that size. The base size here is
// the Default Resolution's, which is what most games draw at; the real one is
// reported with the first frame. The maximum has to cover a 1080p game at the scale in use, or
// the frontend is told of frames larger than it made room for.
static unsigned scaled_dimension(unsigned native)
{
    return static_cast<unsigned>(static_cast<u64>(native) * g_cfg.video.resolution_scale_percent.get() / 100);
}


static void fill_av_info(retro_system_av_info* info)
{
    unsigned width = 1280, height = 720;
    switch (g_cfg.video.resolution.get())
    {
    case video_resolution::_1080p: width = 1920; height = 1080; break;
    case video_resolution::_480p: width = 720; height = 480; break;
    case video_resolution::_576p: width = 720; height = 576; break;
    default: break;
    }
    info->geometry.base_width = scaled_dimension(width);
    info->geometry.base_height = scaled_dimension(height);
    info->geometry.max_width = std::max(3840u, scaled_dimension(1920));
    info->geometry.max_height = std::max(2160u, scaled_dimension(1080));
    info->geometry.aspect_ratio = s_display_aspect;
    // The PS3's refresh rate. A 30 fps game of the VBlank Frequency list has
    // its VBLANK at 120 Hz and is shown at 60 (s_output_hz).
    const double vblank_period = 1'000'000.0 + g_cfg.video.vblank_ntsc.get() * 1000.0;
    info->timing.fps = (s_output_hz ? s_output_hz : g_cfg.video.vblank_rate.get()) * 1'000'000.0 / vblank_period;
    info->timing.sample_rate = 48000.0;
}

void retro_get_system_av_info(struct retro_system_av_info* info)
{
    fill_av_info(info);
    s_reported_width = info->geometry.base_width;
    s_reported_height = info->geometry.base_height;
    s_max_width = info->geometry.max_width;
    s_max_height = info->geometry.max_height;
}

// Tells the frontend the size of the frames it is now getting, once per
// change. Without it the frontend keeps the size from
// retro_get_system_av_info() - 1280x720 whatever the resolution scale.
static void report_frame_size(unsigned width, unsigned height)
{
    if (width == s_reported_width && height == s_reported_height)
        return;

    s_reported_width = width;
    s_reported_height = height;

    retro_system_av_info av = {};
    fill_av_info(&av);
    av.geometry.base_width = width;
    av.geometry.base_height = height;

    // Only a frame larger than the frontend made room for needs a full AV
    // info update, which rebuilds its video driver; otherwise geometry does.
    if (width > s_max_width || height > s_max_height)
    {
        s_max_width = av.geometry.max_width = std::max({av.geometry.max_width, s_max_width, width});
        s_max_height = av.geometry.max_height = std::max({av.geometry.max_height, s_max_height, height});
        environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
    }
    else
    {
        av.geometry.max_width = s_max_width;
        av.geometry.max_height = s_max_height;
        environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av.geometry);
    }

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "RPCS3: presenting %ux%u\n", width, height);
}

// Forward declarations
static bool setup_hw_render();
#ifdef HAVE_VULKAN
static bool setup_vulkan_hw_render();
#endif

void retro_init(void)
{
    if (core_initialized)
        return;

    // The upstream build and the commit of this repository the core was built
    // from ("v0.0.43-20243 (libretro core 1a2b3c4)"), so a tester's log says
    // which build it came from
    if (log_cb)
        log_cb(RETRO_LOG_INFO, "RPCS3 libretro core %s\n", RPCS3_LIBRETRO_VERSION);



#ifdef _WIN32
    lrcore_install_crash_handler();
    lrcore_raise_timer_resolution();

    // Also from standalone's startup, and missing with it (NNshi's logs):
    // Winsock, without which a game's first socket crashed it (Wipeout HD
    // Fury, in sys_net_bnet_socket), and the 2-3 GiB working set PS3 memory
    // that has to stay resident is locked in ("Failed to lock sudo memory").
    // Standalone gives up without them; the core says so and carries on.
    if (!SetProcessWorkingSetSize(GetCurrentProcess(), 0x80000000, 0xC0000000))
        sys_log.error("SetProcessWorkingSetSize() failed (error %lu)", GetLastError());
    WSADATA wsa_data{};
    s_wsa_started = WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    if (!s_wsa_started)
        sys_log.error("WSAStartup() failed, games cannot use the network");
#endif
#ifdef __linux__
    // Standalone's other startup setting: 1 us timer slack instead of the
    // default 50, for this thread and every thread it starts.
    prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
#endif
#if defined(__linux__) || defined(__APPLE__)
    // Standalone also raises the limit of open files at startup, to 4096 on
    // Linux and OPEN_MAX on macOS. The core ran with the frontend's, often
    // 1024, and the NVIDIA Vulkan driver, which takes a file descriptor for
    // each fence, ran out of them as the first game started: vkCreateFence
    // failed with VK_ERROR_OUT_OF_HOST_MEMORY, errno 24 (demifiend9, RTX 4050).
    // Only the soft limit is raised, up to the hard one; the frontend's
    // process is not given a lower limit than it had.
    {
#ifdef __APPLE__
        const rlim_t wanted = OPEN_MAX;
#else
        const rlim_t wanted = 4096;
#endif
        struct rlimit limits;
        if (getrlimit(RLIMIT_NOFILE, &limits) == 0 && limits.rlim_cur < wanted)
        {
            const rlim_t old_limit = limits.rlim_cur;
            limits.rlim_cur = limits.rlim_max == RLIM_INFINITY ? wanted : std::min<rlim_t>(wanted, limits.rlim_max);
            if (setrlimit(RLIMIT_NOFILE, &limits) != 0)
                limits.rlim_cur = old_limit;
            if (log_cb)
                log_cb(RETRO_LOG_INFO, "Maximum open file descriptors: %llu (was %llu)\n",
                    static_cast<unsigned long long>(limits.rlim_cur), static_cast<unsigned long long>(old_limit));
        }
    }
#endif

    // Get system directory first
    const char* sys_dir = nullptr;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys_dir) && sys_dir)
    {
        system_dir = sys_dir;
    }

    // RPCS3's config and cache directories go under <system>/rpcs3/ too, before
    // anything below asks for them (see g_libretro_config_dir in File.cpp).
    if (!system_dir.empty())
    {
        extern std::string g_libretro_config_dir;
        g_libretro_config_dir = system_dir + "/rpcs3/";
        fs::create_path(g_libretro_config_dir);
    }

    // Get save directory
    const char* sav_dir = nullptr;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sav_dir) && sav_dir)
    {
        save_dir = sav_dir;
    }

    // Forward RPCS3 internal logs (including firmware installer logs) to libretro logger
    if (!s_logs_hooked)
    {
        logs::listener::add(&g_libretro_logs);
        s_logs_hooked = true;

        // Also create a native RPCS3 file logger for detailed debugging
        // This captures all internal RPCS3 logs that may not be forwarded to RetroArch
        // Beside everything else the core keeps, in system/rpcs3 (NNshi); the
        // save directory only when there is no system directory
        const std::string log_dir = !system_dir.empty() ? system_dir + "/rpcs3/" : (save_dir.empty() ? std::string() : save_dir + "/");
        if (!log_dir.empty())
        {
            const std::string log_path = log_dir + "rpcs3_detailed.log";
            s_file_logger = logs::make_file_listener(log_path, 100 * 1024 * 1024); // 100MB max
        }
    }

    // Initialize locale
    std::setlocale(LC_ALL, "C");

    // Set the emulator directory to RetroArch system/rpcs3/
    // This is where firmware (dev_flash) and other RPCS3 data should be stored
    std::string emu_dir = system_dir + "/rpcs3/";
    g_cfg_vfs.emulator_dir.from_string(emu_dir);

    // Ensure RPCS3 can reload the correct EmulatorDir inside Emu.Init()/BootGame.
    // Emu.Init() resets g_cfg_vfs and then loads vfs.yml from fs::get_config_dir(true).
    // If vfs.yml is missing, RPCS3 falls back to RetroArch root, which breaks /dev_flash.
    if (!fs::is_file(cfg_vfs::get_path()))
    {
        g_cfg_vfs.save();
    }


    if (!libretro_is_firmware_installed())
    {
        const std::string pup_path = find_firmware_pup();
        if (pup_path.empty())
        {
        }
        else
        {

            if (!g_fxo->is_init())
            {
                g_fxo->reset();
            }

            bool ok = false;
            try
            {
                ok = libretro_install_firmware(pup_path, [](int cur, int total)
                {

                });
            }
            catch (const std::exception& e)
            {
                (void)e;
                ok = false;
            }
            catch (...)
            {

                ok = false;
            }


        }
    }
    else
    {
    }

    // RPCS3 replaces a renderer it was not told about with the default (Null),
    // and standalone tells it by probing the GPU. Here the frontend's context is
    // what decides, so every renderer this build has is allowed.
    std::set<video_renderer> supported_renderers{video_renderer::null};
#ifndef WITHOUT_OPENGL
    supported_renderers.insert(video_renderer::opengl);
#endif
#ifdef HAVE_VULKAN
    supported_renderers.insert(video_renderer::vulkan);
#endif
    Emu.SetSupportedRenderers(std::move(supported_renderers));

    // Initialize the emulator
    Emu.SetHasGui(false);
    Emu.SetUsr("00000001");
    Emu.Init();



    // Set up callbacks
    init_emu_callbacks();

    // Set up hardware rendering (OpenGL context). Skipped without one: there is
    // no context to reset, so the boot below must not wait for one either.
    //
    // The renderer option decides whether one is asked for. OpenGL draws into
    // the frontend's context; Vulkan has none to draw into here, so it finishes
    // each frame into memory and retro_run hands the pixels over - the same
    // path Android and the Apple embedded systems already take.
#ifdef HAVE_VULKAN
    if (get_option_value("rpcs3_renderer", "vulkan") == "vulkan")
    {
        // The frontend's hardware context first: no copy through the CPU.
        // RPCS3_VK_READBACK=1 keeps the copy, for comparing the two.
        const char* readback = std::getenv("RPCS3_VK_READBACK");
        if (!(readback && *readback == '1') && setup_vulkan_hw_render())
            g_libretro_vulkan_hw = true;
        else
        {
            if (log_cb)
                log_cb(RETRO_LOG_INFO, "RPCS3: Vulkan without a hardware context - frames are copied back through memory\n");
            g_libretro_software_present = true;
        }
    }
#else
    if (get_option_value("rpcs3_renderer", "vulkan") == "vulkan" && log_cb)
        log_cb(RETRO_LOG_WARN, "RPCS3: this core was built without Vulkan - using OpenGL\n");
#endif

    if (!g_libretro_software_present && !g_libretro_vulkan_hw && !setup_hw_render())
    {
        // Every context this asked for was refused. Carrying on as if one had
        // been given is how a frontend with no OpenGL ends up with a core that
        // waits forever for a reset that never comes; take the path that needs
        // no context instead.
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: the frontend gave no OpenGL context - drawing through memory instead\n");

        g_libretro_software_present = true;
    }

    // Only the software path cares: with a hardware context the frontend takes
    // the image from the GPU and this is ignored. XRGB8888 is B,G,R,X in
    // memory, which is the VK_FORMAT_B8G8R8A8_UNORM the renderer hands over.
    if (g_libretro_software_present)
    {
        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
        if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt) && log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: frontend refused XRGB8888, there is nothing else to draw with\n");
    }

    core_initialized = true;




}

// Emulator::Kill does not tear down on the calling thread - it hands the work to
// a thread of its own, which stops the PPU, SPU and RSX threads, unmounts the
// VFS and only then marks the state fully stopped. Anything that pulls the
// ground out from under that (dropping the disc device, cleaning up globals)
// has to wait for it. Bounded, because a core that hangs the frontend on unload
// is worse than one that lets go early, and the timeout is worth saying out loud.
static void wait_for_emulation_stop(const char* what)
{
    constexpr int kStopWaitMs = 5000;
    int waited = 0;
    while (!Emu.IsStopped(true) && waited < kStopWaitMs)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        waited++;
    }
    if (!Emu.IsStopped(true))
        log_cb(RETRO_LOG_WARN, "RPCS3: emulation did not finish stopping in %d ms; %s anyway\n",
            kStopWaitMs, what);
    else if (waited > 0)
        log_cb(RETRO_LOG_INFO, "RPCS3: emulation stopped after %d ms\n", waited);
}

// A boot that fails part way - vkCreateDevice refusing a feature the GPU does
// not have throws from inside BootGame (sco's Mali phone) - leaves the threads
// it already started running, with game_loaded never set. Nothing stopped them:
// retro_unload_game and retro_deinit only stop a loaded game, so CleanUp pulled
// global state out from under live threads and the frontend hung on "Unloading
// core..." until the app was killed. Stop whatever the boot got going, and let
// go of the disc image it mounted.
static void stop_failed_boot()
{
    if (!Emu.IsStopped(true))
    {
        Emu.GracefulShutdown(false, false);
        wait_for_emulation_stop("giving up on the boot");
    }
    unload_iso();
    s_iso_path.clear();
}

#ifdef _WIN32
void thread_ctrl_uninstall_exception_handlers(); // Utilities/Thread.cpp
#endif

void retro_deinit(void)
{
    if (!core_initialized)
        return;

    stop_pause_watchdog();



    // Also when no game loaded: a failed boot may have left threads running
    if (game_loaded || !Emu.IsStopped(true))
    {
        Emu.GracefulShutdown(false, false);
        // Same order as retro_unload_game: CleanUp tears down global state the
        // stopping threads are still standing on.
        wait_for_emulation_stop("cleaning up");
    }

    Emulator::CleanUp();
    core_initialized = false;
    game_loaded = false;

    // Last, once nothing is logging any more: unlink both listeners, and let
    // the file logger flush and join its writer thread here rather than in a
    // static destructor inside FreeLibrary (see s_file_logger).
    if (s_logs_hooked)
    {
        if (s_file_logger)
        {
            logs::listener::remove(s_file_logger.get());
            s_file_logger.reset();
        }
        logs::listener::remove(&g_libretro_logs);
        s_logs_hooked = false;
    }



#ifdef _WIN32
    lrcore_uninstall_crash_handler();
    lrcore_restore_timer_resolution();
    if (s_wsa_started)
    {
        WSACleanup();
        s_wsa_started = false;
    }
    // RPCS3's own exception handlers, installed when the library was loaded
    // (Utilities/Thread.cpp); left in, the next exception in the frontend
    // after the unload jumps into code that is gone
    thread_ctrl_uninstall_exception_handlers();
#endif


}

static bool do_boot_game(const std::string& path);
static void forget_state();

static void context_reset()
{



    // Initialize libretro video with the new context
    libretro_video_init(hw_render.get_current_framebuffer, hw_render.get_proc_address);



    // Now that the GL context is ready, boot the game if pending
    if (pending_game_boot && !game_loaded)
    {

        if (do_boot_game(game_path))
        {
            game_loaded = true;
            start_pause_watchdog();
        }
        pending_game_boot = false;
    }


}

static void context_destroy()
{


    libretro_video_deinit();


}

static bool setup_hw_render()
{
    // The version here is the one the frontend asks the driver for, not a
    // floor it is allowed to exceed - RetroArch passes it straight to
    // glXCreateContextAttribs and logs "Creating context for requested version
    // 4.3". The comment that used to be here said the opposite, and that cost
    // someone an evening: asking for 4.3 gets exactly 4.3, and Mesa then does
    // not advertise the extensions that were promoted into core after it.
    // Buffer storage went into 4.4 and direct state access into 4.5, so a 4.3
    // context on a card that can do 4.6 reports neither, and RPCS3 refuses to
    // start with "GL_ARB_direct_state_access ... is required but not supported
    // by your GPU" on hardware that supports it perfectly well. Reported on a
    // Radeon RX 6600.
    //
    // So ask high and step down. 4.5 is the real floor for what the GL backend
    // checks for; below that it will refuse whatever we do, and the lower
    // entries are only there so the failure comes from RPCS3's own capability
    // check, which names what is missing, rather than from a context that was
    // never created.
    hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
    hw_render.context_reset = context_reset;
    hw_render.context_destroy = context_destroy;
    hw_render.depth = true;
    hw_render.stencil = true;
    hw_render.bottom_left_origin = true;
    hw_render.cache_context = true;
    hw_render.debug_context = false;

    static const struct { unsigned major, minor; } kCoreVersions[] = {
        {4, 6}, {4, 5}, {4, 4}, {4, 3}, {3, 3},
    };
    for (const auto& v : kCoreVersions)
    {
        hw_render.version_major = v.major;
        hw_render.version_minor = v.minor;
        if (environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
        {
            if (log_cb)
                log_cb(RETRO_LOG_INFO, "RPCS3: OpenGL core %u.%u context requested\n", v.major, v.minor);
            return true;
        }
    }

    // Final fallback: try legacy OpenGL compatibility context


    hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
    hw_render.version_major = 3;
    hw_render.version_minor = 0;

    if (environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
    {

        return true;
    }


    return false;
}

#ifdef HAVE_VULKAN
// ---- Vulkan hardware context -------------------------------------------------
//
// The frontend creates the VkInstance from the application info below and asks
// the core for a device on it (create_device). That device is RPCS3's own -
// render_device::create with its extensions and features, plus whatever the
// frontend needs - made by building the libretro swapchain that owns it. The
// renderer then takes that swapchain at boot instead of making its own
// (VKGSRender's constructor), and retro_run hands each finished image to the
// frontend with set_image. The device is the frontend's afterwards: it destroys
// it after destroy_device, where the core frees what it made on it.

static const struct retro_hw_render_interface_vulkan* s_vk_iface = nullptr;
static retro_hw_render_callback s_vk_hw_render{};

static const VkApplicationInfo* vk_get_application_info()
{
    // 1.2 is what RPCS3's own instance asks for, and what the renderer's
    // physical device queries rely on.
    static VkApplicationInfo info{};
    info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    info.pApplicationName = "RPCS3";
    info.pEngineName = "RPCS3";
    info.apiVersion = VK_API_VERSION_1_2;
    return &info;
}

// Whether RPCS3 can make its own Vulkan device - the renderer's, when it does
// not draw on the frontend's context - tried here with the same
// render_device::create before the boot. A GPU without a feature RPCS3
// requires (shaderUniformBufferUnsizedArray on sco's Mali) failed it inside
// BootGame, with the emulator's threads half started, and stopping those hung
// RetroArch; refused here, the load just fails.
static bool vk_own_device_possible(std::string& error)
{
    vk::instance inst;
    if (!inst.create("RPCS3"))
    {
        error = "no Vulkan driver";
        return false;
    }
    std::vector<vk::physical_device>& gpus = inst.enumerate_devices();
    if (gpus.empty())
    {
        error = "no Vulkan GPU";
        return false;
    }
    // The renderer's choice: the adapter the options name, else the first
    vk::physical_device* chosen = &gpus[0];
    const std::string adapter = g_cfg.video.vk.adapter;
    for (auto& candidate : gpus)
        if (!adapter.empty() && candidate.get_name() == adapter)
            chosen = &candidate;

    u32 graphics_family = umax;
    u32 transfer_family = umax;
    for (u32 i = 0, families = chosen->get_queue_count(); i < families; ++i)
    {
        const auto flags = chosen->get_queue_properties(i).queueFlags;
        if (graphics_family == umax && (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            graphics_family = i;
        else if (transfer_family == umax && (flags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) == (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))
            transfer_family = i;
    }
    if (graphics_family == umax)
    {
        error = chosen->get_name() + " has no graphics queue";
        return false;
    }

    vk::render_device dev;
    try
    {
        dev.create(*chosen, graphics_family, graphics_family, transfer_family);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
    dev.destroy();
    return true;
}

static bool vk_create_device(struct retro_vulkan_context* context, VkInstance instance, VkPhysicalDevice gpu,
    VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr /*get_instance_proc_addr*/,
    const char** required_device_extensions, unsigned num_required_device_extensions,
    const char** /*required_device_layers*/, unsigned /*num_required_device_layers*/,
    const VkPhysicalDeviceFeatures* required_features)
{
    vk::instance& inst = vk::libretro::shared_instance();
    inst.adopt(instance);
    std::vector<vk::physical_device>& gpus = inst.enumerate_devices();
    if (gpus.empty())
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: Vulkan: the frontend's instance has no GPU\n");
        return false;
    }

    // The one the frontend picked, else the adapter the options name, else
    // the first - the same order of preference as the renderer's own.
    vk::physical_device* chosen = nullptr;
    for (auto& candidate : gpus)
        if (gpu && static_cast<VkPhysicalDevice>(candidate) == gpu)
            chosen = &candidate;
    if (!chosen)
    {
        const std::string adapter = g_cfg.video.vk.adapter;
        for (auto& candidate : gpus)
            if (!adapter.empty() && candidate.get_name() == adapter)
                chosen = &candidate;
    }
    if (!chosen)
        chosen = &gpus[0];

    // The graphics queue the frontend shares, and a separate compute and
    // transfer family for the renderer's async work where there is one - the
    // choice the headless swapchain makes.
    u32 graphics_family = umax;
    u32 transfer_family = umax;
    for (u32 i = 0, families = chosen->get_queue_count(); i < families; ++i)
    {
        const auto flags = chosen->get_queue_properties(i).queueFlags;
        if (graphics_family == umax && (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            graphics_family = i;
        else if (transfer_family == umax && (flags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) == (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))
            transfer_family = i;
    }
    if (graphics_family == umax)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: Vulkan: %s has no graphics queue\n", chosen->get_name().c_str());
        return false;
    }

    // The frontend presents from the queue it is given, so it has to be able to.
    if (surface)
    {
        VkBool32 can_present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(*chosen, graphics_family, surface, &can_present);
        if (!can_present)
        {
            if (log_cb)
                log_cb(RETRO_LOG_ERROR, "RPCS3: Vulkan: the graphics queue of %s cannot present to the frontend's window\n", chosen->get_name().c_str());
            return false;
        }
    }

    vk::libretro::set_frontend_requirements(required_device_extensions, num_required_device_extensions, required_features);
    vk::libretro::set_hw_present(true);

    vk::swapchain_LIBRETRO* swapchain = nullptr;
    try
    {
        // Constructing it creates the device (swapchain_base -> render_device::create).
        swapchain = new vk::swapchain_LIBRETRO(*chosen, graphics_family, graphics_family, transfer_family, true);
    }
    catch (const std::exception& e)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: Vulkan: creating the device failed: %s\n", e.what());
        vk::libretro::set_hw_present(false);
        return false;
    }

    const vk::render_device& dev = swapchain->get_device();
    vk::libretro::set_frontend_device(dev);
    vk::libretro::set_shared_swapchain(swapchain);

    context->gpu = *chosen;
    context->device = dev;
    context->queue = dev.get_graphics_queue();
    context->queue_family_index = graphics_family;
    context->presentation_queue = dev.get_graphics_queue();
    context->presentation_queue_family_index = graphics_family;

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "RPCS3: Vulkan device created on %s for the frontend's context (queue family %u)\n",
            chosen->get_name().c_str(), graphics_family);
    return true;
}

// What RPCS3 made on the device - its allocator, the swapchain images - and
// not the device itself, which is the frontend's.
//
// Done from context_destroy rather than the negotiation's destroy_device:
// RetroArch unloads the core when content is closed and only destroys the
// Vulkan context later, when it brings its video driver back up for the menu,
// so a destroy_device of ours was called in a library that was already gone
// (NNshi: a crash in rpcs3_libretro.dll_unloaded on every close).
// context_destroy comes while the core is still loaded.
static void vk_release_device_resources()
{
    auto* swapchain = vk::libretro::shared_swapchain();
    if (!swapchain)
        return;

    // A renderer still drawing on the device would outlive it. RetroArch takes
    // the two down in either order: closing content unloads the core first and
    // the context later, but quitting brings the video driver down - and the
    // device with it - before the core is unloaded. In that order the emulator
    // is still running here, and its renderer then waited on fences of a
    // destroyed device on the way out ("vkGetFenceStatus: Invalid device",
    // and an abort). So stop it here, the way retro_unload_game does; that
    // finds it stopped later and only drops the disc.
    if (!Emu.IsStopped())
    {
        stop_pause_watchdog();
        Emu.GracefulShutdown(false, false);
        wait_for_emulation_stop("the Vulkan context going away");
    }

    // Nothing of the device may be in use, the frontend's last frame included.
    vk::acquire_global_submit_lock();
    vkDeviceWaitIdle(swapchain->get_device());
    vk::release_global_submit_lock();

    vk::libretro::drop_pending_frames();
    swapchain->destroy(true);
    delete swapchain;
    vk::libretro::set_shared_swapchain(nullptr);
    vk::libretro::set_frontend_device(VK_NULL_HANDLE);
    vk::libretro::set_queue_lock(nullptr, nullptr, nullptr);
    vk::libretro::shared_instance().destroy();
    s_vk_iface = nullptr;
}

static const struct retro_hw_render_context_negotiation_interface_vulkan s_vk_negotiation{
    RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
    1, // create_device, not create_device2: the frontend's instance is enough
    vk_get_application_info,
    vk_create_device,
    nullptr, // see vk_release_device_resources
};

static void vk_fetch_interface()
{
    const struct retro_hw_render_interface* iface = nullptr;
    if (environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &iface) && iface &&
        iface->interface_type == RETRO_HW_RENDER_INTERFACE_VULKAN)
    {
        s_vk_iface = reinterpret_cast<const struct retro_hw_render_interface_vulkan*>(iface);
        vk::libretro::set_queue_lock(s_vk_iface->lock_queue, s_vk_iface->unlock_queue, s_vk_iface->handle);
    }
}

static void vk_context_reset()
{
    vk_fetch_interface();
    if (!s_vk_iface)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: Vulkan: the frontend gave a context but no Vulkan interface\n");
        return;
    }

    if (pending_game_boot && !game_loaded)
    {
        if (do_boot_game(game_path))
        {
            game_loaded = true;
            start_pause_watchdog();
        }
        pending_game_boot = false;
    }
}

static void vk_context_destroy()
{
    vk_release_device_resources();
}

static bool setup_vulkan_hw_render()
{
    s_vk_hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
    s_vk_hw_render.version_major = VK_API_VERSION_1_2;
    s_vk_hw_render.version_minor = 0;
    s_vk_hw_render.context_reset = vk_context_reset;
    s_vk_hw_render.context_destroy = vk_context_destroy;
    // RetroArch rebuilds its video driver for a fullscreen toggle. Without
    // this it took the Vulkan context down with it, and context_destroy stops
    // the emulator (vk_release_device_resources), so the game quit at every
    // toggle (NNshi). Cached, the device survives the rebuild, and
    // context_destroy comes only when it really goes away.
    s_vk_hw_render.cache_context = true;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &s_vk_hw_render))
        return false;

    if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, const_cast<retro_hw_render_context_negotiation_interface_vulkan*>(&s_vk_negotiation)))
    {
        // Without it the frontend makes a device of its own, without the
        // extensions and features RPCS3 needs. Take the readback path then,
        // and take back the request for a context, which that path does not use.
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: the frontend has no Vulkan context negotiation\n");
        retro_hw_render_callback none{};
        none.context_type = RETRO_HW_CONTEXT_NONE;
        environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &none);
        return false;
    }

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "RPCS3: Vulkan hardware context requested\n");
    return true;
}

// Hand the newest finished frame to the frontend. An image it has been given
// is held until it can no longer be using it: replaced, and then as many
// frames gone by as the frontend has frames in flight.
static void vk_present_frame()
{
    vk_fetch_interface();
    auto* swapchain = dynamic_cast<vk::swapchain_LIBRETRO*>(vk::libretro::shared_swapchain());

    static u32 s_shown = umax;
    static u32 s_generation = 0;
    static u64 s_frame = 0;
    static unsigned s_width = 1280, s_height = 720;
    static std::deque<std::pair<u32, u64>> s_replaced;
    static struct retro_vulkan_image s_image{};
    s_frame++;

    // A resize made the images anew: the ones held are gone, and so is the
    // one the frontend would show again.
    if (const u32 generation = vk::libretro::image_generation(); generation != s_generation)
    {
        s_generation = generation;
        s_replaced.clear();
        s_shown = umax;
    }

    u32 index = umax;
    vk::swapchain_LIBRETRO::hw_image image;
    const bool new_frame = swapchain && s_vk_iface && vk::libretro::take_frame(index) && swapchain->get_hw_image(index, image);
    libretro_check_frame_stall(new_frame);

    if (new_frame)
    {
        s_image.image_view = image.view;
        s_image.image_layout = VK_IMAGE_LAYOUT_GENERAL;
        s_image.create_info = image.view_info;
        s_vk_iface->set_image(s_vk_iface->handle, &s_image, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);

        if (s_shown != umax)
            s_replaced.emplace_back(s_shown, s_frame);
        s_shown = index;
        s_width = image.width;
        s_height = image.height;
        report_frame_size(s_width, s_height);
        video_cb(RETRO_HW_FRAME_BUFFER_VALID, s_width, s_height, 0);
    }
    else
    {
        // The frontend shows the last image again, or nothing yet.
        video_cb(NULL, s_width, s_height, 0);
    }

    if (swapchain)
    {
        u32 in_flight = 3;
        if (s_vk_iface && s_vk_iface->get_sync_index_mask)
            in_flight = std::max(1, std::popcount(s_vk_iface->get_sync_index_mask(s_vk_iface->handle)));
        while (!s_replaced.empty() && s_frame - s_replaced.front().second > in_flight)
        {
            swapchain->release_image(s_replaced.front().first);
            s_replaced.pop_front();
        }

        // Images replaced by a restart or a resize stay alive while the
        // frontend may still show one (see swapchain_LIBRETRO): until it has a
        // frame from the new ones, and is done with the frames before that.
        static u64 s_retire_from = 0;
        static u32 s_retire_generation = 0;
        if (!swapchain->has_retired())
            s_retire_from = 0;
        else if (!s_retire_from || s_retire_generation != s_generation)
        {
            if (new_frame)
            {
                s_retire_from = s_frame;
                s_retire_generation = s_generation;
            }
        }
        else if (s_frame - s_retire_from > in_flight)
        {
            swapchain->free_retired();
            s_retire_from = 0;
        }
    }
}
#endif

// The last component of a path, with either kind of separator
static std::string_view path_leaf(std::string_view path)
{
    const usz slash = path.find_last_of("/\\");
    return slash == umax ? path : path.substr(slash + 1);
}

// Set when the content was a PKG, installed and done with: retro_run then only
// shows a black frame until it asks RetroArch to close the content.
static bool s_pkg_install_only = false;
static u32 s_pkg_install_frames = 0;

bool retro_load_game(const struct retro_game_info* game)
{


    if (!game || !game->path)
    {

        return false;
    }

    // Surround needs the frontend's multi-channel output, negotiated now, at
    // load; a frontend without it keeps the classic stereo callback
    if (libretro_audio_negotiate_multi(environ_cb))
        { if (log_cb) log_cb(RETRO_LOG_INFO, "RPCS3: multi-channel audio output negotiated; Audio Format can offer games 5.1/7.1\n"); }
    else
        { if (log_cb) log_cb(RETRO_LOG_INFO, "RPCS3: the frontend has no multi-channel audio output; stereo\n"); }

    // Save states (see retro_serialize_size): none until the game runs, a
    // size of their own each, and as RPCS3 writes them, little-endian
    {
        uint64_t quirks = RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE
            | RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE
            | RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT;
        environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);
    }

    // Get content directory from RetroArch (for PKG installation)
    const char* content_dir_ptr = nullptr;
    if (environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_dir_ptr) && content_dir_ptr)
    {
        content_dir = content_dir_ptr;
    }
    else
    {
        content_dir.clear();

    }

    // Check if frontend supports frame duping (passing NULL to video_cb to reuse last frame)
    bool can_dupe = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_CAN_DUPE, &can_dupe) && can_dupe)
    {

    }
    else
    {

    }

    game_path = game->path;

    // A PKG is installed, not started, as standalone RPCS3 does: the core says
    // where it went and has RetroArch close the content again (retro_run). The
    // game is started afterwards from its PARAM.SFO in dev_hdd0/game/ (NNshi:
    // a game booted right after its install looked like a hang while its
    // modules compiled).
    if (is_pkg_file(game_path))
    {
        std::string installed_eboot;
        if (!install_pkg_file(game_path, installed_eboot))
            return false;

        std::string message;
        if (installed_eboot.empty())
        {
            message = "PKG installed (no game in it - DLC or an update)";
        }
        else
        {
            // .../game/<title id>/USRDIR/EBOOT.BIN
            const std::string game_dir = fs::get_parent_dir(fs::get_parent_dir(installed_eboot));
            message = "PKG installed. Start it from " + game_dir + "/PARAM.SFO";
        }
        if (log_cb)
            log_cb(RETRO_LOG_INFO, "RPCS3: %s\n", message.c_str());
        libretro_show_message(message.c_str(), 600);
        s_pkg_install_only = true;
        return true;
    }
    // A disc image. The ISO reader serves the volume inside it as an fs::
    // virtual device, so the boot path is taken from there and everything
    // below reads the image through the device. An encrypted image needs its
    // redump key in data/redump/ - the reader looks for it and says so when it
    // is missing.
    else if (is_iso_file(game_path))
    {
        load_iso(game_path);
        s_iso_path = game_path;

        const std::string disc_root = iso_device::virtual_device_name + "/";
        const std::string eboot = disc_root + "PS3_GAME/USRDIR/EBOOT.BIN";

        if (!fs::is_file(eboot))
        {
            if (log_cb)
                log_cb(RETRO_LOG_ERROR,
                    "RPCS3: %s could not be opened as a PS3 disc: no PS3_GAME/USRDIR/EBOOT.BIN in the volume. "
                    "An encrypted image needs its .dkey in data/redump/.\n",
                    game_path.c_str());
            libretro_show_message("This disc image could not be opened - an encrypted one needs its redump key", 500);
            unload_iso();
            s_iso_path.clear();
            return false;
        }

        if (log_cb)
            log_cb(RETRO_LOG_INFO, "RPCS3: booting the disc image as %s\n", eboot.c_str());

        game_path = eboot;
    }
    // PARAM.SFO or PS3_DISC.SFB stand for the game folder they sit in. They
    // are content for the sake of the frontend's per-folder options: those are
    // named after the folder of the loaded file, and for an EBOOT.BIN that is
    // USRDIR in every game, while PARAM.SFO of a PSN title sits in the folder
    // named after its title ID (NPUB31234) and PS3_DISC.SFB in the root of a
    // disc dump. The game folder is booted, as for an EBOOT.BIN (NNshi).
    else if (const std::string name = fmt::to_lower(path_leaf(game_path)); name == "param.sfo" || name == "ps3_disc.sfb")
    {
        std::string game_folder = fs::get_parent_dir(game_path);
        // A disc dump's PARAM.SFO is in PS3_GAME, one level below its root
        if (name == "param.sfo" && fmt::to_lower(path_leaf(game_folder)) == "ps3_game" && fs::is_file(fs::get_parent_dir(game_folder) + "/PS3_DISC.SFB"))
            game_folder = fs::get_parent_dir(game_folder);

        if (log_cb)
            log_cb(RETRO_LOG_INFO, "RPCS3: booting the game folder %s\n", game_folder.c_str());
        game_path = game_folder;
    }
    // Check if EBOOT.BIN was passed directly - need to find parent game folder
    else if (game_path.size() >= 9)
    {
        std::string filename = game_path;
        // Extract just the filename for comparison
        size_t last_slash = filename.find_last_of("/\\");
        if (last_slash != std::string::npos)
            filename = filename.substr(last_slash + 1);

        // Check if it's an EBOOT.BIN file (case insensitive)
        bool is_eboot = (filename == "EBOOT.BIN" || filename == "eboot.bin" ||
                         filename == "Eboot.bin" || filename == "EBOOT.bin");

        if (is_eboot)
        {


            std::string game_folder;
            std::string current_path = fs::get_parent_dir(game_path);


            // Check up to 4 levels up for PS3_GAME or valid game structure
            for (int i = 0; i < 4 && !current_path.empty(); i++)
            {

                // Check if this directory contains PS3_GAME subdirectory (disc game structure)
                std::string ps3_game_path = current_path + "/PS3_GAME";
                if (fs::is_dir(ps3_game_path))
                {
                    game_folder = current_path;
                    break;
                }

                // Check if current directory name ends with USRDIR (we're inside PS3_GAME folder)
                size_t usrdir_pos = current_path.find("USRDIR");
                if (usrdir_pos != std::string::npos &&
                    (usrdir_pos == current_path.size() - 6 ||
                     current_path[usrdir_pos + 6] == '/' ||
                     current_path[usrdir_pos + 6] == '\\'))
                {
                    // Go up one more level to get PS3_GAME, then check for parent
                    std::string ps3_game = fs::get_parent_dir(current_path);
                    if (ps3_game.size() >= 8)
                    {
                        std::string ps3_game_name = ps3_game;
                        size_t slash = ps3_game_name.find_last_of("/\\");
                        if (slash != std::string::npos)
                            ps3_game_name = ps3_game_name.substr(slash + 1);

                        if (ps3_game_name == "PS3_GAME")
                        {
                            game_folder = fs::get_parent_dir(ps3_game);
                            break;
                        }
                    }
                }

                // Check if this directory contains PARAM.SFO (HDD game structure)
                std::string param_sfo = current_path + "/PARAM.SFO";
                if (fs::is_file(param_sfo))
                {
                    game_folder = current_path;
                    break;
                }

                current_path = fs::get_parent_dir(current_path);
            }

            if (game_folder.empty())
            {

                return false;
            }

            game_path = game_folder;
        }
    }

    // OpenGL when there is a context to draw into, and the boot then waits for
    // context_reset(). Without one it has to be Vulkan: RPCS3's Vulkan backend
    // can finish a frame into memory, which is what the software path reads.
    // Null is the option saying not to draw at all, which is worth having when
    // the question is whether the emulator runs rather than what it looks like.
    if (get_option_value("rpcs3_renderer", "vulkan") == "null")
        g_cfg.video.renderer.set(video_renderer::null);
    else if (g_libretro_software_present || g_libretro_vulkan_hw)
        g_cfg.video.renderer.set(video_renderer::vulkan);
    else
        g_cfg.video.renderer.set(video_renderer::opengl);

    // Configure PPU decoder - use LLVM for best performance
    g_cfg.core.ppu_decoder.set(ppu_decoder_type::llvm);


    // Configure SPU decoder - use LLVM for best performance
    g_cfg.core.spu_decoder.set(spu_decoder_type::llvm);


    // Performance optimizations
    g_cfg.core.spu_loop_detection.set(false);
    g_cfg.core.llvm_threads.set(0);  // 0 = as many as the CPU has, as in RPCS3
    g_cfg.core.llvm_precompilation.set(true);  // Precompile LLVM modules
    g_cfg.video.multithreaded_rsx.set(false);
    g_cfg.video.disable_vertex_cache.set(false);  // Keep vertex cache enabled

    // Shader compilation optimizations
    g_cfg.video.shadermode.set(shader_mode::async_recompiler);  // Async multi-threaded shader compilation
    g_cfg.video.shader_compiler_threads_count.set(0);  // 0 = auto (optimal for CPU)
    g_cfg.video.disable_on_disk_shader_cache.set(false);  // Keep shader cache enabled for faster subsequent loads

    // RSX optimizations
    g_cfg.video.strict_rendering_mode.set(false);  // Disable strict mode for better performance
    g_cfg.video.disable_FIFO_reordering.set(false);  // Keep FIFO reordering enabled

    // Audio optimizations
    g_cfg.audio.enable_buffering.set(false);  // RetroArch buffers; see rpcs3_audio_buffering
    g_cfg.audio.desired_buffer_duration.set(100);  // 100ms buffer for smoother audio in libretro


    // Configure audio - set explicit stereo layout to avoid "Unsupported layout 0" error
    // (audio_channel_layout::automatic = 0 is not handled by default_layout_channel_count)
    g_cfg.audio.channel_layout.set(audio_channel_layout::stereo);


    // RPCS3's native interface draws what a PS3 draws over the game - message
    // dialogs ("install game data?", errors), save data lists, the on-screen
    // keyboard - into the picture, driven with the pad. Without it every one of
    // those calls failed at once, as there is no Qt dialog to fall back to
    // (NNshi). It was off because its icons were missing, which the core now
    // installs (install_ui_icons). The software present reads the picture back
    // before the overlays are drawn on the output, unless it records with them.
    g_cfg.misc.use_native_interface.set(true);
    g_cfg.video.record_with_overlays.set(true);
    g_cfg.misc.show_shader_compilation_hint.set(false);
    g_cfg.misc.show_ppu_compilation_hint.set(false);
    g_cfg.misc.show_autosave_autoload_hint.set(false);
    g_cfg.misc.show_pressure_intensity_toggle_hint.set(false);


    // The block above hardcodes settings that also have core options behind them
    // (PPU/SPU decoder, SPU loop detection, LLVM precompilation, multithreaded
    // RSX, ...). It runs after the early libretro_apply_core_options() call, so
    // without re-applying them here the user's choices are silently discarded -
    // e.g. "LLVM Precompilation: disabled" still precompiled every module.
    libretro_apply_core_options();
    libretro_limit_disk_cache();

    // Save config so BootGame() loads the correct settings when it reloads config.yml
    // Note: RPCS3 loads config from fs::get_config_dir(true) which adds "config/" subdirectory
    const std::string config_path = fs::get_config_dir(true) + "config.yml";
    g_cfg.save(config_path);

    install_ui_icons();
    install_config_database();
    install_patches();

    // The Patch Manager for this game, now that its serial and the patch files
    // are there
    libretro_patches::collect(game_serial(game_path));
    if (libretro_patches::any())
        publish_core_options(environ_cb);

    // For null renderer, boot immediately. For OpenGL, defer until
    // context_reset(). The software path has no context coming, so it boots
    // here as well or it would wait forever.
    if (g_cfg.video.renderer.get() == video_renderer::null || g_libretro_software_present)
    {

        if (!do_boot_game(game_path))
        {
            return false;
        }
        game_loaded = true;
        start_pause_watchdog();
    }
    else
    {
        // Defer game boot until context_reset() when GL context is ready
        pending_game_boot = true;

    }

    return true;
}

// Actually boot the game - called from context_reset() when GL context is ready
static bool do_boot_game(const std::string& path)
{


    // Ensure /dev_flash points to RetroArch system/rpcs3/ so the installed firmware is visible during boot
    if (!system_dir.empty())
    {
        const std::string emu_dir = system_dir + "/rpcs3/";
        g_cfg_vfs.emulator_dir.from_string(emu_dir);
        vfs::mount("/dev_flash", g_cfg_vfs.get_dev_flash());
    }

    // Initialize pad_thread before booting - cellPadInit requires this
    if (!g_libretro_pad_thread)
    {
        g_libretro_pad_thread = std::make_unique<pad_thread>(nullptr, nullptr, "");
        g_libretro_pad_thread->Init();

        // Create and initialize the libretro pad handler if not already done
        if (!g_libretro_pad_handler)
        {
            g_libretro_pad_handler = std::make_shared<LibretroPadHandler>();
            g_libretro_pad_handler->Init();
        }

        // Bind pads to the handler
        const auto& pads = g_libretro_pad_thread->GetPads();
        for (const auto& pad : pads)
        {
            if (pad)
            {
                g_libretro_pad_handler->bindPadToDevice(pad);
            }
        }

    }

    // The patches turned on in the Patch Manager, for RPCS3 to read as it boots
    libretro_patches::write_config([](const char* key) { return get_option_value(key, "disabled"); });

#ifdef HAVE_VULKAN
    if (g_cfg.video.renderer == video_renderer::vulkan && !vk::libretro::hw_present())
    {
        std::string vk_error;
        if (!vk_own_device_possible(vk_error))
        {
            if (log_cb)
                log_cb(RETRO_LOG_ERROR, "RPCS3: the GPU cannot run RPCS3's Vulkan renderer: %s\n", vk_error.c_str());
            libretro_show_message("RPCS3: this GPU lacks a Vulkan feature RPCS3 requires", 600);
            unload_iso();
            s_iso_path.clear();
            return false;
        }
    }
#endif

    game_boot_result result = game_boot_result::generic_error;
    try
    {
        Emu.SetForceBoot(true);
        result = Emu.BootGame(path);
    }
    catch (const std::exception& e)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: booting %s threw: %s\n", path.c_str(), e.what());
        stop_failed_boot();
        return false;
    }
    catch (...)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: booting %s threw an unknown exception\n", path.c_str());
        stop_failed_boot();
        return false;
    }

    if (result != game_boot_result::no_errors)
    {
        const char* error_str = "unknown";
        switch (result)
        {
        case game_boot_result::generic_error: error_str = "generic_error"; break;
        case game_boot_result::nothing_to_boot: error_str = "nothing_to_boot"; break;
        case game_boot_result::wrong_disc_location: error_str = "wrong_disc_location"; break;
        case game_boot_result::invalid_file_or_folder: error_str = "invalid_file_or_folder"; break;
        case game_boot_result::invalid_bdvd_folder: error_str = "invalid_bdvd_folder"; break;
        case game_boot_result::install_failed: error_str = "install_failed"; break;
        case game_boot_result::decryption_error: error_str = "decryption_error"; break;
        case game_boot_result::file_creation_error: error_str = "file_creation_error"; break;
        case game_boot_result::firmware_missing: error_str = "firmware_missing"; break;
        case game_boot_result::firmware_version: error_str = "firmware_version"; break;
        case game_boot_result::unsupported_disc_type: error_str = "unsupported_disc_type"; break;
        case game_boot_result::savestate_corrupted: error_str = "savestate_corrupted"; break;
        case game_boot_result::savestate_version_unsupported: error_str = "savestate_version_unsupported"; break;
        case game_boot_result::still_running: error_str = "still_running"; break;
        case game_boot_result::already_added: error_str = "already_added"; break;
        case game_boot_result::currently_restricted: error_str = "currently_restricted"; break;
        default: break;
        }
        // Without this the frontend only says the content failed to load, and
        // the reason is nowhere - rpcs3_detailed.log does not always have it.
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: booting %s failed: %s\n", path.c_str(), error_str);
        stop_failed_boot();
        return false;
    }


    // Wait for emulator to transition out of loading/starting states
    // Poll for up to 30 seconds
    constexpr int max_wait_ms = 30000;
    constexpr int poll_interval_ms = 100;
    int waited_ms = 0;

    // Wait while in loading or starting state
    while (waited_ms < max_wait_ms)
    {
        system_state state = Emu.GetStatus();

        // If running, paused, or ready - we can proceed. Starting too: that is
        // the emulator compiling the game's PPU modules (ppu_cmd::initialize),
        // which on a game's first boot can take over a minute, and the PPU
        // thread ends it itself once they are done. Waiting it out here held
        // context_reset - RetroArch showed nothing at all until the modules were
        // compiled, rather than its progress messages (NNshi).
        if (state == system_state::running || state == system_state::paused ||
            state == system_state::ready || state == system_state::frozen ||
            state == system_state::starting)
        {
            break;
        }

        // If stopped, boot failed
        if (state == system_state::stopped)
        {

            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
        waited_ms += poll_interval_ms;

    }

    system_state final_state = Emu.GetStatus();

    // If ready but not running, start it
    if (final_state == system_state::ready)
    {

        Emu.Run(true);
    }
    // If paused, resume
    else if (final_state == system_state::paused || final_state == system_state::frozen)
    {

        Emu.Resume();
    }


    // Start pause watchdog once the emulator is running so RetroArch pause fully pauses emulation.
    // (This is safe to call multiple times.)
    start_pause_watchdog();

    return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info, size_t num_info)
{
    (void)game_type;
    (void)info;
    (void)num_info;
    return false;
}

void retro_unload_game(void)
{
    s_pkg_install_only = false;
    s_pkg_install_frames = 0;
    // A motor left running would keep going after the game is gone.
    libretro_input_stop_rumble();

    stop_pause_watchdog();
    forget_state();
    if (game_loaded)
    {
        Emu.GracefulShutdown(false, false);
        game_loaded = false;
        game_path.clear();

        // The teardown does not finish on this thread: Emulator::Kill hands it
        // to a thread of its own ("Emulation Join Thread"), which stops the PPU,
        // SPU and RSX threads, unmounts the VFS and only then marks the state
        // fully stopped. Dropping the disc device before that pulls the ISO out
        // from under threads that are still shutting down - ozzfreak's log ends
        // exactly there, "Unloading ISO" from this thread and then the join
        // thread reported as too sleepy, waiting on something that never comes.
        //
        // So wait for the stop to complete. Bounded, because a core that hangs
        // the frontend on unload is worse than one that lets go early, and the
        // timeout is worth saying out loud if it is ever hit.
        wait_for_emulation_stop("unloading the disc");

        // Drops the disc device, and with it the handle on the image.
        unload_iso();
        s_iso_path.clear();

        // The game's cache on dev_hdd1 is closed now
        libretro_limit_disk_cache();
    }

}

void retro_run(void)
{
    if (s_pkg_install_only)
    {
        // One frame and the close: RetroArch keeps the install message up for
        // a few seconds on its menu anyway, and three seconds of black before
        // it were only a wait (NNshi)
        // 32 bits a pixel, so the pitch fits whichever format RetroArch is in
        static u32 s_black[320 * 240] = {};
        video_cb(s_black, 320, 240, 320 * sizeof(u32));
        if (++s_pkg_install_frames == 1)
            environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
        return;
    }

    if (!game_loaded)
        return;

    // The guest can end the emulation on its own (_sys_process_exit, e.g. the
    // ScummVM launcher's Quit). Nothing told the frontend so far, so RetroArch
    // kept spinning on the last frame of a dead emulator. Ask it to unload.
    //
    // Not when it is only switching executables, though. A launcher that
    // starts the real game with exitspawn (Zone of the Enders HD Collection
    // booting ZoE2) stops the emulator too, and the next executable is booted
    // from the Emulation Join Thread once the stop is complete. Unloading at
    // that point had GracefulShutdown hold the emulator's state guard right
    // when that boot came, which then failed with "Booting is restricted"
    // (NNshi). exitspawn switches continuous mode on before it stops the
    // emulator, and the boot follows only once the stop is complete; give it a
    // moment to begin before taking the stop as the end.
    static u32 s_stopped_frames = 0;
    if (Emu.IsStopped() && !pending_game_boot)
    {
        if (!Emu.IsStopped(true) || (Emu.ContinuousModeEnabled(false) && ++s_stopped_frames < 120))
            return;

        s_stopped_frames = 0;
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
        return;
    }
    s_stopped_frames = 0;

    static u64 s_run_counter = 0;
    s_run_counter++;
    s_run_count++;

    // Update watchdog timestamp.
    s_last_retro_run_us.store(lr_now_us());
#ifdef HAVE_VULKAN
    vk::libretro::frontend_ran();
#endif


    // Check for variable updates
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
    {
        libretro_apply_core_options();
        // The game's database settings stay on top of options changed while
        // it runs, as they were at its start.
        if (!s_database_config.empty())
            g_cfg.from_string(s_database_config);
    }

    // Poll input
    input_poll_cb();
    libretro_input_poll(input_state_cb);
    libretro_input_poll_sensors();  // Poll gyro/accelerometer data
    libretro_mouse_update();  // Light guns to the PS3 mice

    // Process pad handler to update RPCS3's pad states from libretro input
    if (g_libretro_pad_handler)
    {
        g_libretro_pad_handler->process();
    }

    // Copy button values from m_buttons to m_buttons_external so cellPad can read them
    if (g_libretro_pad_thread)
    {
        g_libretro_pad_thread->apply_copilots();

        // What pad_thread's own loop does after that, which the core never
        // ran: without it no pad counted as connected, and RPCS3's in-game
        // dialogs (save data list, "install game data?") ignored the
        // controller and left the game waiting on them for good (NNshi)
        u32 connected = 0;
        {
            std::lock_guard lock(pad::g_pad_mutex);
            for (const auto& pad : g_libretro_pad_thread->GetPads())
                if (pad && pad->is_connected())
                    connected++;
        }
        g_libretro_pad_thread->frontend_update(connected);
    }

    // One PS3 VBLANK per frame the frontend asks for (Frame Pacing:
    // RetroArch) - two where the VBLANK runs at twice the rate the core shows
    // frames at (a 30 fps game of the VBlank Frequency list)
    if (g_libretro_frontend_vblank && Emu.IsRunning())
    {
        g_libretro_vblank_requests += s_output_hz ? std::max<u32>(1, g_cfg.video.vblank_rate.get() / s_output_hz) : 1;
        g_libretro_vblank_requests.notify_one();
    }

    // Process audio
    libretro_audio_process(audio_batch_cb);

#ifdef HAVE_VULKAN
    if (g_libretro_vulkan_hw)
    {
        vk_present_frame();
        vk::libretro::frontend_returned();
        return;
    }
#endif

    // Without a hardware context there is no GL state to tidy and nothing to
    // blit: the renderer has already put the finished frame in memory.
    if (g_libretro_software_present)
    {
        const void* pixels = nullptr;
        u32 sw_width = 0, sw_height = 0, sw_pitch = 0;
        static u32 s_sw_width = 1280, s_sw_height = 720;
        const bool new_frame = libretro_take_software_frame(&pixels, &sw_width, &sw_height, &sw_pitch);
        libretro_check_frame_stall(new_frame);
        if (new_frame)
        {
            report_frame_size(sw_width, sw_height);
            s_sw_width = sw_width;
            s_sw_height = sw_height;
            video_cb(pixels, sw_width, sw_height, sw_pitch);
        }
        else
            video_cb(NULL, s_sw_width, s_sw_height, 0);
        return;
    }

    // Clean up GL state before returning control to frontend
    // Per libretro docs: cores must unbind all GL resources before video_cb
    // This prevents state conflicts between RSX rendering and RetroArch's rendering
    libretro_cleanup_gl_state();

    // Ensure RSX shared-context GPU work is ordered before RetroArch presents.
    libretro_wait_for_present_fence();

    // CRITICAL FIX: Only present frames when RSX has actually produced a new one
    // This prevents showing the same stale frame repeatedly, which causes flickering/flashing
    // Check if RSX has rendered a new frame since our last presentation
    const bool has_new_frame = libretro_has_new_frame();

    // What the blit actually wrote. libretro_blit_to_frontend() copies the
    // shared texture at its own size, so telling the frontend a fixed 1280x720
    // is right only while the texture happens to be that - and when it is not,
    // the frontend reads a rectangle that is not the one we drew: too small
    // crops the image, too large leaves whatever was in the rest of the FBO,
    // which is black.
    const unsigned frame_width = static_cast<unsigned>(libretro_get_shared_texture_width());
    const unsigned frame_height = static_cast<unsigned>(libretro_get_shared_texture_height());

    // The frontend sized its window from retro_get_system_av_info(); if the
    // emulator is drawing something else now, say so, or it keeps scaling to
    // the old shape.
    report_frame_size(frame_width, frame_height);

    libretro_check_frame_stall(has_new_frame);

    if (has_new_frame)
    {
        // New frame available - blit from RSX's shared texture to RetroArch's FBO, then present
        // CRITICAL: FBOs are NOT shared between GL contexts, so RSX renders to a shared texture,
        // and we blit that texture to RetroArch's actual FBO here on the main thread.
        libretro_blit_to_frontend();
        video_cb(RETRO_HW_FRAME_BUFFER_VALID, frame_width, frame_height, 0);
        libretro_mark_frame_presented();
    }
    else
    {
        // No new frame - tell RetroArch to reuse the previous frame (frame duping)
        video_cb(NULL, frame_width, frame_height, 0);
    }
}

// A reset is an unload and a load of the same game, done the way those are
// done. Emu.Restart() did it behind the core's back instead: the stop and the
// reload ran on the Emulation Join Thread while retro_run carried on, the
// pause watchdog kept pausing and resuming a machine that was being torn down,
// and between the stop completing and Load() claiming the state, retro_run
// could see a stopped emulator and ask the frontend to shut down. A reload
// that failed left it stopped with nothing said.
void retro_reset(void)
{
    if (!game_loaded)
        return;

    stop_pause_watchdog();

    Emu.GracefulShutdown(false, false);
    wait_for_emulation_stop("restarting");

    // game_path is still what the load booted - for a disc image, its EBOOT
    // inside the image. The stop has dropped the image (Emulator::Kill unloads
    // it unless it switches executables), so it is mounted again the way
    // retro_load_game mounted it; booting the EBOOT without it failed with
    // "Failed to open executable" and the reset closed the content.
    if (!s_iso_path.empty())
        load_iso(s_iso_path);

    if (!do_boot_game(game_path))
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: the game did not boot again after a reset\n");
        libretro_show_message("The game could not be restarted", 300);
        // retro_unload_game does nothing once game_loaded is false
        unload_iso();
        s_iso_path.clear();
        game_loaded = false;
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
    }
}

// Save states. RPCS3 makes a savestate in one way only: it stops the
// emulation, writes the whole machine out zstd-compressed and boots back into
// it, which takes seconds and is hundreds of MB. Here it writes into one
// buffer the core keeps (g_memory_savestate), not a file: the size RetroArch
// asks for before a save is that of a state made right then, the save copies
// it into RetroArch's buffer, and the game is booted back from the core's
// buffer where it lies. A load copies RetroArch's data into that buffer and
// boots it. No disk either way. The buffer keeps its memory from one state to
// the next. The size changes from one state to the next
// (RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE), and the .info file gives
// RetroArch basic save states only, never rewind, run-ahead or netplay, which
// would make one every frame.
static std::vector<u8> s_state;   // the last state made or loaded, compressed
static bool s_state_ready = false; // made for the save to come
static u64 s_state_run = 0;       // made while this frame was the latest

static void forget_state()
{
    s_state_ready = false;
}

// Ends the session when the game cannot be booted again
static void lost_the_game(const char* what)
{
    if (log_cb)
        log_cb(RETRO_LOG_ERROR, "RPCS3: the game did not boot again after %s\n", what);
    libretro_show_message("The game could not be resumed", 300);
    unload_iso();
    s_iso_path.clear();
    game_loaded = false;
    environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
}

// The game booted from the state in s_state, read where it lies
static bool boot_memory_state()
{
    g_memory_savestate = &s_state;
    const bool booted = do_boot_game(memory_savestate_path);
    g_memory_savestate = nullptr;
    return booted;
}

static bool make_state()
{
    forget_state();

    if (!game_loaded || pending_game_boot || !(Emu.IsRunning() || Emu.IsPaused()))
        return false;

    stop_pause_watchdog();
    if (Emu.IsPaused())
        Emu.Resume();

    // The state into s_state
    g_memory_savestate = &s_state;
    Emu.Kill(false, true);

    // Until the emulation has stopped (the state written, or not), or the
    // preparation gave up on it and the game runs on: SPU threads that cannot
    // be paused where a state can be taken, a video decoder or a game save in
    // progress. RPCS3 says which on screen.
    while (!Emu.IsStopped(true) && (Emu.IsClosePending() || Emu.IsStopped()))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    g_memory_savestate = nullptr;

    if (!Emu.IsStopped(true))
    {
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "RPCS3: the game cannot be saved right now\n");
        start_pause_watchdog();
        return false;
    }

    // Written if the stop says so
    const bool written = Emu.GetBoot() == memory_savestate_path && !s_state.empty();

    // Back into the game: from the state, or as it was booted if there is none
    if (!written && !s_iso_path.empty())
        load_iso(s_iso_path);

    if (!(written ? boot_memory_state() : do_boot_game(game_path)))
    {
        lost_the_game("saving a state");
        return false;
    }

    if (!written)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "RPCS3: writing the state failed\n");
        return false;
    }

    s_state_ready = true;
    s_state_run = s_run_count;
    return true;
}

size_t retro_serialize_size(void)
{
    // One state for one save: RetroArch asks for the size and saves within
    // the same frame
    if (s_state_ready && s_state_run == s_run_count)
        return s_state.size();

    return make_state() ? s_state.size() : 0;
}

bool retro_serialize(void* data, size_t size)
{
    if ((!s_state_ready || s_state_run != s_run_count) && !make_state())
        return false;

    forget_state();

    if (!data || size < s_state.size())
        return false;

    // The state into RetroArch's buffer, as RPCS3 wrote it
    std::memcpy(data, s_state.data(), s_state.size());

    if (size > s_state.size())
        std::memset(static_cast<u8*>(data) + s_state.size(), 0, size - s_state.size());

    return true;
}

bool retro_unserialize(const void* data, size_t size)
{
    forget_state();

    // A state this core made: a zstd frame (magic 28 B5 2F FD)
    static constexpr u8 zstd_magic[4] = {0x28, 0xb5, 0x2f, 0xfd};
    if (!game_loaded || pending_game_boot || !data || size < 4 || std::memcmp(data, zstd_magic, 4))
        return false;

    stop_pause_watchdog();
    Emu.GracefulShutdown(false, false);
    wait_for_emulation_stop("loading a state");

    // RetroArch's buffer is not ours past this call: the state into the
    // core's, which the boot reads where it lies. A trailing pad of zeros
    // after the zstd frame (a larger buffer) is left in; the reader stops at
    // the frame's end.
    const u8* bytes = static_cast<const u8*>(data);
    s_state.assign(bytes, bytes + size);

    if (boot_memory_state())
        return true;

    if (log_cb)
        log_cb(RETRO_LOG_ERROR, "RPCS3: the state could not be booted\n");

    // The game as it was booted, rather than nothing
    if (!s_iso_path.empty())
        load_iso(s_iso_path);

    if (!do_boot_game(game_path))
        lost_the_game("a state that would not load");

    return false;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void* retro_get_memory_data(unsigned id)
{
    (void)id;
    return nullptr;
}

size_t retro_get_memory_size(unsigned id)
{
    (void)id;
    return 0;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    libretro_input_set_controller(port, device);
}

// PSN messages have no window to be written or read in here; a game that
// asks gets a cancel, as if the player had closed the window.
namespace
{
    class libretro_sendmessage_dialog : public SendMessageDialogBase
    {
    public:
        error_code Exec(message_data&, std::set<std::string>&) override { return CELL_CANCEL; }
        void callback_handler(rpcn::NotificationType, const std::string&, bool) override {}
    };

    class libretro_recvmessage_dialog : public RecvMessageDialogBase
    {
    public:
        error_code Exec(SceNpBasicMessageMainType, SceNpBasicMessageRecvOptions, SceNpBasicMessageRecvAction&, u64&) override { return CELL_CANCEL; }
        void callback_handler(const shared_ptr<std::pair<std::string, message_data>>, u64) override {}
    };
}

// Initialize emulator callbacks for libretro integration
static void init_emu_callbacks()
{
    emu_callbacks callbacks{};

    callbacks.call_from_main_thread = [](std::function<void()> func, atomic_t<u32>* wake_up)
    {
        // In libretro context, we're single-threaded for the main loop
        // Just execute directly
        func();
        if (wake_up)
        {
            *wake_up = true;
            wake_up->notify_one();
        }

        // Emulator::Kill ends its "Emulation Join Thread" by handing the last
        // reference to that thread over in this functor, for the main thread to
        // destroy once the thread is done. Standalone runs it on the Qt main
        // thread. Here it runs on the caller - which is that thread - so
        // destroying the functor joined the thread from itself, a wait that
        // never ends: "Emulation Join Thread is too sleepy" doubling in the log,
        // an unload that gave up after its timeout, and a crash on the next
        // load (NNshi, ozzfreak). Let the thread return first, and destroy the
        // functor on one that is not it.
        if (thread_ctrl::get_name() == "Emulation Join Thread")
            std::thread([f = std::move(func)]() mutable { f = nullptr; }).detach();
    };

    callbacks.on_install_pkgs = [](const std::vector<std::string>& pkgs, bool /*from_optical_drive*/) -> bool
    {
        // A disc that carries packages in PS3_GAME/INSDIR asks the frontend to
        // install them before the game runs. There is no package installer here
        // and no UI to run one in, so the boot goes on without them - which is
        // what the disc itself does when the data is already installed, and is
        // in any case better than what this was until now: an unset
        // std::function, i.e. a bad_function_call the moment a disc had one.
        for (const std::string& pkg : pkgs)
        {
            if (log_cb)
                log_cb(RETRO_LOG_WARN, "RPCS3: skipping package %s - this core has no installer\n", pkg.c_str());
        }

        return true;
    };

    callbacks.try_to_quit = [](bool force_quit, std::function<void()> on_exit) -> bool
    {
        if (force_quit && on_exit)
        {
            on_exit();
        }
        return force_quit;
    };

    callbacks.init_gs_render = [](utils::serial* ar)
    {
        switch (g_cfg.video.renderer.get())
        {
        case video_renderer::null:
            g_fxo->init<rsx::thread, named_thread<NullGSRender>>(ar);
            break;
#ifndef LIBRETRO_NO_GL
        case video_renderer::opengl:
            g_fxo->init<rsx::thread, named_thread<GLGSRender>>(ar);
            break;
#endif
#ifdef HAVE_VULKAN
        // Missing until now, so asking for Vulkan quietly got the null
        // renderer and no picture at all.
        case video_renderer::vulkan:
            g_fxo->init<rsx::thread, named_thread<VKGSRender>>(ar);
            break;
#endif
        default:
            g_fxo->init<rsx::thread, named_thread<NullGSRender>>(ar);
            break;
        }
    };

    callbacks.get_gs_frame = []() -> std::unique_ptr<GSFrameBase>
    {
        return std::make_unique<LibretroGSFrame>();
    };

    callbacks.close_gs_frame = []() {};

    callbacks.get_camera_handler = []() -> std::shared_ptr<camera_handler_base>
    {
        return std::make_shared<null_camera_handler>();
    };

    callbacks.get_music_handler = []() -> std::shared_ptr<music_handler_base>
    {
        return std::make_shared<null_music_handler>();
    };

    callbacks.get_audio = []() -> std::shared_ptr<AudioBackend>
    {
        return std::make_shared<LibretroAudioBackend>();
    };

    callbacks.get_audio_enumerator = [](u64) -> std::shared_ptr<audio_device_enumerator>
    {
        return nullptr;
    };

    callbacks.init_kb_handler = []()
    {
        g_fxo->init<KeyboardHandlerBase, NullKeyboardHandler>(Emu.DeserialManager());
    };

    callbacks.init_mouse_handler = []()
    {
        g_fxo->init<MouseHandlerBase, LibretroMouseHandler>(Emu.DeserialManager());
    };

    callbacks.init_pad_handler = [](std::string_view)
    {
        // Initialize libretro input polling
        libretro_input_init();

        // Create and initialize the libretro pad handler
        if (!g_libretro_pad_handler)
        {
            g_libretro_pad_handler = std::make_shared<LibretroPadHandler>();
            g_libretro_pad_handler->Init();
        }


    };

    // The overlays' text: dev_flash's fonts are always searched as well, so
    // these only add the system's own, which cover more characters.
    callbacks.get_font_dirs = []() -> std::vector<std::string>
    {
#ifdef _WIN32
        std::string windir = "C:\\Windows";
        if (const char* env = std::getenv("WINDIR"))
            windir = env;
        return { windir + "\\Fonts\\" };
#elif defined(__ANDROID__)
        return { "/system/fonts/" };
#elif defined(__APPLE__)
        return { "/System/Library/Fonts/", "/System/Library/Fonts/Supplemental/", "/Library/Fonts/" };
#else
        std::vector<std::string> dirs;
        if (const char* home = std::getenv("HOME"))
            dirs.push_back(std::string(home) + "/.local/share/fonts/");
        dirs.push_back("/usr/share/fonts/");
        dirs.push_back("/usr/share/fonts/truetype/dejavu/");
        dirs.push_back("/usr/share/fonts/TTF/");
        dirs.push_back("/usr/share/fonts/dejavu/");
        dirs.push_back("/usr/share/fonts/noto/");
        return dirs;
#endif
    };

    callbacks.get_msg_dialog = []() -> std::shared_ptr<MsgDialogBase>
    {
        return nullptr;
    };

    callbacks.get_osk_dialog = []() -> std::shared_ptr<OskDialogBase>
    {
        return nullptr;
    };

    callbacks.get_save_dialog = []() -> std::unique_ptr<SaveDialogBase>
    {
        return std::make_unique<libretro_save_dialog>();
    };
    g_cellsavedata_auto_confirm = true;

    callbacks.get_trophy_notification_dialog = []() -> std::unique_ptr<TrophyNotificationBase>
    {
        return nullptr;
    };

    callbacks.on_run = [](bool) {};
    callbacks.on_pause = []() {};
    callbacks.on_resume = []() {};
    callbacks.on_stop = []() {};
    callbacks.on_ready = []() {};

    callbacks.on_emulation_stop_no_response = [](std::shared_ptr<atomic_t<bool>>, int) {};
    callbacks.on_save_state_progress = [](std::shared_ptr<atomic_t<bool>>, stx::shared_ptr<utils::serial>, stx::atomic_ptr<std::string>*, std::shared_ptr<void>) {};

    callbacks.enable_disc_eject = [](bool) {};
    callbacks.enable_disc_insert = [](bool) {};
    callbacks.on_missing_fw = []() {};
    callbacks.handle_taskbar_progress = [](s32, s32) {};

    // Standalone's English texts (libretro_localized_strings.h), with the
    // argument where standalone puts it. These were empty: the progress dialog
    // for PPU and SPU compilation had no text, nor had message and save dialogs.
    callbacks.get_localized_string = [](localized_string_id id, const char* arg) -> std::string
    {
        const char* text = libretro_localized_template(id);
        if (!text)
            return {};
        std::string result = text;
        if (const usz pos = result.find("%0"); pos != umax)
            result.replace(pos, 2, arg ? arg : "");
        return result;
    };
    callbacks.get_localized_u32string = [](localized_string_id id, const char* arg) -> std::u32string
    {
        return utf8_to_u32string(g_emu_callbacks.get_localized_string(id, arg));
    };
    callbacks.get_localized_setting = [](const cfg::_base*, u32) -> std::string { return {}; };

    callbacks.play_sound = [](const std::string&, std::optional<f32>) {};
    callbacks.add_breakpoint = [](u32) {};

    callbacks.display_sleep_control_supported = []() { return false; };
    callbacks.enable_display_sleep = [](bool) {};

    callbacks.check_microphone_permissions = []() {};
    // Save data dialogs play a game's animated icon (ICON1.PAM) through a
    // video source, and overlay_video ensure()s it gets one: returning none
    // killed the game's thread the moment such a dialog opened, and the game
    // waited on it for good - Project Diva F 2nd's "Pick from list" (NNshi).
    // Standalone decodes it with Qt; here the source never has a frame, so
    // the entry keeps its still icon.
    callbacks.make_video_source = []() -> std::unique_ptr<video_source>
    {
        struct still_video_source final : video_source
        {
            void set_iso_path(const std::string&) override {}
            void set_video_path(const std::string&, bool) override {}
            void set_audio_path(const std::string&, bool) override {}
            void set_active(bool) override {}
            bool get_active() const override { return false; }
            bool has_new() const override { return false; }
            void get_image(std::vector<u8>&, int&, int&, int&, int&) override {}
        };
        return std::make_unique<still_video_source>();
    };

    // Standalone resolves paths with Qt's canonicalFilePath: absolute, links
    // followed, no trailing separator. Without this the default passed paths
    // through unchanged, and the boot path of a title in dev_hdd0/game was cut
    // from "<hdd0>/game/" + '/' - one character too far, so SCUM12000 booted
    // as /dev_hdd0/game/CUM12000/. A path that is not on disk (a disc image's
    // virtual device) keeps its text, less any trailing separator; Qt would
    // have made it empty, which the image boot does not expect.
    callbacks.resolve_path = [](std::string_view path) -> std::string
    {
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::canonical(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
        if (!ec)
        {
            const std::u8string text = canonical.generic_u8string();
            return std::string(text.begin(), text.end());
        }
        while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
            path.remove_suffix(1);
        return std::string(path);
    };

    // Every callback has to be set: an empty one is a std::bad_function_call
    // the first time the emulator reaches for it, which kills the frontend.
    // These five were not. Images are decoded by Qt in standalone; without
    // it, cellPhotoDecode and the media library get a failure to report.
    callbacks.get_image_info = [](const std::string&, std::string&, s32&, s32&, s32&) { return false; };
    callbacks.get_scaled_image = [](const std::string&, s32, s32, s32&, s32&, u8*, bool) { return false; };
    callbacks.get_sendmessage_dialog = []() -> std::shared_ptr<SendMessageDialogBase> { return std::make_shared<libretro_sendmessage_dialog>(); };
    callbacks.get_recvmessage_dialog = []() -> std::shared_ptr<RecvMessageDialogBase> { return std::make_shared<libretro_recvmessage_dialog>(); };
    callbacks.enable_gamemode = [](bool) {};

    callbacks.update_emu_settings = []() {};
    callbacks.save_emu_settings = []() {};

    // Three more since upstream build 20147, the first of which ended every
    // boot right after the title was logged.
    //
    // Standalone reads its downloaded config_database.dat here; the core reads
    // the copy it carries (install_config_database), unless Database Settings
    // Override is off. RPCS3 applies the entry on top of config.yml, where the
    // core options are, so the database wins for the settings it names.
    callbacks.get_database_config = [](const std::string& title_id) { return lookup_database_config(title_id); };

    // Standalone's, without Qt: the photo goes to dev_hdd0/photo/<date>/ under
    // the title and the time, with a counter when that name is taken.
    callbacks.get_photo_path = [](std::string_view title) -> std::string
    {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::string_view extension = ".png";
        if (const auto extension_start = title.find_last_of('.'); extension_start != umax)
        {
            extension = title.substr(extension_start);
            title = title.substr(0, extension_start);
        }

        std::string suffix = std::string(extension);
        const std::string path = vfs::get(fmt::format("/dev_hdd0/photo/%04d/%02d/%02d/%s %02d-%02d-%04d %02d-%02d-%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, vfs::escape(title, true),
            tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec));

        u32 counter = 0;
        while (!Emu.IsStopped() && fs::is_file(path + suffix))
        {
            suffix = fmt::format(" %d%s", ++counter, extension);
        }

        return path + suffix;
    };

    // Like resolve_path, for a path that may not exist yet: the part that does
    // is made canonical, the rest is appended as written. The default passed
    // the text through untouched.
    callbacks.resolve_path_may_not_exist = [](std::string_view path) -> std::string
    {
        std::error_code ec;
        const std::filesystem::path resolved = std::filesystem::weakly_canonical(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
        if (ec)
            return std::string(path);
        std::u8string text = resolved.generic_u8string();
#ifdef _WIN32
        // A path that was absolute without a drive stays without one, as in
        // standalone. Strip the drive before trailing separators so a root
        // such as D:/ becomes / instead of D:.
        if (path.starts_with("/") && !path.starts_with("//") && text.size() >= 3 && text[1] == u8':' && text[2] == u8'/')
            text.erase(0, 2);
#endif
        while (text.size() > 1 && text.back() == u8'/')
            text.pop_back();
        return std::string(text.begin(), text.end());
    };

    g_emu_callbacks = std::move(callbacks);
}

