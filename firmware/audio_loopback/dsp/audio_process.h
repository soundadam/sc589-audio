/*!
 * @file      audio_process.h
 * @brief     Sample processing, independent of the codecs and the DMA path.
 *
 *            Nothing declared here knows about the ADAU1979, the ADAU1962A,
 *            SPORT or the ping-pong buffers. audio_path.c hands over one
 *            completed block of interleaved frames; a filter or gain stage
 *            replaces the body of AudioProcess() without touching buffer
 *            submission. Being free of hardware dependencies, this unit also
 *            builds and runs on the host for testing.
 */

#ifndef __AUDIO_PROCESS_H__
#define __AUDIO_PROCESS_H__

#include <stdint.h>

/*
 * Sizes of the per-channel float work buffers. The play-back path hands over
 * 64 frames of 4 channels (FRAMES_PER_BUFFER, NUM_CHANNELS);
 * longer blocks are filtered in chunks, and channels past the maximum are
 * passed through unfiltered.
 */
#define AUDIO_PROCESS_MAX_FRAMES    (64u)
#define AUDIO_PROCESS_MAX_CHANNELS  (4u)

/*
 * Sets up the filters and clears their state. Call once before the first
 * AudioProcess(), and again to restart from silence.
 */
void AudioProcessInit(void);

/*
 * Produces one output block from one input block.
 *
 * Each channel is converted from sign-extended 24-bit fixed point to float,
 * taken down to the low rate and processed there (IIR low-pass on even
 * channels, test tone on odd ones), brought back up to the codec rate, then
 * saturated and rounded back. Filter state carries over between calls, so
 * consecutive blocks must belong to one stream.
 *
 * Parameters
 *  pIn         interleaved source samples, nFrames * nChannels of them
 *  pOut        interleaved destination samples, same count
 *  nFrames     number of frames in the block
 *  nChannels   samples per frame
 *
 * Returns
 *  None
 *
 */
void AudioProcess(const int32_t *pIn, int32_t *pOut,
                  uint32_t nFrames, uint32_t nChannels);

#endif /* __AUDIO_PROCESS_H__ */
