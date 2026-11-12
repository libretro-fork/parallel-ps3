#include "stdafx.h"

#include "libretro_audio.h"
#include "libretro_core.h"

#include <audio/conversion/float_to_s16.h>

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
    lr_audio_out_set_multi(s_multi_available ? s_multi.batch_int16 : nullptr, s_multi_available ? s_multi.batch_float : nullptr);
    return s_multi_available;
}

bool libretro_audio_multi_available()
{
    return s_multi_available;
}

// The speakers of RPCS3's layouts, in the order RPCS3 interleaves them - the
// order cubeb's layouts give them in standalone, ascending speaker bits, which
// is the order the frontend takes. 0 for a layout that names none: the
// usual speakers for the channel count then (libretro_audio_out.c), as for a
// layout whose speakers are not as many as the channels - the frontend
// refuses a frame whose channel count and mask disagree, which was silence
// when a stereo game ran under a 5.1 layout (NNshi).
static unsigned speakers_of(audio_channel_layout layout)
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
    default: return 0;
    }
}

LibretroAudioBackend* get_libretro_audio_backend()
{
    return s_audio_backend;
}

static size_t pull_samples(void* ctx, void* out, size_t max_frames, lr_audio_format* format)
{
    return static_cast<LibretroAudioBackend*>(ctx)->GetSamples(out, max_frames, format);
}

static size_t borrow_samples(void* ctx, const void** ptr, size_t frames, lr_audio_format* format)
{
    return static_cast<LibretroAudioBackend*>(ctx)->Borrow(ptr, frames, format);
}

static void return_samples(void* ctx, size_t frames)
{
    static_cast<LibretroAudioBackend*>(ctx)->Return(frames);
}

void libretro_audio_process(retro_audio_sample_batch_t audio_batch_cb, double frame_rate)
{
    // The pacing, the silence and the hand-over are libretro_audio_out.c's
    if (s_audio_backend)
        lr_audio_out_run_lent(audio_batch_cb, pull_samples, borrow_samples, return_samples, s_audio_backend, frame_rate);
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
    m_speakers = speakers_of(get_channel_layout());

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

void LibretroAudioBackend::SetLendRing(lr_audio_ring* ring)
{
    // Once retro_run is out of a pull or a loan: the ring it was given
    // before is not touched again after this returns
    begin_exclusive();
    m_ring = ring;
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

size_t LibretroAudioBackend::GetSamples(void* out, size_t max_frames, lr_audio_format* format)
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

    if (Initialized() && (m_ring || m_write_callback) && ch && ch <= LR_AUDIO_OUT_CHANNELS)
    {
        // What is there of what is due, copied out: of cellAudio's ring when
        // it could not be lent (less than is due, or in two pieces), else
        // from the write callback
        const size_t bytes = m_ring
            ? lr_audio_ring_read(m_ring, out, max_frames * bytes_per_frame)
            : m_write_callback(static_cast<u32>(max_frames * bytes_per_frame), out);
        frames = bytes / bytes_per_frame;
        format->is_float = m_sample_size == AudioSampleSize::FLOAT;
        format->channels = ch;
        format->speakers = m_speakers;
    }

    retro_atomic_store_release_int(&m_access, 0);
    return frames;
}

size_t LibretroAudioBackend::Borrow(const void** ptr, size_t frames, lr_audio_format* format)
{
    if (!IsPlaying() || !Initialized())
        return 0;

    if (!retro_atomic_cas_int(&m_access, 0, 1))
        return 0;

    const u32 ch = get_channels();
    const size_t bytes_per_frame = static_cast<size_t>(ch) * get_sample_size();

    if (Initialized() && m_ring && ch && ch <= LR_AUDIO_OUT_CHANNELS
        && lr_audio_ring_borrow(m_ring, ptr) >= frames * bytes_per_frame)
    {
        format->is_float = m_sample_size == AudioSampleSize::FLOAT;
        format->channels = ch;
        format->speakers = m_speakers;
        m_lent_frame_bytes = bytes_per_frame;
        return frames; // m_access stays taken until Return
    }

    retro_atomic_store_release_int(&m_access, 0);
    return 0;
}

void LibretroAudioBackend::Return(size_t frames)
{
    lr_audio_ring_consume(m_ring, frames * m_lent_frame_bytes);
    retro_atomic_store_release_int(&m_access, 0);
}
