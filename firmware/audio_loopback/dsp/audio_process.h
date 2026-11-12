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
 * Channels with frame buffers. The play-back path hands over 64 frames of 4
 * channels (FRAMES_PER_BUFFER, NUM_CHANNELS); blocks may be any length, and
 * channels past the maximum are passed through unprocessed.
 */
#define AUDIO_PROCESS_MAX_CHANNELS  (4u)

/*
 * Clears the frame buffers. Call once before the first AudioProcess(), and
 * again to restart from silence.
 */
void AudioProcessInit(void);

/*
 * Produces one output block from one input block.
 *
 * Each channel is converted from sign-extended 24-bit fixed point to float
 * and collected into frames of FFT_N samples (fft_tables.h). Each full frame
 * goes through FFT and IFFT, keeping every bin on even channels and only
 * those up to FFT_KEEP_HZ on odd ones, and is played out, saturated and
 * rounded back, while the next frame fills: the output lags the input by
 * FFT_N samples. Frames carry over between calls, so consecutive blocks must
 * belong to one stream.
 *
 * Parameters
 *  pIn         interleaved source samples, nFrames * nChannels of them
 *  pOut        interleaved destination samples, same count; may be pIn
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
