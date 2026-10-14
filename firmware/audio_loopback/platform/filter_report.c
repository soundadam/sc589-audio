/*!
 * @file      filter_report.c
 * @brief     Measures the low-pass filters on the board and prints the result.
 */

#include <sys/platform.h>
#include <builtins.h>
#include <services/pwr/adi_pwr.h>
#include <stdint.h>
#include <stdio.h>
#include "adc_dac_playback.h"
#include "dsp/audio_process.h"
#include "dsp/biquad.h"
#include "dsp/fir.h"
#include "dsp/lowpass_coeffs.h"
#include "platform/filter_report.h"

/* Twice the filter length, so the zeros after the last tap show */
#define REPORT_FIR_LEN      (2u * LOWPASS_FIR_TAPS)
/*
 * The IIR's impulse response is below 1e-8 of its peak by sample 384 for the
 * current design, under the 24-bit resolution of the audio path. At the
 * console rate the report takes about 2 s.
 */
#define REPORT_IIR_LEN      (384u)
/* Timed runs per filter; the fastest one counts, the first warms the cache */
#define REPORT_RUNS         (8u)

static float FirDelay[LOWPASS_FIR_TAPS];
static float IirState[LOWPASS_IIR_SECTIONS * BIQUAD_STATES];

static float Block[FRAMES_PER_BUFFER];
static int32_t Frames[FRAMES_PER_BUFFER * NUM_CHANNELS];

static void PrintImpulse(const char *pName, FirFilter *pFir, BiquadCascade *pIir, uint32_t nLen)
{
	uint32_t n;
	float fIn;
	float fOut;

	for (n = 0u; n < nLen; n++)
	{
		fIn = (n == 0u) ? 1.0f : 0.0f;
		if (pFir != NULL)
		{
			FirProcess(pFir, &fIn, &fOut, 1u);
		}
		else
		{
			BiquadProcess(pIir, &fIn, &fOut, 1u);
		}
		printf("ir,%s,%u,%.9g\n", pName, (unsigned)n, (double)fOut);
	}
}

static void PrintCycles(const char *pName, uint32_t nFrames, unsigned long long nCycles)
{
	printf("cycles,%s,%u,%lu\n", pName, (unsigned)nFrames, (unsigned long)nCycles);
}

void FilterReport(void)
{
	FirFilter Fir;
	BiquadCascade Iir;
	uint32_t nCclk = 0u;
	uint32_t nRun;
	uint32_t n;
	unsigned long long nStart;
	unsigned long long nFir = ~0ull;
	unsigned long long nIir = ~0ull;
	unsigned long long nAll = ~0ull;

	if (adi_pwr_GetCoreFreq(0u, &nCclk) == ADI_PWR_SUCCESS)
	{
		printf("clock,cclk,%lu\n", (unsigned long)nCclk);
	}

	FirInit(&Fir, LowpassFirCoeffs, FirDelay, LOWPASS_FIR_TAPS);
	PrintImpulse("fir", &Fir, NULL, REPORT_FIR_LEN);

	BiquadInit(&Iir, LowpassIirSos, IirState, LOWPASS_IIR_SECTIONS);
	PrintImpulse("iir", NULL, &Iir, REPORT_IIR_LEN);

	/* a ramp of non-zero samples, a few hundredths of full scale */
	for (n = 0u; n < FRAMES_PER_BUFFER; n++)
	{
		Block[n] = (float)n * (1.0f / 2048.0f);
	}
	for (n = 0u; n < FRAMES_PER_BUFFER * NUM_CHANNELS; n++)
	{
		Frames[n] = (int32_t)(n * 1024u);
	}

	AudioProcessInit();
	for (nRun = 0u; nRun < REPORT_RUNS; nRun++)
	{
		nStart = emuclk();
		FirProcess(&Fir, Block, Block, FRAMES_PER_BUFFER);
		nStart = emuclk() - nStart;
		nFir = (nStart < nFir) ? nStart : nFir;

		nStart = emuclk();
		BiquadProcess(&Iir, Block, Block, FRAMES_PER_BUFFER);
		nStart = emuclk() - nStart;
		nIir = (nStart < nIir) ? nStart : nIir;

		nStart = emuclk();
		AudioProcess(Frames, Frames, FRAMES_PER_BUFFER, NUM_CHANNELS);
		nStart = emuclk() - nStart;
		nAll = (nStart < nAll) ? nStart : nAll;
	}
	AudioProcessInit();

	PrintCycles("fir", FRAMES_PER_BUFFER, nFir);
	PrintCycles("iir", FRAMES_PER_BUFFER, nIir);
	PrintCycles("audio_process", FRAMES_PER_BUFFER, nAll);
}
