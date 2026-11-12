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
 *            float array, run through the selected experiment's chain
 *            (chain.h), then converted back and re-interleaved.
 */

#include "audio_process.h"
#include "chain.h"

/* Full scale of a 24-bit sample: 1.0f maps to 2^23 */
#define S24_SCALE       (8388608.0f)
#define S24_INV_SCALE   (1.0f / S24_SCALE)
/* Largest and smallest 24-bit codes, exact in float */
#define S24_MAX         (8388607.0f)
#define S24_MIN         (-8388608.0f)

typedef struct
{
	uint32_t nExperiment;
	void (*pInit)(void);
	void (*pChannel)(uint32_t nChannel, const float *pIn, float *pOut, uint32_t nFrames);
} Chain;

static const Chain Chains[] =
{
	{ AUDIO_PROCESS_E02, ChainE02Init, ChainE02Channel },
	{ AUDIO_PROCESS_E03, ChainE03Init, ChainE03Channel },
	{ AUDIO_PROCESS_E04, ChainE04Init, ChainE04Channel },
};

static const Chain *pSelected = &Chains[2];

/* One channel of the current chunk: input and output */
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

int32_t AudioProcessSelect(uint32_t nExperiment)
{
	uint32_t nIndex;

	for (nIndex = 0u; nIndex < sizeof(Chains) / sizeof(Chains[0]); nIndex++)
	{
		if (Chains[nIndex].nExperiment == nExperiment)
		{
			pSelected = &Chains[nIndex];
			AudioProcessInit();
			return 0;
		}
	}
	return -1;
}

/*
 * Sets up the selected chain's filters and clears their state.
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
	pSelected->pInit();
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

			/* de-interleave and convert, then the chain */
			for (nFrame = 0u; nFrame < nChunk; nFrame++)
			{
				ChannelIn[nFrame] = S24ToFloat(pIn[((nDone + nFrame) * nChannels) + nChannel]);
			}
			pSelected->pChannel(nChannel, ChannelIn, ChannelOut, nChunk);

			/* convert back and re-interleave */
			for (nFrame = 0u; nFrame < nChunk; nFrame++)
			{
				pOut[((nDone + nFrame) * nChannels) + nChannel] = FloatToS24(ChannelOut[nFrame]);
			}
		}
	}
}
