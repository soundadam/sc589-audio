/*!
 * @file      filter_report.c
 * @brief     Measures the multirate chain on the board and prints the result.
 */

#include <sys/platform.h>
#include <builtins.h>
#include <services/pwr/adi_pwr.h>
#include <stdint.h>
#include <stdio.h>
#include "adc_dac_playback.h"
#include "dsp/audio_process.h"
#include "dsp/biquad.h"
#include "dsp/multirate.h"
#include "dsp/multirate_coeffs.h"
#include "platform/filter_report.h"

#if MULTIRATE_SAMPLE_RATE != SAMPLE_RATE
#error "multirate_coeffs was designed for another sample rate; rerun coursework/E02-multirate/design.py"
#endif

/*
 * Blocks of the chain's impulse response: the decimator's and the
 * interpolator's lengths plus the IIR's tail, rounded up to whole blocks.
 * At the console rate the report takes about 3 s.
 */
#define REPORT_CHAIN_BLOCKS ((3u * MULTIRATE_TAPS + FRAMES_PER_BUFFER - 1u) / FRAMES_PER_BUFFER)
/* Low-rate samples in one block, at most */
#define REPORT_LOW_FRAMES   MULTIRATE_LOW_FRAMES(FRAMES_PER_BUFFER, MULTIRATE_FACTOR)
/* Timed runs per stage; the fastest one counts, the first warms the cache */
#define REPORT_RUNS         (8u)
/* Impulse height, as a fraction of full scale */
#define REPORT_IMPULSE      (0.5f)
/* Full scale of a 24-bit sample */
#define REPORT_S24_SCALE    (8388608.0f)

static float DecDelay[MULTIRATE_TAPS];
static float IirState[RATE8K_IIR_SECTIONS * BIQUAD_STATES];
static float IntDelay[MULTIRATE_TAPS / MULTIRATE_FACTOR];

static float Block[FRAMES_PER_BUFFER];
static float Low[REPORT_LOW_FRAMES];
static int32_t Frames[FRAMES_PER_BUFFER * NUM_CHANNELS];

/*
 * Prints channel 0's response to an impulse on channel 0, through the whole
 * AudioProcess() as the audio path calls it, scaled back to a unit impulse.
 */
static void PrintChainImpulse(void)
{
	uint32_t nBlock;
	uint32_t n;

	AudioProcessInit();
	for (nBlock = 0u; nBlock < REPORT_CHAIN_BLOCKS; nBlock++)
	{
		for (n = 0u; n < FRAMES_PER_BUFFER * NUM_CHANNELS; n++)
		{
			Frames[n] = 0;
		}
		if (nBlock == 0u)
		{
			Frames[0] = (int32_t)(REPORT_IMPULSE * REPORT_S24_SCALE);
		}

		AudioProcess(Frames, Frames, FRAMES_PER_BUFFER, NUM_CHANNELS);

		for (n = 0u; n < FRAMES_PER_BUFFER; n++)
		{
			printf("ir,chain,%u,%.9g\n", (unsigned)((nBlock * FRAMES_PER_BUFFER) + n),
			       (double)((float)Frames[n * NUM_CHANNELS] / (REPORT_IMPULSE * REPORT_S24_SCALE)));
		}
	}
}

static void PrintCycles(const char *pName, uint32_t nFrames, unsigned long long nCycles)
{
	printf("cycles,%s,%u,%lu\n", pName, (unsigned)nFrames, (unsigned long)nCycles);
}

void FilterReport(void)
{
	Decimator Dec;
	BiquadCascade Iir;
	Interpolator Int;
	uint32_t nCclk = 0u;
	uint32_t nRun;
	uint32_t n;
	unsigned long long nStart;
	unsigned long long nDec = ~0ull;
	unsigned long long nIir = ~0ull;
	unsigned long long nInt = ~0ull;
	unsigned long long nAll = ~0ull;

	if (adi_pwr_GetCoreFreq(0u, &nCclk) == ADI_PWR_SUCCESS)
	{
		printf("clock,cclk,%lu\n", (unsigned long)nCclk);
	}

	PrintChainImpulse();

	/* a ramp of non-zero samples, a few hundredths of full scale */
	for (n = 0u; n < FRAMES_PER_BUFFER; n++)
	{
		Block[n] = (float)n * (1.0f / 2048.0f);
	}
	for (n = 0u; n < REPORT_LOW_FRAMES; n++)
	{
		Low[n] = Block[n];
	}
	for (n = 0u; n < FRAMES_PER_BUFFER * NUM_CHANNELS; n++)
	{
		Frames[n] = (int32_t)(n * 1024u);
	}

	BiquadInit(&Iir, Rate8kIirSos, IirState, RATE8K_IIR_SECTIONS);
	for (nRun = 0u; nRun < REPORT_RUNS; nRun++)
	{
		/*
		 * A block is not a multiple of the factor, so how many low-rate
		 * samples it carries depends on the phase it starts at. Starting
		 * every run at phase 0 times the block with the most.
		 */
		DecimatorInit(&Dec, MultirateTaps, DecDelay, MULTIRATE_TAPS, MULTIRATE_FACTOR);
		InterpolatorInit(&Int, MultirateTaps, IntDelay, MULTIRATE_TAPS, MULTIRATE_FACTOR);
		AudioProcessInit();

		nStart = emuclk();
		(void)DecimatorProcess(&Dec, Block, FRAMES_PER_BUFFER, Low);
		nStart = emuclk() - nStart;
		nDec = (nStart < nDec) ? nStart : nDec;

		nStart = emuclk();
		BiquadProcess(&Iir, Low, Low, REPORT_LOW_FRAMES);
		nStart = emuclk() - nStart;
		nIir = (nStart < nIir) ? nStart : nIir;

		nStart = emuclk();
		(void)InterpolatorProcess(&Int, Low, Block, FRAMES_PER_BUFFER);
		nStart = emuclk() - nStart;
		nInt = (nStart < nInt) ? nStart : nInt;

		nStart = emuclk();
		AudioProcess(Frames, Frames, FRAMES_PER_BUFFER, NUM_CHANNELS);
		nStart = emuclk() - nStart;
		nAll = (nStart < nAll) ? nStart : nAll;
	}
	AudioProcessInit();

	PrintCycles("decimator", FRAMES_PER_BUFFER, nDec);
	PrintCycles("iir", REPORT_LOW_FRAMES, nIir);
	PrintCycles("interpolator", FRAMES_PER_BUFFER, nInt);
	PrintCycles("audio_process", FRAMES_PER_BUFFER, nAll);
}
