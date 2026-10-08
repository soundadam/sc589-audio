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
 *            Each block is de-interleaved into one contiguous float array per
 *            channel, filtered channel by channel, then converted back and
 *            re-interleaved. The contiguous per-channel layout is what the
 *            CCES run-time fir() in filter.h expects, so FirBlock() can later
 *            be replaced by that call without touching the rest.
 */

#include <string.h>
#include "audio_process.h"

/* Full scale of a 24-bit sample: 1.0f maps to 2^23 */
#define S24_SCALE       (8388608.0f)
#define S24_INV_SCALE   (1.0f / S24_SCALE)
/* Largest and smallest 24-bit codes, exact in float */
#define S24_MAX         (8388607.0f)
#define S24_MIN         (-8388608.0f)

/* Number of FIR coefficients */
#define FIR_TAPS        (31u)
/* Past input samples each channel carries from one block into the next */
#define FIR_HISTORY     (FIR_TAPS - 1u)

/*
 * Low-pass, 31 taps, windowed sinc: cut-off 4 kHz at fs = 48 kHz (-6 dB),
 * Hamming window, normalised to unity gain at DC. About -1.9 dB at 3 kHz,
 * -29.5 dB at 6 kHz and below -52 dB from 8 kHz up; group delay 15 samples.
 *
 * The response is symmetric, so the order of the table does not matter,
 * which also holds for fir() in filter.h (it takes coefficients reversed).
 */
static const float FirCoeffs[FIR_TAPS] =
{
	 1.692246424e-03f,  1.767507786e-03f,  1.461626058e-03f,  0.0f,
	-3.348916074e-03f, -8.518383780e-03f, -1.402633194e-02f, -1.689652016e-02f,
	-1.332832381e-02f,  0.0f,              2.443180774e-02f,  5.824101969e-02f,
	 9.647368011e-02f,  1.319292809e-01f,  1.570533550e-01f,  1.661359041e-01f,
	 1.570533550e-01f,  1.319292809e-01f,  9.647368011e-02f,  5.824101969e-02f,
	 2.443180774e-02f,  0.0f,             -1.332832381e-02f, -1.689652016e-02f,
	-1.402633194e-02f, -8.518383780e-03f, -3.348916074e-03f,  0.0f,
	 1.461626058e-03f,  1.767507786e-03f,  1.692246424e-03f
};

/*
 * Per-channel working buffer. The first FIR_HISTORY entries hold the tail of
 * the previous block, the rest the current block, so that every output sample
 * sees FIR_TAPS contiguous inputs without wrapping an index.
 */
static float FirWork[AUDIO_PROCESS_MAX_CHANNELS][FIR_HISTORY + AUDIO_PROCESS_MAX_FRAMES];
/* Per-channel filtered output of the current block */
static float FirOut[AUDIO_PROCESS_MAX_CHANNELS][AUDIO_PROCESS_MAX_FRAMES];

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
 * Filters one block of one channel.
 *
 * Parameters
 *  pWork       FIR_HISTORY past samples followed by nFrames new ones; on
 *              return the last FIR_HISTORY samples are moved to the front
 *              ready for the next block
 *  pOut        nFrames filtered samples
 *  nFrames     number of new samples in pWork
 *
 * Returns
 *  None
 *
 */
static void FirBlock(float *pWork, float *pOut, uint32_t nFrames)
{
	uint32_t nFrame;
	uint32_t nTap;
	float fAcc;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		/* pWork[nFrame + FIR_HISTORY] is the newest input for this output */
		fAcc = 0.0f;
		for (nTap = 0u; nTap < FIR_TAPS; nTap++)
		{
			fAcc += FirCoeffs[nTap] * pWork[nFrame + FIR_HISTORY - nTap];
		}
		pOut[nFrame] = fAcc;
	}

	memmove(pWork, &pWork[nFrames], FIR_HISTORY * sizeof(float));
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
	uint32_t nDone;
	uint32_t nChunk;
	uint32_t nFrame;
	uint32_t nChannel;
	uint32_t nIndex;

	/* blocks longer than the work buffers are taken a chunk at a time */
	for (nDone = 0u; nDone < nFrames; nDone += nChunk)
	{
		nChunk = nFrames - nDone;
		if (nChunk > AUDIO_PROCESS_MAX_FRAMES)
		{
			nChunk = AUDIO_PROCESS_MAX_FRAMES;
		}

		/* de-interleave and convert, after each channel's history */
		for (nFrame = 0u; nFrame < nChunk; nFrame++)
		{
			for (nChannel = 0u; nChannel < nChannels; nChannel++)
			{
				nIndex = ((nDone + nFrame) * nChannels) + nChannel;
				if (nChannel < AUDIO_PROCESS_MAX_CHANNELS)
				{
					FirWork[nChannel][FIR_HISTORY + nFrame] = S24ToFloat(pIn[nIndex]);
				}
				else
				{
					/* no filter state for this channel: pass it through */
					pOut[nIndex] = pIn[nIndex];
				}
			}
		}

		for (nChannel = 0u; (nChannel < nChannels) && (nChannel < AUDIO_PROCESS_MAX_CHANNELS); nChannel++)
		{
			FirBlock(FirWork[nChannel], FirOut[nChannel], nChunk);
		}

		/* convert back and re-interleave */
		for (nFrame = 0u; nFrame < nChunk; nFrame++)
		{
			for (nChannel = 0u; (nChannel < nChannels) && (nChannel < AUDIO_PROCESS_MAX_CHANNELS); nChannel++)
			{
				nIndex = ((nDone + nFrame) * nChannels) + nChannel;
				pOut[nIndex] = FloatToS24(FirOut[nChannel][nFrame]);
			}
		}
	}
}
