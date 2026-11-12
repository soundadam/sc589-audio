/*!
 * @file      filter_report.c
 * @brief     Measures E04's overlap-save filters on the board and prints the result.
 */

#include <sys/platform.h>
#include <builtins.h>
#include <services/pwr/adi_pwr.h>
#include <trans.h>
#include <stdint.h>
#include <stdio.h>
#include "adc_dac_playback.h"
#include "dsp/audio_process.h"
#include "dsp/fft.h"
#include "dsp/fir.h"
#include "dsp/ols.h"
#include "dsp/ols_coeffs.h"
#include "platform/filter_report.h"

#if OLS_SAMPLE_RATE != SAMPLE_RATE
#error "ols_coeffs was designed for another sample rate; rerun coursework/E04-overlap-save/design.py"
#endif
#if (OLS_FFT_LEN != 512u) || (((OLS_HOP * OLS_FACTOR) % FRAMES_PER_BUFFER) != 0u)
#error "the report assumes 512-point frames and whole blocks per hop"
#endif

#define REPORT_PARTS        OLS_FIR1023_PARTS
/* Codec-rate blocks in one hop */
#define REPORT_HOP_BLOCKS   ((OLS_HOP * OLS_FACTOR) / FRAMES_PER_BUFFER)
/* Timed runs per stage; the fastest one counts, the first warms the cache */
#define REPORT_RUNS         (8u)
/* Impulse height, as a fraction of full scale */
#define REPORT_IMPULSE      (0.5f)
/* Full scale of a 24-bit sample */
#define REPORT_S24_SCALE    (8388608.0f)

static float Twiddle[FFT_TWIDDLE_FLOATS(OLS_FFT_LEN)];
static RealFft Fft;
static float ShortSpectra[OLS_SPECTRA_FLOATS(OLS_FFT_LEN, 1u)];
static float LongSpectra[OLS_SPECTRA_FLOATS(OLS_FFT_LEN, REPORT_PARTS)];
static float Fdl[OLS_SPECTRA_FLOATS(OLS_FFT_LEN, REPORT_PARTS)];
static float Frame[OLS_FFT_LEN];
static float Work[FFT_SPECTRUM_FLOATS(OLS_FFT_LEN)];
static float Held[OLS_HOP];
static float FirDelay[OLS_FIR1023_TAPS];

static float Hop[OLS_HOP];
static float HopOut[OLS_HOP];
static float Spectrum[FFT_SPECTRUM_FLOATS(OLS_FFT_LEN)];
static float LibIn[OLS_FFT_LEN];
static float LibRe[OLS_FFT_LEN];
static float LibIm[OLS_FFT_LEN];
static int32_t Frames[FRAMES_PER_BUFFER * NUM_CHANNELS];

static void Keep(unsigned long long nCycles, unsigned long long *pBest)
{
	*pBest = (nCycles < *pBest) ? nCycles : *pBest;
}

static void PrintCycles(const char *pName, uint32_t nFrames, unsigned long long nCycles)
{
	printf("cycles,%s,%u,%lu\n", pName, (unsigned)nFrames, (unsigned long)nCycles);
}

/*
 * Prints the impulse response of an overlap-save filter: OLS_HOP samples of
 * delay and nTaps of filter, a hop at a time.
 */
static void PrintImpulse(const char *pName, const float *pSpectra, uint32_t nParts, uint32_t nTaps)
{
	OlsFilter Ols;
	uint32_t nHops = (OLS_HOP + nTaps + OLS_HOP - 1u) / OLS_HOP;
	uint32_t nHop;
	uint32_t n;

	OlsInit(&Ols, &Fft, pSpectra, nParts, Fdl, Frame, Work, Held);
	for (nHop = 0u; nHop < nHops; nHop++)
	{
		for (n = 0u; n < OLS_HOP; n++)
		{
			Hop[n] = ((nHop == 0u) && (n == 0u)) ? 1.0f : 0.0f;
		}
		OlsProcess(&Ols, Hop, HopOut, OLS_HOP);
		for (n = 0u; n < OLS_HOP; n++)
		{
			printf("ir,%s,%u,%.9g\n", pName, (unsigned)((nHop * OLS_HOP) + n), (double)HopOut[n]);
		}
	}
}

/*
 * Times the four ways of filtering one hop. Every run starts from a cleared
 * filter, so overlap-save transforms exactly one frame per run.
 */
static void TimeFilters(void)
{
	FirFilter Fir;
	OlsFilter Ols;
	uint32_t nRun;
	unsigned long long nStart;
	unsigned long long nFir255 = ~0ull;
	unsigned long long nFir1023 = ~0ull;
	unsigned long long nOls = ~0ull;
	unsigned long long nPart = ~0ull;

	for (nRun = 0u; nRun < REPORT_RUNS; nRun++)
	{
		FirInit(&Fir, Fir255Taps, FirDelay, OLS_FIR255_TAPS);
		nStart = emuclk();
		FirProcess(&Fir, Hop, HopOut, OLS_HOP);
		Keep(emuclk() - nStart, &nFir255);

		FirInit(&Fir, Fir1023Taps, FirDelay, OLS_FIR1023_TAPS);
		nStart = emuclk();
		FirProcess(&Fir, Hop, HopOut, OLS_HOP);
		Keep(emuclk() - nStart, &nFir1023);

		OlsInit(&Ols, &Fft, ShortSpectra, 1u, Fdl, Frame, Work, Held);
		nStart = emuclk();
		OlsProcess(&Ols, Hop, HopOut, OLS_HOP);
		Keep(emuclk() - nStart, &nOls);

		OlsInit(&Ols, &Fft, LongSpectra, REPORT_PARTS, Fdl, Frame, Work, Held);
		nStart = emuclk();
		OlsProcess(&Ols, Hop, HopOut, OLS_HOP);
		Keep(emuclk() - nStart, &nPart);
	}

	PrintCycles("fir255", OLS_HOP, nFir255);
	PrintCycles("ols", OLS_HOP, nOls);
	PrintCycles("fir1023", OLS_HOP, nFir1023);
	PrintCycles("partitioned", OLS_HOP, nPart);
}

/*
 * Times one 512-point frame through this FFT, its inverse, and the CCES
 * library's rfft512() for comparison.
 */
static void TimeFfts(void)
{
	uint32_t nRun;
	uint32_t n;
	unsigned long long nStart;
	unsigned long long nFwd = ~0ull;
	unsigned long long nInv = ~0ull;
	unsigned long long nLib = ~0ull;

	for (nRun = 0u; nRun < REPORT_RUNS; nRun++)
	{
		for (n = 0u; n < OLS_FFT_LEN; n++)
		{
			Spectrum[n] = Hop[n % OLS_HOP];
			LibIn[n] = Spectrum[n];
		}
		nStart = emuclk();
		RealFftForward(&Fft, Spectrum);
		Keep(emuclk() - nStart, &nFwd);

		nStart = emuclk();
		RealFftInverse(&Fft, Spectrum);
		Keep(emuclk() - nStart, &nInv);

		nStart = emuclk();
		(void)rfft512(LibIn, LibRe, LibIm);
		Keep(emuclk() - nStart, &nLib);
	}

	PrintCycles("fft512", OLS_FFT_LEN, nFwd);
	PrintCycles("ifft512", OLS_FFT_LEN, nInv);
	PrintCycles("rfft512", OLS_FFT_LEN, nLib);
}

/*
 * Times AudioProcess() block by block over one hop, from a cleared chain:
 * each channel's frame falls in its own block of the hop.
 */
static void TimeAudioProcess(void)
{
	unsigned long long Best[REPORT_HOP_BLOCKS];
	unsigned long long nSum = 0ull;
	unsigned long long nPeak = 0ull;
	unsigned long long nStart;
	uint32_t nRun;
	uint32_t nBlock;
	uint32_t n;

	for (nBlock = 0u; nBlock < REPORT_HOP_BLOCKS; nBlock++)
	{
		Best[nBlock] = ~0ull;
	}
	for (n = 0u; n < FRAMES_PER_BUFFER * NUM_CHANNELS; n++)
	{
		Frames[n] = (int32_t)(n * 1024u);
	}

	for (nRun = 0u; nRun < REPORT_RUNS; nRun++)
	{
		AudioProcessInit();
		for (nBlock = 0u; nBlock < REPORT_HOP_BLOCKS; nBlock++)
		{
			nStart = emuclk();
			AudioProcess(Frames, Frames, FRAMES_PER_BUFFER, NUM_CHANNELS);
			Keep(emuclk() - nStart, &Best[nBlock]);
		}
	}
	AudioProcessInit();

	for (nBlock = 0u; nBlock < REPORT_HOP_BLOCKS; nBlock++)
	{
		nSum += Best[nBlock];
		nPeak = (Best[nBlock] > nPeak) ? Best[nBlock] : nPeak;
	}
	PrintCycles("audio_process", REPORT_HOP_BLOCKS * FRAMES_PER_BUFFER, nSum);
	PrintCycles("audio_process_peak", FRAMES_PER_BUFFER, nPeak);
}

void FilterReport(void)
{
	uint32_t nCclk = 0u;
	uint32_t n;

	if (adi_pwr_GetCoreFreq(0u, &nCclk) == ADI_PWR_SUCCESS)
	{
		printf("clock,cclk,%lu\n", (unsigned long)nCclk);
	}

	(void)RealFftInit(&Fft, Twiddle, OLS_FFT_LEN);
	(void)OlsSpectra(&Fft, Fir255Taps, OLS_FIR255_TAPS, ShortSpectra);
	(void)OlsSpectra(&Fft, Fir1023Taps, OLS_FIR1023_TAPS, LongSpectra);

	PrintImpulse("ols", ShortSpectra, 1u, OLS_FIR255_TAPS);
	PrintImpulse("partitioned", LongSpectra, REPORT_PARTS, OLS_FIR1023_TAPS);

	/* a ramp of non-zero samples, a few hundredths of full scale */
	for (n = 0u; n < OLS_HOP; n++)
	{
		Hop[n] = (float)n * (1.0f / 8192.0f);
	}
	TimeFilters();
	TimeFfts();
	TimeAudioProcess();
}
