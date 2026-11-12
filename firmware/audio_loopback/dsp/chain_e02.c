/*!
 * @file      chain_e02.c
 * @brief     E02's chain: 48 kHz -> 8 kHz -> 48 kHz around an IIR or a tone.
 *
 *            The processing runs at the low rate between a decimator and an
 *            interpolator (multirate.h, multirate_coeffs.h): even channels
 *            decimate their input and low-pass it with the IIR, odd channels
 *            replace it with the test tone. A stereo pair thus carries the
 *            filtered input on one side and the tone on the other.
 */

#include "audio_process.h"
#include "biquad.h"
#include "chain.h"
#include "multirate.h"
#include "multirate_coeffs.h"
#include "tone.h"

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

/* The low-rate stream of the current chunk */
static float ChannelLow[LOW_FRAMES];

void ChainE02Init(void)
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

void ChainE02Channel(uint32_t nChannel, const float *pIn, float *pOut, uint32_t nFrames)
{
	uint32_t nLow;

	if ((nChannel % 2u) == 0u)
	{
		/* down to the low rate and filter */
		nLow = DecimatorProcess(&Dec[nChannel / 2u], pIn, nFrames, ChannelLow);
		BiquadProcess(&Iir[nChannel / 2u], ChannelLow, ChannelLow, nLow);
	}
	else
	{
		/* the input is not used: the tone takes its place */
		nLow = InterpolatorInputs(&Int[nChannel], nFrames);
		ToneProcess(&Tones[nChannel / 2u], ChannelLow, nLow);
	}

	/* the phases line up, so the interpolator takes exactly the nLow samples */
	(void)InterpolatorProcess(&Int[nChannel], ChannelLow, pOut, nFrames);
}
