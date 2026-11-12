/*!
 * @file      filter_report.c
 * @brief     Checks the E03 FFT against the CCES library on the board and
 *            prints the result.
 *
 *            The comparison with rfft32/ifft32/cfft32 (trans.h) lives here
 *            and not in dsp/, as only CCES has those functions; dsp/ stays
 *            plain C99 and builds on the host.
 */

#include <sys/platform.h>
#include <builtins.h>
#include <services/pwr/adi_pwr.h>
#include <trans.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "adc_dac_playback.h"
#include "dsp/audio_process.h"
#include "dsp/fft.h"
#include "platform/filter_report.h"

#if FFT_SAMPLE_RATE != SAMPLE_RATE
#error "fft_tables was designed for another sample rate; rerun coursework/E03-fft/design.py"
#endif

#if FFT_N != 32u
#error "the library calls below are the 32-point ones; change them with FFT_N"
#endif

/* Timed runs per function; the fastest one counts, the first warms the cache */
#define REPORT_RUNS         (8u)
/* Impulse height, as a fraction of full scale */
#define REPORT_IMPULSE      (0.5f)
/* Full scale of a 24-bit sample */
#define REPORT_S24_SCALE    (8388608.0f)
/* Blocks of the impulse responses printed */
#define REPORT_IR_BLOCKS    (2u)
/* Blocks of noise through AudioProcess() for the loop-back check */
#define REPORT_NOISE_BLOCKS (16u)

/* The test frame: real samples, and a second set for the imaginary parts */
static float Re[FFT_N];
static float Im[FFT_N];

/* Own transforms */
static Cplx OwnIn[FFT_N];
static Cplx OwnOut[FFT_N];
static Cplx OwnBack[FFT_N];
static Cplx OwnReal[FFT_REAL_BINS];
static float OwnRealBack[FFT_N];

/* Library transforms: separate real and imaginary arrays */
static float LibInRe[FFT_N];
static float LibInIm[FFT_N];
static float LibOutRe[FFT_N];
static float LibOutIm[FFT_N];

static int32_t Frames[FRAMES_PER_BUFFER * NUM_CHANNELS];
static int32_t Sent[REPORT_NOISE_BLOCKS * FRAMES_PER_BUFFER * NUM_CHANNELS];

static uint32_t nSeed = 1u;

/* Uniform in [-1, 1), the same sequence every boot */
static float Uniform(void)
{
	nSeed = (nSeed * 1664525u) + 1013904223u;
	return ((float)(nSeed >> 8) * (1.0f / 8388608.0f)) - 1.0f;
}

static void PrintCplx(const char *pName, const Cplx *pX, uint32_t n)
{
	uint32_t k;

	for (k = 0u; k < n; k++)
	{
		printf("fft,%s,%u,%.9g,%.9g\n", pName, (unsigned)k, (double)pX[k].re, (double)pX[k].im);
	}
}

static void PrintSplit(const char *pName, const float *pRe, const float *pIm, uint32_t n)
{
	uint32_t k;

	for (k = 0u; k < n; k++)
	{
		printf("fft,%s,%u,%.9g,%.9g\n", pName, (unsigned)k, (double)pRe[k], (double)((pIm != NULL) ? pIm[k] : 0.0f));
	}
}

static void PrintCycles(const char *pName, uint32_t nFrames, unsigned long long nCycles)
{
	printf("cycles,%s,%u,%lu\n", pName, (unsigned)nFrames, (unsigned long)nCycles);
}

/* Library inputs from the test frame; called before every library call */
static void LoadLibInput(void)
{
	uint32_t n;

	for (n = 0u; n < FFT_N; n++)
	{
		LibInRe[n] = Re[n];
		LibInIm[n] = Im[n];
	}
}

/* The library's inverse input: the own complex FFT's spectrum */
static void LoadLibSpectrum(void)
{
	uint32_t n;

	for (n = 0u; n < FFT_N; n++)
	{
		LibInRe[n] = OwnOut[n].re;
		LibInIm[n] = OwnOut[n].im;
	}
}

/*
 * Every transform on the same frame, own and library, printed as vectors for
 * the host to compare: the scaling and bin layout of the library's results
 * are read off there rather than assumed here.
 */
static void PrintTransforms(void)
{
	uint32_t n;

	for (n = 0u; n < FFT_N; n++)
	{
		Re[n] = Uniform();
		Im[n] = Uniform();
		OwnIn[n] = CplxMake(Re[n], Im[n]);
	}
	PrintSplit("re_in", Re, NULL, FFT_N);
	PrintSplit("im_in", Im, NULL, FFT_N);

	/* real input */
	FftRealForward(Re, OwnReal);
	PrintCplx("rfft_own", OwnReal, FFT_REAL_BINS);
	FftRealInverse(OwnReal, OwnRealBack);
	PrintSplit("rifft_own", OwnRealBack, NULL, FFT_N);
	LoadLibInput();
	(void)rfft32(LibInRe, LibOutRe, LibOutIm);
	PrintSplit("rfft_lib", LibOutRe, LibOutIm, FFT_N);

	/* complex input */
	FftForward(OwnIn, OwnOut);
	PrintCplx("cfft_own", OwnOut, FFT_N);
	LoadLibInput();
	(void)cfft32(LibInRe, LibInIm, LibOutRe, LibOutIm);
	PrintSplit("cfft_lib", LibOutRe, LibOutIm, FFT_N);

	/* inverse of the own complex spectrum */
	FftInverse(OwnOut, OwnBack);
	PrintCplx("ifft_own", OwnBack, FFT_N);
	LoadLibSpectrum();
	(void)ifft32(LibInRe, LibInIm, LibOutRe, LibOutIm);
	PrintSplit("ifft_lib", LibOutRe, LibOutIm, FFT_N);
}

/*
 * Channels 0 and 1's responses to an impulse on each at the start of a frame,
 * through AudioProcess() as the audio path calls it, scaled back to a unit
 * impulse: a delayed copy, and one frame's low-pass.
 */
static void PrintImpulses(void)
{
	uint32_t nBlock;
	uint32_t n;

	AudioProcessInit();
	for (nBlock = 0u; nBlock < REPORT_IR_BLOCKS; nBlock++)
	{
		for (n = 0u; n < FRAMES_PER_BUFFER * NUM_CHANNELS; n++)
		{
			Frames[n] = 0;
		}
		if (nBlock == 0u)
		{
			Frames[0] = (int32_t)(REPORT_IMPULSE * REPORT_S24_SCALE);
			Frames[1] = (int32_t)(REPORT_IMPULSE * REPORT_S24_SCALE);
		}

		AudioProcess(Frames, Frames, FRAMES_PER_BUFFER, NUM_CHANNELS);

		for (n = 0u; n < FRAMES_PER_BUFFER; n++)
		{
			unsigned nIndex = (unsigned)((nBlock * FRAMES_PER_BUFFER) + n);
			printf("ir,identity,%u,%.9g\n", nIndex,
			       (double)((float)Frames[n * NUM_CHANNELS] / (REPORT_IMPULSE * REPORT_S24_SCALE)));
			printf("ir,lowpass,%u,%.9g\n", nIndex,
			       (double)((float)Frames[(n * NUM_CHANNELS) + 1u] / (REPORT_IMPULSE * REPORT_S24_SCALE)));
		}
	}
}

/*
 * Noise through AudioProcess(): the largest difference, in 24-bit LSB, between
 * an even channel's output and its input FFT_N samples earlier.
 */
static void PrintLoopback(void)
{
	const uint32_t nPerBlock = FRAMES_PER_BUFFER * NUM_CHANNELS;
	uint32_t nBlock;
	uint32_t nFrame;
	uint32_t nChannel;
	int32_t nMax = 0;

	for (nFrame = 0u; nFrame < REPORT_NOISE_BLOCKS * nPerBlock; nFrame++)
	{
		Sent[nFrame] = (int32_t)(0.9f * Uniform() * REPORT_S24_SCALE);
	}

	AudioProcessInit();
	for (nBlock = 0u; nBlock < REPORT_NOISE_BLOCKS; nBlock++)
	{
		AudioProcess(&Sent[nBlock * nPerBlock], Frames, FRAMES_PER_BUFFER, NUM_CHANNELS);
		for (nFrame = 0u; nFrame < FRAMES_PER_BUFFER; nFrame++)
		{
			uint32_t nAt = (nBlock * FRAMES_PER_BUFFER) + nFrame;

			for (nChannel = 0u; (nChannel < NUM_CHANNELS) && (nAt >= FFT_N); nChannel += 2u)
			{
				int32_t nDiff = Frames[(nFrame * NUM_CHANNELS) + nChannel]
				              - Sent[((nAt - FFT_N) * NUM_CHANNELS) + nChannel];
				nDiff = (nDiff < 0) ? -nDiff : nDiff;
				nMax = (nDiff > nMax) ? nDiff : nMax;
			}
		}
	}
	printf("diff,loopback_lsb,%ld\n", (long)nMax);
}

#define TIME(nBest, call)                                  \
	do                                                     \
	{                                                      \
		unsigned long long nStart = emuclk();              \
		call;                                              \
		nStart = emuclk() - nStart;                        \
		(nBest) = (nStart < (nBest)) ? nStart : (nBest);   \
	} while (0)

void FilterReport(void)
{
	uint32_t nCclk = 0u;
	uint32_t nRun;
	uint32_t k;
	unsigned long long nCfftOwn = ~0ull;
	unsigned long long nIfftOwn = ~0ull;
	unsigned long long nRfftOwn = ~0ull;
	unsigned long long nRifftOwn = ~0ull;
	unsigned long long nCfftLib = ~0ull;
	unsigned long long nIfftLib = ~0ull;
	unsigned long long nRfftLib = ~0ull;
	unsigned long long nFrame = ~0ull;
	unsigned long long nAll = ~0ull;

	if (adi_pwr_GetCoreFreq(0u, &nCclk) == ADI_PWR_SUCCESS)
	{
		printf("clock,cclk,%lu\n", (unsigned long)nCclk);
	}

	PrintTransforms();
	PrintImpulses();
	PrintLoopback();

	for (nRun = 0u; nRun < REPORT_RUNS; nRun++)
	{
		TIME(nCfftOwn, FftForward(OwnIn, OwnOut));
		TIME(nIfftOwn, FftInverse(OwnOut, OwnBack));
		TIME(nRfftOwn, FftRealForward(Re, OwnReal));
		TIME(nRifftOwn, FftRealInverse(OwnReal, OwnRealBack));

		/* inputs reloaded outside the timing, in case a call works in them */
		LoadLibInput();
		TIME(nCfftLib, (void)cfft32(LibInRe, LibInIm, LibOutRe, LibOutIm));
		LoadLibInput();
		TIME(nRfftLib, (void)rfft32(LibInRe, LibOutRe, LibOutIm));
		LoadLibSpectrum();
		TIME(nIfftLib, (void)ifft32(LibInRe, LibInIm, LibOutRe, LibOutIm));

		/* one frame of an odd channel, as AudioProcess() runs it */
		TIME(nFrame,
		     FftRealForward(Re, OwnReal);
		     for (k = 0u; k < FFT_REAL_BINS; k++)
		     {
		         OwnReal[k] = CplxScale(OwnReal[k], FftBinGain[k]);
		     }
		     FftRealInverse(OwnReal, OwnRealBack));

		TIME(nAll, AudioProcess(Frames, Frames, FRAMES_PER_BUFFER, NUM_CHANNELS));
	}
	AudioProcessInit();

	PrintCycles("cfft_own", FFT_N, nCfftOwn);
	PrintCycles("ifft_own", FFT_N, nIfftOwn);
	PrintCycles("rfft_own", FFT_N, nRfftOwn);
	PrintCycles("rifft_own", FFT_N, nRifftOwn);
	PrintCycles("cfft_lib", FFT_N, nCfftLib);
	PrintCycles("ifft_lib", FFT_N, nIfftLib);
	PrintCycles("rfft_lib", FFT_N, nRfftLib);
	PrintCycles("frame", FFT_N, nFrame);
	PrintCycles("audio_process", FRAMES_PER_BUFFER, nAll);
}
