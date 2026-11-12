/*!
 * @file      audio_process.c
 * @brief     Sample processing, independent of the codecs and the DMA path.
 *
 *            Samples arrive as fixed point and are processed as float. In
 *            I2S mode both codec drivers program the SPORT with SLEN = 23 and
 *            DTYPE = sign fill, so each int32_t already holds a sign-extended
 *            24-bit value in [-2^23, 2^23 - 1]; no shifting or masking is
 *            needed before the conversion.
 *
 *            E03: every channel is cut into frames of FFT_N samples. A full
 *            frame is transformed (FftRealForward), its spectrum optionally
 *            weighted, and transformed back (FftRealInverse); the result is
 *            played out while the next frame fills, so the output lags the
 *            input by exactly FFT_N samples. Even channels leave the spectrum
 *            as it is, and come out as their input delayed. Odd channels keep
 *            only the bins up to FFT_KEEP_HZ (FftBinGain), a low-pass applied
 *            frame by frame with no overlap between frames.
 */

#include "audio_process.h"
#include "fft.h"

/* Full scale of a 24-bit sample: 1.0f maps to 2^23 */
#define S24_SCALE       (8388608.0f)
#define S24_INV_SCALE   (1.0f / S24_SCALE)
/* Largest and smallest 24-bit codes, exact in float */
#define S24_MAX         (8388607.0f)
#define S24_MIN         (-8388608.0f)

/* The frame each channel is filling, and the one it is playing out */
static float FrameIn[AUDIO_PROCESS_MAX_CHANNELS][FFT_N];
static float FrameOut[AUDIO_PROCESS_MAX_CHANNELS][FFT_N];
/* Position in both frames; the same for every channel */
static uint32_t nPos;

/* Spectrum of the frame being processed */
static Cplx Spectrum[FFT_REAL_BINS];

/*
 * Converts a sign-extended 24-bit sample to float in [-1.0, 1.0).
 */
static inline float S24ToFloat(int32_t nSample)
{
	return (float)nSample * S24_INV_SCALE;
}

/*
 * Converts a float back to a sign-extended 24-bit sample.
 *
 * Out-of-range values saturate to full scale rather than wrapping, and the
 * result is rounded half away from zero rather than truncated.
 */
static inline int32_t FloatToS24(float fSample)
{
	float fScaled = fSample * S24_SCALE;

	if (fScaled > S24_MAX)
	{
		fScaled = S24_MAX;
	}
	else if (fScaled < S24_MIN)
	{
		fScaled = S24_MIN;
	}

	return (int32_t)((fScaled >= 0.0f) ? (fScaled + 0.5f) : (fScaled - 0.5f));
}

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

/*
 * Clears the frames, so the first FFT_N output samples are silence.
 *
 * Parameters
 *  None
 *
 * Returns
 *  None
 *
 */
void AudioProcessInit(void)
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
	}
	nPos = 0u;
}

/*
 * Produces one output block from one input block.
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
                  uint32_t nFrames, uint32_t nChannels)
{
	uint32_t nActive = (nChannels < AUDIO_PROCESS_MAX_CHANNELS) ? nChannels : AUDIO_PROCESS_MAX_CHANNELS;
	uint32_t nFrame;
	uint32_t nChannel;
	uint32_t nIndex;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		for (nChannel = 0u; nChannel < nChannels; nChannel++)
		{
			nIndex = (nFrame * nChannels) + nChannel;
			if (nChannel >= AUDIO_PROCESS_MAX_CHANNELS)
			{
				/* no frame buffers for this channel: pass it through */
				pOut[nIndex] = pIn[nIndex];
			}
			else
			{
				/* play out the last frame's sample at this position, then replace it */
				pOut[nIndex] = FloatToS24(FrameOut[nChannel][nPos]);
				FrameIn[nChannel][nPos] = S24ToFloat(pIn[nIndex]);
			}
		}

		nPos++;
		if (nPos == FFT_N)
		{
			for (nChannel = 0u; nChannel < nActive; nChannel++)
			{
				ProcessFrame(nChannel);
			}
			nPos = 0u;
		}
	}
}
