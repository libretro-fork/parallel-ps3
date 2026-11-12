#pragma once

#include "Emu/Audio/AudioBackend.h"
#include "libretro.h"

#include <retro_atomic.h>


// Multi-channel audio (RetroArch since 10 September 2026), from the current
// libretro.h, which the core's copy predates
#ifndef RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI
#define RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI (94 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_AUDIO_SPEAKER_FRONT_LEFT    0x001
#define RETRO_AUDIO_SPEAKER_FRONT_RIGHT   0x002
#define RETRO_AUDIO_SPEAKER_FRONT_CENTER  0x004
#define RETRO_AUDIO_SPEAKER_LOW_FREQUENCY 0x008
#define RETRO_AUDIO_SPEAKER_BACK_LEFT     0x010
#define RETRO_AUDIO_SPEAKER_BACK_RIGHT    0x020
#define RETRO_AUDIO_SPEAKER_SIDE_LEFT     0x200
#define RETRO_AUDIO_SPEAKER_SIDE_RIGHT    0x400
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_multi_int16_t)(
      const int16_t *data, size_t frames, unsigned channels, unsigned layout);
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_multi_float_t)(
      const float *data, size_t frames, unsigned channels, unsigned layout);
struct retro_audio_sample_multi_callback
{
   retro_audio_sample_batch_multi_int16_t batch_int16;
   retro_audio_sample_batch_multi_float_t batch_float;
};
#endif

// Asks the frontend for multi-channel output; true when it takes it, and
// from then on all audio goes through it (the frontend's rule: one or the
// other for the loaded game). Call from retro_load_game.
bool libretro_audio_negotiate_multi(retro_environment_t environ_cb);
bool libretro_audio_multi_available();

// retro_run: one frame's worth of the game's audio to the frontend - the
// sample rate over @frame_rate, the frames a second the core declares
void libretro_audio_process(retro_audio_sample_batch_t audio_batch_cb, double frame_rate);

// LibretroAudioBackend - AudioBackend implementation for libretro. Nothing
// is buffered here: retro_run pulls what is due straight from cellAudio's
// ring (the write callback) into the buffer it hands the frontend.
class LibretroAudioBackend : public AudioBackend
{
private:
    retro_atomic_int_t m_playing;
    retro_atomic_int_t m_initialized;

    // Who is using the callbacks and the format: 1 while retro_run pulls,
    // 2 while cellAudio's thread opens, closes or sets them. retro_run never
    // waits for it - it pulls nothing that run and the time is owed to the
    // next - and the other side waits only out of a pull.
    retro_atomic_int_t m_access;

    void begin_exclusive();
    void end_exclusive();

    // Note: We use the base class m_sampling_rate, m_sample_size, m_channels, m_layout
    // DO NOT shadow them here or audio format detection will break!

public:
    LibretroAudioBackend();
    ~LibretroAudioBackend() override;

    std::string_view GetName() const override { return "Libretro"; }

    bool Open(std::string_view dev_id, AudioFreq freq, AudioSampleSize sample_size, AudioChannelCnt ch_cnt, audio_channel_layout layout) override;
    void Close() override;

    void SetWriteCallback(std::function<u32(u32, void*)> cb) override;
    void SetStateCallback(std::function<void(AudioStateEvent)> cb) override;

    f64 GetCallbackFrameLen() override;

    bool IsPlaying() override { return retro_atomic_load_acquire_int(&m_playing) != 0; }

    void Play() override;
    void Pause() override;

    bool Initialized() override { return retro_atomic_load_acquire_int(&m_initialized) != 0; }

    // retro_run's: up to @max_frames frames of the game's audio into @out
    // (room for max_frames of eight floats), in the backend's format - floats
    // when *@is_float, s16 otherwise - and @channels and @layout as they are
    // for them; the frames given, 0 when there is nothing to give this run
    size_t GetSamples(void* out, size_t max_frames, bool* is_float, u32* channels, audio_channel_layout* layout);
};

// Global audio backend instance access
LibretroAudioBackend* get_libretro_audio_backend();
