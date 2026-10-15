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
 *            float array, processed, then converted back and re-interleaved.
 *            The processing runs at the low rate between a decimator and an
 *            interpolator (multirate.h, multirate_coeffs.h): even channels
 *            decimate their input and low-pass it with the IIR, odd channels
 *            replace it with the test tone. A stereo pair thus carries the
 *            filtered input on one side and the tone on the other.
 */

#include "audio_process.h"
#include "biquad.h"
#include "multirate.h"
#include "multirate_coeffs.h"
#include "tone.h"

/* Full scale of a 24-bit sample: 1.0f maps to 2^23 */
#define S24_SCALE       (8388608.0f)
#define S24_INV_SCALE   (1.0f / S24_SCALE)
/* Largest and smallest 24-bit codes, exact in float */
#define S24_MAX         (8388607.0f)
#define S24_MIN         (-8388608.0f)

/* Even channels take the IIR, odd channels the tone */
#define IIR_CHANNELS    ((AUDIO_PROCESS_MAX_CHANNELS + 1u) / 2u)
#define TONE_CHANNELS   (AUDIO_PROCESS_MAX_CHANNELS / 2u)
/* Low-rate samples in one chunk, at most */
#define LOW_FRAMES      MULTIRATE_LOW_FRAMES(AUDIO_PROCESS_MAX_FRAMES, MULTIRATE_FACTOR)

static float DecDelay[IIR_CHANNELS][MULTIRATE_TAPS];
static Decimator Dec[IIR_CHANNELS];

static float IirState[IIR_CHANNELS][RATE8K_IIR_SECTIONS * BIQUAD_STATES];
static BiquadCascade Iir[IIR_CHANNELS];

static Tone Tones[TONE_CHANNELS];

static float IntDelay[AUDIO_PROCESS_MAX_CHANNELS][MULTIRATE_TAPS / MULTIRATE_FACTOR];
static Interpolator Int[AUDIO_PROCESS_MAX_CHANNELS];

/* One channel of the current chunk: input, the low-rate stream, output */
static float ChannelIn[AUDIO_PROCESS_MAX_FRAMES];
static float ChannelLow[LOW_FRAMES];
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

	for (nIndex = 0u; nIndex < IIR_CHANNELS; nIndex++)
	{
		DecimatorInit(&Dec[nIndex], MultirateTaps, DecDelay[nIndex], MULTIRATE_TAPS, MULTIRATE_FACTOR);
		BiquadInit(&Iir[nIndex], Rate8kIirSos, IirState[nIndex], RATE8K_IIR_SECTIONS);
	}
	for (nIndex = 0u; nIndex < TONE_CHANNELS; nIndex++)
	{
		ToneInit(&Tones[nIndex], Rate8kTone, RATE8K_TONE_LEN);
	}
	for (nIndex = 0u; nIndex < AUDIO_PROCESS_MAX_CHANNELS; nIndex++)
	{
		InterpolatorInit(&Int[nIndex], MultirateTaps, IntDelay[nIndex], MULTIRATE_TAPS, MULTIRATE_FACTOR);
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
	uint32_t nLow;

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

			if ((nChannel % 2u) == 0u)
			{
				/* de-interleave and convert, then down to the low rate and filter */
				for (nFrame = 0u; nFrame < nChunk; nFrame++)
				{
					ChannelIn[nFrame] = S24ToFloat(pIn[((nDone + nFrame) * nChannels) + nChannel]);
				}
				nLow = DecimatorProcess(&Dec[nChannel / 2u], ChannelIn, nChunk, ChannelLow);
				BiquadProcess(&Iir[nChannel / 2u], ChannelLow, ChannelLow, nLow);
			}
			else
			{
				/* the input is not used: the tone takes its place */
				nLow = InterpolatorInputs(&Int[nChannel], nChunk);
				ToneProcess(&Tones[nChannel / 2u], ChannelLow, nLow);
			}

			/* the phases line up, so the interpolator takes exactly the nLow samples */
			(void)InterpolatorProcess(&Int[nChannel], ChannelLow, ChannelOut, nChunk);

			/* convert back and re-interleave */
			for (nFrame = 0u; nFrame < nChunk; nFrame++)
			{
				pOut[((nDone + nFrame) * nChannels) + nChannel] = FloatToS24(ChannelOut[nFrame]);
			}
		}
	}
}
