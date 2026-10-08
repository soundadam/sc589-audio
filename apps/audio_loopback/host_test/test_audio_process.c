/*!
 * @file      test_audio_process.c
 * @brief     Host-side tests for audio_process.c.
 *
 *            audio_process.c is included directly rather than linked, so the
 *            tests can see its static coefficients, work buffers and
 *            conversion helpers. Builds with any C99 compiler; see Makefile.
 *
 *            Usage
 *             test_audio_process            run the tests, exit 1 on failure
 *             test_audio_process sweep      print the frequency response as CSV
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../audio_process.c"

#ifndef M_PI
#define M_PI            (3.14159265358979323846)
#endif

#define FS              (48000.0)
#define TEST_CHANNELS   (4u)
#define BLOCK_FRAMES    (64u)
/* Blocks run through the filter per tone: one second of audio */
#define TONE_BLOCKS     (750u)
#define TONE_FRAMES     (TONE_BLOCKS * BLOCK_FRAMES)
/* Tone amplitude, as a fraction of full scale */
#define TONE_LEVEL      (0.5)
/* Allowed difference between measured and computed gain */
#define GAIN_TOL_DB     (0.05)

static int32_t In[TONE_FRAMES * TEST_CHANNELS];
static int32_t Out[TONE_FRAMES * TEST_CHANNELS];
static int32_t Ref[TONE_FRAMES * TEST_CHANNELS];

static uint32_t nFailures = 0u;

#define CHECK(cond, ...)                                   \
	do                                                     \
	{                                                      \
		if (!(cond))                                       \
		{                                                  \
			printf("  FAIL %s:%d: ", __FILE__, __LINE__);  \
			printf(__VA_ARGS__);                           \
			printf("\n");                                  \
			nFailures++;                                   \
		}                                                  \
	} while (0)

/*
 * Clears the filter history so each test starts from silence.
 */
static void ResetFilter(void)
{
	memset(FirWork, 0, sizeof(FirWork));
	memset(FirOut, 0, sizeof(FirOut));
}

/*
 * Gain of the FIR at fHz, computed from the coefficients.
 */
static double ComputedGainDb(double fHz)
{
	double fRe = 0.0;
	double fIm = 0.0;
	double fW = 2.0 * M_PI * fHz / FS;
	uint32_t nTap;

	for (nTap = 0u; nTap < FIR_TAPS; nTap++)
	{
		fRe += FirCoeffs[nTap] * cos(fW * nTap);
		fIm -= FirCoeffs[nTap] * sin(fW * nTap);
	}

	return 20.0 * log10(sqrt((fRe * fRe) + (fIm * fIm)));
}

/*
 * Runs a tone through channel 0 in 64-frame blocks, as audio_path.c does,
 * and returns the gain measured from the output RMS once the filter has
 * settled.
 */
static double MeasuredGainDb(double fHz)
{
	uint32_t nFrame;
	uint32_t nBlock;
	double fSumIn = 0.0;
	double fSumOut = 0.0;

	ResetFilter();
	memset(In, 0, sizeof(In));

	for (nFrame = 0u; nFrame < TONE_FRAMES; nFrame++)
	{
		In[nFrame * TEST_CHANNELS] =
			FloatToS24((float)(TONE_LEVEL * sin(2.0 * M_PI * fHz * nFrame / FS)));
	}

	for (nBlock = 0u; nBlock < TONE_BLOCKS; nBlock++)
	{
		AudioProcess(&In[nBlock * BLOCK_FRAMES * TEST_CHANNELS],
		             &Out[nBlock * BLOCK_FRAMES * TEST_CHANNELS],
		             BLOCK_FRAMES, TEST_CHANNELS);
	}

	/* skip the first block, where the history is still filling */
	for (nFrame = BLOCK_FRAMES; nFrame < TONE_FRAMES; nFrame++)
	{
		double fIn = In[nFrame * TEST_CHANNELS];
		double fOut = Out[nFrame * TEST_CHANNELS];
		fSumIn += fIn * fIn;
		fSumOut += fOut * fOut;
	}

	return 10.0 * log10(fSumOut / fSumIn);
}

static void TestConversions(void)
{
	printf("conversions\n");

	CHECK(S24ToFloat(0) == 0.0f, "0 -> %f", S24ToFloat(0));
	CHECK(S24ToFloat(-8388608) == -1.0f, "-2^23 -> %f", S24ToFloat(-8388608));
	CHECK(S24ToFloat(4194304) == 0.5f, "2^22 -> %f", S24ToFloat(4194304));

	/* saturation */
	CHECK(FloatToS24(1.0f) == 8388607, "1.0 -> %d", FloatToS24(1.0f));
	CHECK(FloatToS24(1.5f) == 8388607, "1.5 -> %d", FloatToS24(1.5f));
	CHECK(FloatToS24(-1.0f) == -8388608, "-1.0 -> %d", FloatToS24(-1.0f));
	CHECK(FloatToS24(-1.5f) == -8388608, "-1.5 -> %d", FloatToS24(-1.5f));

	/* rounding half away from zero */
	CHECK(FloatToS24(1.5f * S24_INV_SCALE) == 2, "1.5 LSB -> %d", FloatToS24(1.5f * S24_INV_SCALE));
	CHECK(FloatToS24(-1.5f * S24_INV_SCALE) == -2, "-1.5 LSB -> %d", FloatToS24(-1.5f * S24_INV_SCALE));
	CHECK(FloatToS24(0.4f * S24_INV_SCALE) == 0, "0.4 LSB -> %d", FloatToS24(0.4f * S24_INV_SCALE));

	/* every 24-bit code survives the round trip */
	{
		int32_t nCode;
		uint32_t nBad = 0u;
		for (nCode = -8388608; nCode <= 8388607; nCode++)
		{
			if (FloatToS24(S24ToFloat(nCode)) != nCode)
			{
				nBad++;
			}
		}
		CHECK(nBad == 0u, "%u codes changed on the round trip", nBad);
	}
}

static void TestImpulse(void)
{
	const int32_t nAmp = 4194304;   /* 0.5 of full scale */
	uint32_t nFrame;

	printf("impulse response\n");

	ResetFilter();
	memset(In, 0, sizeof(In));
	In[0] = nAmp;
	AudioProcess(In, Out, BLOCK_FRAMES, TEST_CHANNELS);

	for (nFrame = 0u; nFrame < BLOCK_FRAMES; nFrame++)
	{
		int32_t nExpect = (nFrame < FIR_TAPS) ? FloatToS24(FirCoeffs[nFrame] * 0.5f) : 0;
		CHECK(Out[nFrame * TEST_CHANNELS] == nExpect,
		      "frame %u: got %d, expected %d", nFrame, Out[nFrame * TEST_CHANNELS], nExpect);
	}
}

static void TestBlockBoundary(void)
{
	const uint32_t nFrames = 4u * BLOCK_FRAMES;
	uint32_t nIndex;
	uint32_t nBlock;

	printf("block boundaries\n");

	srand(1u);
	for (nIndex = 0u; nIndex < nFrames * TEST_CHANNELS; nIndex++)
	{
		In[nIndex] = (rand() % 16777216) - 8388608;
	}

	/* one call, split internally into AUDIO_PROCESS_MAX_FRAMES chunks */
	ResetFilter();
	AudioProcess(In, Ref, nFrames, TEST_CHANNELS);

	/* many small calls of an awkward size */
	ResetFilter();
	for (nBlock = 0u; nBlock < nFrames; nBlock += 7u)
	{
		uint32_t nChunk = ((nFrames - nBlock) < 7u) ? (nFrames - nBlock) : 7u;
		AudioProcess(&In[nBlock * TEST_CHANNELS], &Out[nBlock * TEST_CHANNELS],
		             nChunk, TEST_CHANNELS);
	}

	CHECK(memcmp(Ref, Out, nFrames * TEST_CHANNELS * sizeof(int32_t)) == 0,
	      "output depends on how the stream is split into blocks");
}

static void TestChannels(void)
{
	const uint32_t nChannels = 6u;
	uint32_t nFrame;
	uint32_t nChannel;

	printf("channels\n");

	ResetFilter();
	memset(In, 0, sizeof(In));
	for (nFrame = 0u; nFrame < BLOCK_FRAMES; nFrame++)
	{
		In[nFrame * nChannels] = FloatToS24((float)(0.5 * sin(2.0 * M_PI * 1000.0 * nFrame / FS)));
		In[(nFrame * nChannels) + 4u] = (int32_t)(nFrame * 1000u);
		In[(nFrame * nChannels) + 5u] = -(int32_t)(nFrame * 1000u);
	}
	AudioProcess(In, Out, BLOCK_FRAMES, nChannels);

	for (nFrame = 0u; nFrame < BLOCK_FRAMES; nFrame++)
	{
		/* silent channels stay silent: no crosstalk from channel 0 */
		for (nChannel = 1u; nChannel < AUDIO_PROCESS_MAX_CHANNELS; nChannel++)
		{
			CHECK(Out[(nFrame * nChannels) + nChannel] == 0,
			      "frame %u channel %u: got %d", nFrame, nChannel, Out[(nFrame * nChannels) + nChannel]);
		}
		/* channels past the maximum pass straight through */
		for (nChannel = AUDIO_PROCESS_MAX_CHANNELS; nChannel < nChannels; nChannel++)
		{
			CHECK(Out[(nFrame * nChannels) + nChannel] == In[(nFrame * nChannels) + nChannel],
			      "frame %u channel %u not passed through", nFrame, nChannel);
		}
	}
}

static void TestFrequencyResponse(void)
{
	/* points quoted in the comment above FirCoeffs */
	static const double Freqs[] = { 0.0, 1000.0, 3000.0, 4000.0, 6000.0, 8000.0, 12000.0 };
	uint32_t nPoint;

	printf("frequency response\n");
	printf("  %8s %12s %12s\n", "Hz", "measured dB", "computed dB");

	for (nPoint = 0u; nPoint < (sizeof(Freqs) / sizeof(Freqs[0])); nPoint++)
	{
		double fMeasured;
		double fComputed = ComputedGainDb(Freqs[nPoint]);

		if (Freqs[nPoint] == 0.0)
		{
			/* a DC "tone" of all zeros has no energy; use a constant */
			uint32_t nFrame;
			ResetFilter();
			for (nFrame = 0u; nFrame < BLOCK_FRAMES * 2u; nFrame++)
			{
				In[nFrame * TEST_CHANNELS] = 4194304;
			}
			AudioProcess(In, Out, BLOCK_FRAMES * 2u, TEST_CHANNELS);
			fMeasured = 20.0 * log10(Out[(BLOCK_FRAMES * 2u - 1u) * TEST_CHANNELS] / 4194304.0);
		}
		else
		{
			fMeasured = MeasuredGainDb(Freqs[nPoint]);
		}

		printf("  %8.0f %12.2f %12.2f\n", Freqs[nPoint], fMeasured, fComputed);

		/* far down the stop band the 24-bit rounding floor dominates */
		if (fComputed > -60.0)
		{
			CHECK(fabs(fMeasured - fComputed) < GAIN_TOL_DB,
			      "%.0f Hz: measured %.3f dB, computed %.3f dB",
			      Freqs[nPoint], fMeasured, fComputed);
		}
	}

	CHECK(fabs(ComputedGainDb(0.0)) < 0.01, "DC gain %.3f dB, expected 0", ComputedGainDb(0.0));
	CHECK(fabs(ComputedGainDb(3000.0) + 1.9) < 0.1, "3 kHz %.2f dB, comment says -1.9", ComputedGainDb(3000.0));
	CHECK(fabs(ComputedGainDb(6000.0) + 29.5) < 0.1, "6 kHz %.2f dB, comment says -29.5", ComputedGainDb(6000.0));
	CHECK(ComputedGainDb(8000.0) < -52.0, "8 kHz %.2f dB, comment says below -52", ComputedGainDb(8000.0));
}

/*
 * Prints measured and computed gain from 100 Hz to 23.9 kHz as CSV.
 */
static void Sweep(void)
{
	double fHz;

	printf("hz,measured_db,computed_db\n");
	for (fHz = 100.0; fHz < FS / 2.0; fHz += 100.0)
	{
		printf("%.0f,%.3f,%.3f\n", fHz, MeasuredGainDb(fHz), ComputedGainDb(fHz));
	}
}

int main(int argc, char *argv[])
{
	if ((argc > 1) && (strcmp(argv[1], "sweep") == 0))
	{
		Sweep();
		return 0;
	}

	TestConversions();
	TestImpulse();
	TestBlockBoundary();
	TestChannels();
	TestFrequencyResponse();

	if (nFailures != 0u)
	{
		printf("%u check(s) failed\n", nFailures);
		return 1;
	}

	printf("all tests passed\n");
	return 0;
}
