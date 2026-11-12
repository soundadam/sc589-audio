/*!
 * @file      chain.h
 * @brief     The experiments' processing chains behind AudioProcess().
 *
 *            audio_process.c converts and de-interleaves a block, then hands
 *            each channel to the selected chain as floats; a chain keeps its
 *            own filter state per channel. Channels are numbered from 0 and
 *            are fewer than AUDIO_PROCESS_MAX_CHANNELS; a chunk is at most
 *            AUDIO_PROCESS_MAX_FRAMES frames. Not for use outside dsp/.
 */

#ifndef __CHAIN_H__
#define __CHAIN_H__

#include <stdint.h>

/*
 * E02: 48 kHz -> 8 kHz -> 48 kHz. Even channels are low-passed by the IIR at
 * 8 kHz, odd channels replaced by the test tone.
 */
void ChainE02Init(void);
void ChainE02Channel(uint32_t nChannel, const float *pIn, float *pOut, uint32_t nFrames);

/*
 * E03: 32-point FFT and IFFT frame by frame, FFT_N samples late. Even
 * channels come out unchanged, odd channels keep the bins up to FFT_KEEP_HZ.
 */
void ChainE03Init(void);
void ChainE03Channel(uint32_t nChannel, const float *pIn, float *pOut, uint32_t nFrames);

/*
 * E04: 48 kHz -> 16 kHz -> 48 kHz. Even channels are filtered by the
 * order-255 FIR through overlap-save, odd channels by the order-1023 FIR
 * through the four-partition overlap-save.
 */
void ChainE04Init(void);
void ChainE04Channel(uint32_t nChannel, const float *pIn, float *pOut, uint32_t nFrames);

#endif /* __CHAIN_H__ */
