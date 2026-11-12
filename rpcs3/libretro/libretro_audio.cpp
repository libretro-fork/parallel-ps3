#include "stdafx.h"

#include "libretro_audio.h"
#include "libretro_core.h"

#include <audio/conversion/float_to_s16.h>

#include <algorithm>
#include <cstring>
#include <bit>
#include <thread>

LOG_CHANNEL(libretro_audio_log, "LibretroAudio");

static LibretroAudioBackend* s_audio_backend = nullptr;

// RPCS3 can output more than two channels (its Audio Channels setting, which
// the core used to pin to stereo). With a frontend that takes them, they go
// out as they are and the frontend fits them to the user's speakers, its
// Output Layout setting: discrete on a device that has them, folded to
// stereo otherwise.
static retro_audio_sample_multi_callback s_multi{};
static bool s_multi_available = false;

bool libretro_audio_negotiate_multi(retro_environment_t environ_cb)
{
    s_multi = {};
    s_multi_available = environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI, &s_multi) && s_multi.batch_int16;
    return s_multi_available;
}

bool libretro_audio_multi_available()
{
    return s_multi_available;
}

// The speakers of RPCS3's layouts, in the order RPCS3 interleaves them - the
// order cubeb's layouts give them in standalone, ascending speaker bits, which
// is the order the frontend takes. Always as many speakers as channels: the
// frontend refuses a frame whose channel count and mask disagree, which was
// silence when a stereo game ran under a 5.1 layout (NNshi).
static unsigned speaker_mask_of(audio_channel_layout layout, u32 channels);

static unsigned speaker_mask(audio_channel_layout layout, u32 channels)
{
    const unsigned mask = speaker_mask_of(layout, channels);
    return static_cast<u32>(std::popcount(mask)) == channels ? mask : speaker_mask_of(audio_channel_layout::automatic, channels);
}

static unsigned speaker_mask_of(audio_channel_layout layout, u32 channels)
{
    constexpr unsigned FL = RETRO_AUDIO_SPEAKER_FRONT_LEFT, FR = RETRO_AUDIO_SPEAKER_FRONT_RIGHT,
        FC = RETRO_AUDIO_SPEAKER_FRONT_CENTER, LFE = RETRO_AUDIO_SPEAKER_LOW_FREQUENCY,
        BL = RETRO_AUDIO_SPEAKER_BACK_LEFT, BR = RETRO_AUDIO_SPEAKER_BACK_RIGHT,
        SL = RETRO_AUDIO_SPEAKER_SIDE_LEFT, SR = RETRO_AUDIO_SPEAKER_SIDE_RIGHT;
    switch (layout)
    {
    case audio_channel_layout::mono: return FC;
    case audio_channel_layout::stereo_lfe: return FL | FR | LFE;
    case audio_channel_layout::quadraphonic: return FL | FR | BL | BR;
    case audio_channel_layout::quadraphonic_lfe: return FL | FR | LFE | BL | BR;
    case audio_channel_layout::surround_5_1: return FL | FR | FC | LFE | SL | SR;
    case audio_channel_layout::surround_7_1: return FL | FR | FC | LFE | BL | BR | SL | SR;
    default: break;
    }
    switch (channels)
    {
    case 6: return FL | FR | FC | LFE | SL | SR;
    case 8: return FL | FR | FC | LFE | BL | BR | SL | SR;
    case 1: return FC;
    default: return FL | FR;
    }
}

LibretroAudioBackend* get_libretro_audio_backend()
{
    return s_audio_backend;
}

void libretro_audio_process(retro_audio_sample_batch_t audio_batch_cb, double frame_rate)
{
    if (!audio_batch_cb || !s_audio_backend || frame_rate <= 0.0)
        return;

    // Exactly one frame's worth a run: the sample rate over the frames a
    // second the core declares, the fractions carried. No clock: the
    // frontend paces the runs, and with them the audio, as it paces the
    // pictures; fast-forward and slow motion take the audio along.
    //
    // It used to be as much as had played in real time since the last run,
    // read off the host's clock, and before that 2048 frames a run (which held
    // RetroArch to 23.4 runs a second) and then a fixed 800 (48000 / 60).
    // While the emulator's audio is stopped (paused, not started yet) the run
    // hands over silence, so the frontend's audio keeps its pace too.
    static constexpr double SAMPLE_RATE = 48000.0;
    static constexpr size_t FRAMES_PER_BATCH = 512;
    // retro_run's alone: the game's samples as the callback gives them, and
    // as s16 for a frontend that takes no floats. Up to 7.1.
    alignas(64) static union { float f32[FRAMES_PER_BATCH * 8]; int16_t s16[FRAMES_PER_BATCH * 8]; } s_raw;
    alignas(64) static int16_t s_s16[FRAMES_PER_BATCH * 8];
    static double s_owed = 0.0; // the fraction of a frame carried to the next run
    // The format last handed over, for silence while there is no audio
    static u32 s_channels = 2;
    static audio_channel_layout s_layout = audio_channel_layout::automatic;

    s_owed += SAMPLE_RATE / frame_rate;
    const size_t due = static_cast<size_t>(s_owed);
    s_owed -= static_cast<double>(due);

    size_t remaining = due;
    while (remaining > 0)
    {
        const size_t want = std::min(remaining, FRAMES_PER_BATCH);
        bool is_float = false;
        u32 channels = 2;
        audio_channel_layout layout = audio_channel_layout::automatic;
        size_t frames = s_audio_backend->GetSamples(&s_raw, want, &is_float, &channels, &layout);
        if (frames)
        {
            s_channels = channels;
            s_layout = layout;
        }
        if (frames < want)
        {
            // Not playing, or set up again just now: silence for the rest,
            // in the format last handed over
            if (!frames)
            {
                channels = s_channels;
                layout = s_layout;
                is_float = false;
            }
            if (is_float)
                std::fill(s_raw.f32 + frames * channels, s_raw.f32 + want * channels, 0.0f);
            else
                std::fill(s_raw.s16 + frames * channels, s_raw.s16 + want * channels, int16_t{0});
            frames = want;
        }

        const unsigned mask = speaker_mask(layout, channels);
        if (is_float && s_multi_available && s_multi.batch_float)
        {
            // As the game made them: no conversion, no copy
            s_multi.batch_float(s_raw.f32, frames, channels, mask);
        }
        else
        {
            const int16_t* s16 = s_raw.s16;
            if (is_float)
            {
                convert_float_to_s16(s_s16, s_raw.f32, frames * channels);
                s16 = s_s16;
            }
            if (s_multi_available)
                s_multi.batch_int16(s16, frames, channels, mask);
            else if (channels == 2)
                audio_batch_cb(s16, frames);
        }
        remaining -= frames;
    }
}

LibretroAudioBackend::LibretroAudioBackend()
{
    retro_atomic_int_init(&m_access, 0);
    retro_atomic_int_init(&m_playing, 0);
    retro_atomic_int_init(&m_initialized, 0);
    convert_float_to_s16_init_simd();
    s_audio_backend = this;
}

LibretroAudioBackend::~LibretroAudioBackend()
{
    Close();
    if (s_audio_backend == this)
        s_audio_backend = nullptr;
}

// cellAudio's thread: the callbacks and the format to itself, once retro_run
// is out of a pull. Opening, closing and setting a callback are rare.
void LibretroAudioBackend::begin_exclusive()
{
    while (!retro_atomic_cas_int(&m_access, 0, 2))
        std::this_thread::yield();
}

void LibretroAudioBackend::end_exclusive()
{
    retro_atomic_store_release_int(&m_access, 0);
}

bool LibretroAudioBackend::Open(std::string_view dev_id, AudioFreq freq, AudioSampleSize sample_size, AudioChannelCnt ch_cnt, audio_channel_layout layout)
{
    (void)dev_id;

    begin_exclusive();

    // Set base class members - use what the config passes us
    // DO NOT force S16 here - let the config control this via raw.convert_to_s16
    // Floats go to the frontend as they are when it takes them, else as s16
    m_sampling_rate = freq;
    m_sample_size = sample_size;  // Accept what config gives us (usually FLOAT)
    // As many channels as the game configured, up to 8 when the frontend
    // takes multi-channel output and 2 otherwise, as standalone's backends
    // fit the game's to the device
    setup_channel_layout(static_cast<u32>(ch_cnt), libretro_audio_multi_available() ? 8 : 2, layout, libretro_audio_log);

    retro_atomic_store_release_int(&m_initialized, 1);
    end_exclusive();
    return true;
}

void LibretroAudioBackend::Close()
{
    begin_exclusive();
    retro_atomic_store_release_int(&m_playing, 0);
    retro_atomic_store_release_int(&m_initialized, 0);
    end_exclusive();
}

void LibretroAudioBackend::SetWriteCallback(std::function<u32(u32, void*)> cb)
{
    begin_exclusive();
    m_write_callback = std::move(cb);
    end_exclusive();
}

void LibretroAudioBackend::SetStateCallback(std::function<void(AudioStateEvent)> cb)
{
    begin_exclusive();
    m_state_callback = std::move(cb);
    end_exclusive();
}

f64 LibretroAudioBackend::GetCallbackFrameLen()
{
    // How long cellAudio has to hold on to audio before it is collected - it
    // sizes its ring from this. Collection happens once per retro_run, not
    // every 5.3 ms like a sound card callback, and with buffering off the ring
    // was 5.3 ms + two 256-sample blocks = 16 ms: less than one 60 Hz frame.
    // Blocks that did not fit were dropped, RetroArch starved (NNshi: 98 %
    // underrun at 60 fps, fine from 66 fps up). Three frames leave room for a
    // late run; the ring is emptied every run, so this adds no latency.
    return 3.0 / 60.0;
}

void LibretroAudioBackend::Play()
{
    retro_atomic_store_release_int(&m_playing, 1);
}

void LibretroAudioBackend::Pause()
{
    retro_atomic_store_release_int(&m_playing, 0);
}

size_t LibretroAudioBackend::GetSamples(void* out, size_t max_frames, bool* is_float, u32* channels, audio_channel_layout* layout)
{
    if (!IsPlaying() || !Initialized())
        return 0;

    // Never waits: while cellAudio's thread is setting things up, this run
    // gives nothing and the time is owed to the next
    if (!retro_atomic_cas_int(&m_access, 0, 1))
        return 0;

    size_t frames = 0;
    const u32 ch = get_channels();
    const size_t bytes_per_frame = static_cast<size_t>(ch) * get_sample_size();

    if (Initialized() && m_write_callback && ch && ch <= 8)
    {
        // Exactly what is due, straight from cellAudio's ring; it pads a
        // short one with silence, as it does for a sound card
        const u32 bytes = m_write_callback(static_cast<u32>(max_frames * bytes_per_frame), out);
        frames = bytes / bytes_per_frame;
        *is_float = m_sample_size == AudioSampleSize::FLOAT;
        *channels = ch;
        *layout = get_channel_layout();
    }

    retro_atomic_store_release_int(&m_access, 0);
    return frames;
}
