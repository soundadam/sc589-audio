/*!
 * @file      chain_e03.c
 * @brief     E03's chain: 32-point FFT and IFFT frame by frame at 48 kHz.
 *
 *            Every channel is cut into frames of FFT_N samples. A full frame
 *            is transformed (FftRealForward), its spectrum optionally
 *            weighted, and transformed back (FftRealInverse); the result is
 *            played out while the next frame fills, so the output lags the
 *            input by exactly FFT_N samples. Even channels leave the spectrum
 *            as it is, and come out as their input delayed. Odd channels keep
 *            only the bins up to FFT_KEEP_HZ (FftBinGain), a low-pass applied
 *            frame by frame with no overlap between frames.
 */

#include "audio_process.h"
#include "chain.h"
#include "fft.h"

/* The frame each channel is filling, and the one it is playing out */
static float FrameIn[AUDIO_PROCESS_MAX_CHANNELS][FFT_N];
static float FrameOut[AUDIO_PROCESS_MAX_CHANNELS][FFT_N];
/* Each channel's position in both frames */
static uint32_t FramePos[AUDIO_PROCESS_MAX_CHANNELS];

/* Spectrum of the frame being processed */
static Cplx Spectrum[FFT_REAL_BINS];

/*
 * FFT, spectrum weighting on odd channels, IFFT: FrameIn[nChannel] to
 * FrameOut[nChannel].
 */
static void ProcessFrame(uint32_t nChannel)
{
	uint32_t k;

	FftRealForward(FrameIn[nChannel], Spectrum);
	if ((nChannel % 2u) != 0u)
	{
		for (k = 0u; k < FFT_REAL_BINS; k++)
		{
			Spectrum[k] = CplxScale(Spectrum[k], FftBinGain[k]);
		}
	}
	FftRealInverse(Spectrum, FrameOut[nChannel]);
}

void ChainE03Init(void)
{
	uint32_t nChannel;
	uint32_t n;

	for (nChannel = 0u; nChannel < AUDIO_PROCESS_MAX_CHANNELS; nChannel++)
	{
		for (n = 0u; n < FFT_N; n++)
		{
			FrameIn[nChannel][n] = 0.0f;
			FrameOut[nChannel][n] = 0.0f;
		}
		FramePos[nChannel] = 0u;
	}
}

void ChainE03Channel(uint32_t nChannel, const float *pIn, float *pOut, uint32_t nFrames)
{
	uint32_t nPos = FramePos[nChannel];
	uint32_t nFrame;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		/* take the input first, then play out the last frame's sample here */
		FrameIn[nChannel][nPos] = pIn[nFrame];
		pOut[nFrame] = FrameOut[nChannel][nPos];

		nPos++;
		if (nPos == FFT_N)
		{
			ProcessFrame(nChannel);
			nPos = 0u;
		}
	}
	FramePos[nChannel] = nPos;
}
