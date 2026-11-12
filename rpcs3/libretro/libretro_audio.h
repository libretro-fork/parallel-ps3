#pragma once

#include "Emu/Audio/AudioBackend.h"
#include "libretro.h"
#include "libretro_audio_out.h"
#include "libretro_audio_ring.h"

#include <retro_atomic.h>


// Asks the frontend for multi-channel output; true when it takes it, and
// from then on all audio goes through it (the frontend's rule: one or the
// other for the loaded game). Call from retro_load_game.
bool libretro_audio_negotiate_multi(retro_environment_t environ_cb);
bool libretro_audio_multi_available();

// retro_run: one frame's worth of the game's audio to the frontend - the
// sample rate over @frame_rate, the frames a second the core declares
void libretro_audio_process(retro_audio_sample_batch_t audio_batch_cb, double frame_rate);

// LibretroAudioBackend - AudioBackend implementation for libretro. Nothing
// is buffered here. cellAudio's ring is lent to retro_run where it lies
// (SetLendRing): the frontend reads the game's samples from the ring's own
// memory. A source with only a write callback (the RSX audio path) is
// pulled from into the buffer retro_run hands the frontend.
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

    // The layout's speakers as the frontend names them, worked out at Open
    unsigned m_speakers = 0;

    // cellAudio's ring, while it has one for this backend; and the bytes of
    // a frame of what retro_run has on loan from it
    lr_audio_ring* m_ring = nullptr;
    size_t m_lent_frame_bytes = 0;

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
    void SetLendRing(lr_audio_ring* ring) override;

    f64 GetCallbackFrameLen() override;

    bool IsPlaying() override { return retro_atomic_load_acquire_int(&m_playing) != 0; }

    void Play() override;
    void Pause() override;

    bool Initialized() override { return retro_atomic_load_acquire_int(&m_initialized) != 0; }

    // retro_run's: up to @max_frames frames of the game's audio into @out
    // (room for max_frames of eight floats), in the backend's format, which
    // @format is told; the frames given, 0 when there is nothing to give
    // this run
    size_t GetSamples(void* out, size_t max_frames, lr_audio_format* format);

    // retro_run's: exactly @frames frames of the ring where they lie, at
    // *@ptr, until Return - or 0 when the ring cannot lend that many in one
    // piece (GetSamples then). Between a Borrow that lends and its Return
    // cellAudio's thread cannot open, close or take the ring away: it waits
    // for the Return.
    size_t Borrow(const void** ptr, size_t frames, lr_audio_format* format);
    void Return(size_t frames);
};

// Global audio backend instance access
LibretroAudioBackend* get_libretro_audio_backend();
