/* cellAudio's mixer: one port's block of the game's samples (big-endian
 * floats, as they lie in the PS3's memory) added into the block cellAudio
 * hands on, at the port's volume. C89.
 *
 * A port's volume moves to a new level in steps, one a frame
 * (cellAudioSetPortLevel spreads a change over 13 ms). While it is moving
 * each frame is mixed at its own volume, one by one. Once it is there - all
 * of the time but those 13 ms - the volume is one number for the block:
 * the samples are swapped, scaled and added four at a time where the
 * machine has SSE2 or NEON, with no look at the level a frame.
 *
 * Every sample is the same number the mixer made a frame at a time: a
 * multiply and an add each, in the same order. */

#ifndef CELLAUDIO_MIX_H
#define CELLAUDIO_MIX_H

#ifdef __cplusplus
extern "C" {
#endif

/* The layouts a port of eight channels is mixed down to (cellAudio's
 * "downmix" setting) */
enum cell_audio_mix_down
{
   CELL_AUDIO_MIX_STEREO = 0,
   CELL_AUDIO_MIX_5_1,
   CELL_AUDIO_MIX_7_1
};

/* A port's volume: where it is, where it is going, and by how much a
 * frame (0: it is there) */
struct cell_audio_mix_level
{
   float level;
   float target;
   float step;
};

/**
 * cell_audio_mix_port:
 * @out          : the block mixed into: @frames frames of @out_channels
 *                 floats, added to
 * @out_channels : 2, 6 or 8
 * @in           : the port's block: @frames frames of @in_channels
 *                 big-endian floats, at any address
 * @in_channels  : 1, 2 or 8
 * @down         : what eight channels are mixed down to
 * @master       : the volume all ports are scaled by
 * @level        : the port's volume; its level is left where the block
 *                 took it, and its step 0 once the target was reached
 *
 * Returns 1 when the volume reached its target in this block (it was
 * moving and is not any more), else 0.
 */
int cell_audio_mix_port(float *out, unsigned out_channels, const void *in,
      unsigned in_channels, enum cell_audio_mix_down down, unsigned frames,
      float master, struct cell_audio_mix_level *level);

#ifdef __cplusplus
}
#endif

#endif
