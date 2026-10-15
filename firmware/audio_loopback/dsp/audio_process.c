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
 *            Each channel of a block is de-interleaved into a contiguous
 *            float array, filtered, then converted back and re-interleaved.
 *            Even channels run the low-pass FIR, odd channels the low-pass
 *            IIR (lowpass_coeffs.h), so a stereo pair carries one of each
 *            for the same input.
 */

#include "audio_process.h"
#include "fir.h"
#include "biquad.h"
#include "lowpass_coeffs.h"

/* Full scale of a 24-bit sample: 1.0f maps to 2^23 */
#define S24_SCALE       (8388608.0f)
#define S24_INV_SCALE   (1.0f / S24_SCALE)
/* Largest and smallest 24-bit codes, exact in float */
#define S24_MAX         (8388607.0f)
#define S24_MIN         (-8388608.0f)

/* Even channels take the FIR, odd channels the IIR */
#define FIR_CHANNELS    ((AUDIO_PROCESS_MAX_CHANNELS + 1u) / 2u)
#define IIR_CHANNELS    (AUDIO_PROCESS_MAX_CHANNELS / 2u)

static float FirDelay[FIR_CHANNELS][LOWPASS_FIR_TAPS];
static FirFilter Fir[FIR_CHANNELS];

static float IirState[IIR_CHANNELS][LOWPASS_IIR_SECTIONS * BIQUAD_STATES];
static BiquadCascade Iir[IIR_CHANNELS];

/* One channel of the current chunk, before and after filtering */
static float ChannelIn[AUDIO_PROCESS_MAX_FRAMES];
static float ChannelOut[AUDIO_PROCESS_MAX_FRAMES];

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
 * Sets up every channel's filter and clears its state.
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
	uint32_t nIndex;

	for (nIndex = 0u; nIndex < FIR_CHANNELS; nIndex++)
	{
		FirInit(&Fir[nIndex], LowpassFirCoeffs, FirDelay[nIndex], LOWPASS_FIR_TAPS);
	}
	for (nIndex = 0u; nIndex < IIR_CHANNELS; nIndex++)
	{
		BiquadInit(&Iir[nIndex], LowpassIirSos, IirState[nIndex], LOWPASS_IIR_SECTIONS);
	}
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

		for (nChannel = 0u; nChannel < nChannels; nChannel++)
		{
			if (nChannel >= AUDIO_PROCESS_MAX_CHANNELS)
			{
				/* no filter state for this channel: pass it through */
				for (nFrame = 0u; nFrame < nChunk; nFrame++)
				{
					nIndex = ((nDone + nFrame) * nChannels) + nChannel;
					pOut[nIndex] = pIn[nIndex];
				}
				continue;
			}

			/* de-interleave and convert */
			for (nFrame = 0u; nFrame < nChunk; nFrame++)
			{
				ChannelIn[nFrame] = S24ToFloat(pIn[((nDone + nFrame) * nChannels) + nChannel]);
			}

			if ((nChannel % 2u) == 0u)
			{
				FirProcess(&Fir[nChannel / 2u], ChannelIn, ChannelOut, nChunk);
			}
			else
			{
				BiquadProcess(&Iir[nChannel / 2u], ChannelIn, ChannelOut, nChunk);
			}

			/* convert back and re-interleave */
			for (nFrame = 0u; nFrame < nChunk; nFrame++)
			{
				pOut[((nDone + nFrame) * nChannels) + nChannel] = FloatToS24(ChannelOut[nFrame]);
			}
		}
	}
}
