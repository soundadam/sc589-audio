/*!
 * @file      chain_e04.c
 * @brief     E04's chain: 48 kHz -> 16 kHz -> 48 kHz around overlap-save FIR filters.
 *
 *            Every channel is decimated 3:1 to 16 kHz, filtered there and
 *            interpolated 1:3 back to the codec rate (multirate.h,
 *            ols_coeffs.h). Even channels run the order-255 FIR as plain
 *            overlap-save, one 512-point FFT and one inverse per 256 samples;
 *            odd channels run the order-1023 FIR as four partitions of 256
 *            taps, still one FFT and one inverse per 256 samples. Each adds
 *            256 low-rate samples (16 ms) of delay to the filter's own.
 *
 *            The FFT tables and the filter spectra are shared by all
 *            channels; each channel keeps its own frames and delay line.
 *            The channels' frame boundaries are a quarter hop apart
 *            (OlsStagger()), so with 64-frame blocks the FFTs of at most one
 *            channel fall into any block instead of all four into every
 *            twelfth.
 */

#include "audio_process.h"
#include "chain.h"
#include "fft.h"
#include "multirate.h"
#include "ols.h"
#include "ols_coeffs.h"

/* Even channels take the order-255 filter, odd channels the order-1023 one */
#define SHORT_CHANNELS  ((AUDIO_PROCESS_MAX_CHANNELS + 1u) / 2u)
#define LONG_CHANNELS   (AUDIO_PROCESS_MAX_CHANNELS / 2u)
#define SHORT_PARTS     OLS_PARTS(OLS_FIR255_TAPS, OLS_HOP)
#define LONG_PARTS      OLS_PARTS(OLS_FIR1023_TAPS, OLS_HOP)
/* Low-rate samples in one chunk, at most */
#define LOW16K_FRAMES      MULTIRATE_LOW_FRAMES(AUDIO_PROCESS_MAX_FRAMES, OLS_FACTOR)

#if (OLS_FFT_LEN != 2u * OLS_HOP) || (LONG_PARTS != OLS_FIR1023_PARTS)
#error "ols_coeffs.h does not match the overlap-save layout; rerun coursework/E04-overlap-save/design.py"
#endif

static float Dec16kDelay[AUDIO_PROCESS_MAX_CHANNELS][OLS_RATE_TAPS];
static Decimator Dec16k[AUDIO_PROCESS_MAX_CHANNELS];
static float Int16kDelay[AUDIO_PROCESS_MAX_CHANNELS][OLS_RATE_TAPS / OLS_FACTOR];
static Interpolator Int16k[AUDIO_PROCESS_MAX_CHANNELS];

static float OlsTwiddle[FFT_TWIDDLE_FLOATS(OLS_FFT_LEN)];
static RealFft OlsFft;
static float ShortSpectra[OLS_SPECTRA_FLOATS(OLS_FFT_LEN, SHORT_PARTS)];
static float LongSpectra[OLS_SPECTRA_FLOATS(OLS_FFT_LEN, LONG_PARTS)];

static float ShortFdl[SHORT_CHANNELS][OLS_SPECTRA_FLOATS(OLS_FFT_LEN, SHORT_PARTS)];
static float LongFdl[LONG_CHANNELS][OLS_SPECTRA_FLOATS(OLS_FFT_LEN, LONG_PARTS)];
static float OlsFrames[AUDIO_PROCESS_MAX_CHANNELS][OLS_FFT_LEN];
static float OlsHeld[AUDIO_PROCESS_MAX_CHANNELS][OLS_HOP];
static OlsFilter Ols[AUDIO_PROCESS_MAX_CHANNELS];
/* The output spectrum is only needed within one frame, so channels share it */
static float OlsWork[FFT_SPECTRUM_FLOATS(OLS_FFT_LEN)];

/* The low-rate stream of the current chunk */
static float Low16k[LOW16K_FRAMES];

void ChainE04Init(void)
{
	uint32_t nIndex;

	(void)RealFftInit(&OlsFft, OlsTwiddle, OLS_FFT_LEN);
	(void)OlsSpectra(&OlsFft, Fir255Taps, OLS_FIR255_TAPS, ShortSpectra);
	(void)OlsSpectra(&OlsFft, Fir1023Taps, OLS_FIR1023_TAPS, LongSpectra);

	for (nIndex = 0u; nIndex < AUDIO_PROCESS_MAX_CHANNELS; nIndex++)
	{
		DecimatorInit(&Dec16k[nIndex], Rate16kTaps, Dec16kDelay[nIndex], OLS_RATE_TAPS, OLS_FACTOR);
		InterpolatorInit(&Int16k[nIndex], Rate16kTaps, Int16kDelay[nIndex], OLS_RATE_TAPS, OLS_FACTOR);
		if ((nIndex % 2u) == 0u)
		{
			OlsInit(&Ols[nIndex], &OlsFft, ShortSpectra, SHORT_PARTS, ShortFdl[nIndex / 2u],
			        OlsFrames[nIndex], OlsWork, OlsHeld[nIndex]);
		}
		else
		{
			OlsInit(&Ols[nIndex], &OlsFft, LongSpectra, LONG_PARTS, LongFdl[nIndex / 2u],
			        OlsFrames[nIndex], OlsWork, OlsHeld[nIndex]);
		}
		OlsStagger(&Ols[nIndex], (nIndex * OLS_HOP) / AUDIO_PROCESS_MAX_CHANNELS);
	}
}

void ChainE04Channel(uint32_t nChannel, const float *pIn, float *pOut, uint32_t nFrames)
{
	uint32_t nLow;

	nLow = DecimatorProcess(&Dec16k[nChannel], pIn, nFrames, Low16k);
	OlsProcess(&Ols[nChannel], Low16k, Low16k, nLow);
	/* the phases line up, so the interpolator takes exactly the nLow samples */
	(void)InterpolatorProcess(&Int16k[nChannel], Low16k, pOut, nFrames);
}
