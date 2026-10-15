/*!
 * @file      test_audio_process.c
 * @brief     Host-side tests for the dsp/ sources.
 *
 *            The sources are included directly rather than linked, so the
 *            tests can see the static conversion helpers in audio_process.c.
 *            Expected values come from the same coefficient tables the
 *            firmware uses, recomputed here in double precision. Builds with
 *            any C99 compiler; see Makefile.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../audio_process.c"
#include "../fir.c"
#include "../biquad.c"
#include "../lowpass_coeffs.c"

#ifndef M_PI
#define M_PI            (3.14159265358979323846)
#endif

#define FS              (48000.0)
#define TEST_CHANNELS   (4u)
#define BLOCK_FRAMES    (64u)
/* Blocks run through the filters per tone: one second of audio */
#define TONE_BLOCKS     (750u)
#define TONE_FRAMES     (TONE_BLOCKS * BLOCK_FRAMES)
/* Frames left out of the gain measurement while the IIR settles */
#define TONE_SETTLE     (TONE_FRAMES / 4u)
/* Tone amplitude, as a fraction of full scale */
#define TONE_LEVEL      (0.5)
/* Allowed difference between measured and computed gain */
#define GAIN_TOL_DB     (0.05)
/* Allowed difference between a float filter and its double reference */
#define FILTER_TOL      (1e-5)
/* Samples per filter in the float-versus-double comparisons */
#define REF_FRAMES      (4096u)
/* An awkward block size, to split streams off the 64-frame grid */
#define ODD_BLOCK       (7u)

static int32_t In[TONE_FRAMES * TEST_CHANNELS];
static int32_t Out[TONE_FRAMES * TEST_CHANNELS];
static int32_t Ref[TONE_FRAMES * TEST_CHANNELS];

static float SigIn[REF_FRAMES];
static float SigOut[REF_FRAMES];
static float SigRef[REF_FRAMES];

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
 * Fills pSig with uniform noise in [-fLevel, fLevel), the same every run.
 */
static void Noise(float *pSig, uint32_t nFrames, float fLevel)
{
	uint32_t nFrame;

	srand(1u);
	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		pSig[nFrame] = fLevel * (2.0f * ((float)rand() / ((float)RAND_MAX + 1.0f)) - 1.0f);
	}
}

static double MaxAbsDiff(const float *pA, const float *pB, uint32_t nFrames)
{
	double fMax = 0.0;
	uint32_t nFrame;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		double fDiff = fabs((double)pA[nFrame] - (double)pB[nFrame]);
		fMax = (fDiff > fMax) ? fDiff : fMax;
	}
	return fMax;
}

/*
 * Gain of the low-pass FIR (bIir = 0) or IIR (bIir = 1) at fHz, computed
 * from the coefficient tables.
 */
static double ComputedGainDb(int bIir, double fHz)
{
	double fW = 2.0 * M_PI * fHz / FS;
	double fGain = 1.0;
	uint32_t nTap;
	uint32_t nSection;

	if (!bIir)
	{
		double fRe = 0.0;
		double fIm = 0.0;
		for (nTap = 0u; nTap < LOWPASS_FIR_TAPS; nTap++)
		{
			fRe += LowpassFirCoeffs[nTap] * cos(fW * nTap);
			fIm -= LowpassFirCoeffs[nTap] * sin(fW * nTap);
		}
		return 20.0 * log10(sqrt((fRe * fRe) + (fIm * fIm)));
	}

	for (nSection = 0u; nSection < LOWPASS_IIR_SECTIONS; nSection++)
	{
		const float *pC = &LowpassIirSos[nSection * BIQUAD_COEFFS];
		/* numerator and denominator at z = e^{jw} */
		double fNumRe = pC[0] + (pC[1] * cos(fW)) + (pC[2] * cos(2.0 * fW));
		double fNumIm = -(pC[1] * sin(fW)) - (pC[2] * sin(2.0 * fW));
		double fDenRe = 1.0 + (pC[3] * cos(fW)) + (pC[4] * cos(2.0 * fW));
		double fDenIm = -(pC[3] * sin(fW)) - (pC[4] * sin(2.0 * fW));
		fGain *= sqrt(((fNumRe * fNumRe) + (fNumIm * fNumIm)) /
		              ((fDenRe * fDenRe) + (fDenIm * fDenIm)));
	}
	return 20.0 * log10(fGain);
}

/*
 * Runs a tone through channel nChannel in 64-frame blocks, as audio_path.c
 * does, and returns the gain measured from the output RMS once the filter
 * has settled.
 */
static double MeasuredGainDb(uint32_t nChannel, double fHz)
{
	uint32_t nFrame;
	uint32_t nBlock;
	double fSumIn = 0.0;
	double fSumOut = 0.0;

	AudioProcessInit();
	memset(In, 0, sizeof(In));

	for (nFrame = 0u; nFrame < TONE_FRAMES; nFrame++)
	{
		In[(nFrame * TEST_CHANNELS) + nChannel] =
			FloatToS24((float)(TONE_LEVEL * sin(2.0 * M_PI * fHz * nFrame / FS)));
	}

	for (nBlock = 0u; nBlock < TONE_BLOCKS; nBlock++)
	{
		AudioProcess(&In[nBlock * BLOCK_FRAMES * TEST_CHANNELS],
		             &Out[nBlock * BLOCK_FRAMES * TEST_CHANNELS],
		             BLOCK_FRAMES, TEST_CHANNELS);
	}

	for (nFrame = TONE_SETTLE; nFrame < TONE_FRAMES; nFrame++)
	{
		double fIn = In[(nFrame * TEST_CHANNELS) + nChannel];
		double fOut = Out[(nFrame * TEST_CHANNELS) + nChannel];
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

static void TestFirImpulse(void)
{
	float Delay[LOWPASS_FIR_TAPS];
	FirFilter Filter;
	uint32_t nFrame;
	uint32_t nDone;

	printf("FIR impulse response\n");

	memset(SigIn, 0, sizeof(SigIn));
	SigIn[0] = 1.0f;

	/* fed in odd-sized blocks, so the delay line wraps mid-block */
	FirInit(&Filter, LowpassFirCoeffs, Delay, LOWPASS_FIR_TAPS);
	for (nDone = 0u; nDone < 3u * LOWPASS_FIR_TAPS; nDone += ODD_BLOCK)
	{
		FirProcess(&Filter, &SigIn[nDone], &SigOut[nDone], ODD_BLOCK);
	}

	for (nFrame = 0u; nFrame < 3u * LOWPASS_FIR_TAPS; nFrame++)
	{
		float fExpect = (nFrame < LOWPASS_FIR_TAPS) ? LowpassFirCoeffs[nFrame] : 0.0f;
		CHECK(SigOut[nFrame] == fExpect, "h[%u]: got %g, expected %g", nFrame, SigOut[nFrame], fExpect);
	}
}

static void TestFirMatchesConvolution(void)
{
	float Delay[LOWPASS_FIR_TAPS];
	FirFilter Filter;
	uint32_t nFrame;
	uint32_t nTap;

	printf("FIR against direct convolution\n");

	Noise(SigIn, REF_FRAMES, 0.5f);
	for (nFrame = 0u; nFrame < REF_FRAMES; nFrame++)
	{
		double fAcc = 0.0;
		for (nTap = 0u; (nTap < LOWPASS_FIR_TAPS) && (nTap <= nFrame); nTap++)
		{
			fAcc += (double)LowpassFirCoeffs[nTap] * SigIn[nFrame - nTap];
		}
		SigRef[nFrame] = (float)fAcc;
	}

	/* in place, as the firmware may call it */
	memcpy(SigOut, SigIn, sizeof(SigOut));
	FirInit(&Filter, LowpassFirCoeffs, Delay, LOWPASS_FIR_TAPS);
	FirProcess(&Filter, SigOut, SigOut, REF_FRAMES);

	CHECK(MaxAbsDiff(SigOut, SigRef, REF_FRAMES) < FILTER_TOL,
	      "max error %g", MaxAbsDiff(SigOut, SigRef, REF_FRAMES));
}

static void TestBiquadMatchesDifferenceEquation(void)
{
	float State[LOWPASS_IIR_SECTIONS * BIQUAD_STATES];
	BiquadCascade Cascade;
	/* per section: x[n-1], x[n-2], y[n-1], y[n-2] */
	double History[LOWPASS_IIR_SECTIONS][4];
	uint32_t nFrame;
	uint32_t nSection;

	printf("IIR against its difference equation\n");

	Noise(SigIn, REF_FRAMES, 0.5f);
	memset(History, 0, sizeof(History));
	for (nFrame = 0u; nFrame < REF_FRAMES; nFrame++)
	{
		double fX = SigIn[nFrame];
		for (nSection = 0u; nSection < LOWPASS_IIR_SECTIONS; nSection++)
		{
			const float *pC = &LowpassIirSos[nSection * BIQUAD_COEFFS];
			double *pH = History[nSection];
			double fY = (pC[0] * fX) + (pC[1] * pH[0]) + (pC[2] * pH[1])
			          - (pC[3] * pH[2]) - (pC[4] * pH[3]);
			pH[1] = pH[0];
			pH[0] = fX;
			pH[3] = pH[2];
			pH[2] = fY;
			fX = fY;
		}
		SigRef[nFrame] = (float)fX;
	}

	memcpy(SigOut, SigIn, sizeof(SigOut));
	BiquadInit(&Cascade, LowpassIirSos, State, LOWPASS_IIR_SECTIONS);
	BiquadProcess(&Cascade, SigOut, SigOut, REF_FRAMES);

	CHECK(MaxAbsDiff(SigOut, SigRef, REF_FRAMES) < FILTER_TOL,
	      "max error %g", MaxAbsDiff(SigOut, SigRef, REF_FRAMES));
}

static void TestBlockBoundary(void)
{
	const uint32_t nFrames = 4u * BLOCK_FRAMES;
	uint32_t nIndex;
	uint32_t nDone;

	printf("block boundaries\n");

	srand(1u);
	for (nIndex = 0u; nIndex < nFrames * TEST_CHANNELS; nIndex++)
	{
		In[nIndex] = (rand() % 16777216) - 8388608;
	}

	/* one call, split internally into AUDIO_PROCESS_MAX_FRAMES chunks */
	AudioProcessInit();
	AudioProcess(In, Ref, nFrames, TEST_CHANNELS);

	/* many small calls of an awkward size */
	AudioProcessInit();
	for (nDone = 0u; nDone < nFrames; nDone += ODD_BLOCK)
	{
		uint32_t nChunk = ((nFrames - nDone) < ODD_BLOCK) ? (nFrames - nDone) : ODD_BLOCK;
		AudioProcess(&In[nDone * TEST_CHANNELS], &Out[nDone * TEST_CHANNELS],
		             nChunk, TEST_CHANNELS);
	}

	CHECK(memcmp(Ref, Out, nFrames * TEST_CHANNELS * sizeof(int32_t)) == 0,
	      "output depends on how the stream is split into blocks");
}

/*
 * An impulse on one channel comes out of that channel only, shaped by the
 * FIR on even channels and the IIR on odd ones; channels past the maximum
 * pass straight through.
 */
static void TestChannels(void)
{
	const uint32_t nChannels = AUDIO_PROCESS_MAX_CHANNELS + 2u;
	const float fAmp = 0.5f;
	float Delay[LOWPASS_FIR_TAPS];
	float State[LOWPASS_IIR_SECTIONS * BIQUAD_STATES];
	FirFilter Filter;
	BiquadCascade Cascade;
	uint32_t nDriven;
	uint32_t nChannel;
	uint32_t nFrame;

	printf("channels\n");

	/* each filter's response to the impulse, computed on its own */
	memset(SigIn, 0, sizeof(SigIn));
	SigIn[0] = fAmp;
	FirInit(&Filter, LowpassFirCoeffs, Delay, LOWPASS_FIR_TAPS);
	FirProcess(&Filter, SigIn, SigRef, BLOCK_FRAMES);
	BiquadInit(&Cascade, LowpassIirSos, State, LOWPASS_IIR_SECTIONS);
	BiquadProcess(&Cascade, SigIn, SigOut, BLOCK_FRAMES);

	for (nDriven = 0u; nDriven < AUDIO_PROCESS_MAX_CHANNELS; nDriven++)
	{
		const float *pExpect = ((nDriven % 2u) == 0u) ? SigRef : SigOut;

		AudioProcessInit();
		memset(In, 0, sizeof(In));
		In[nDriven] = FloatToS24(fAmp);
		for (nFrame = 0u; nFrame < BLOCK_FRAMES; nFrame++)
		{
			for (nChannel = AUDIO_PROCESS_MAX_CHANNELS; nChannel < nChannels; nChannel++)
			{
				In[(nFrame * nChannels) + nChannel] = (int32_t)((nFrame + 1u) * 1000u * nChannel);
			}
		}
		AudioProcess(In, Out, BLOCK_FRAMES, nChannels);

		for (nFrame = 0u; nFrame < BLOCK_FRAMES; nFrame++)
		{
			for (nChannel = 0u; nChannel < nChannels; nChannel++)
			{
				int32_t nGot = Out[(nFrame * nChannels) + nChannel];
				int32_t nExpect;

				if (nChannel >= AUDIO_PROCESS_MAX_CHANNELS)
				{
					nExpect = In[(nFrame * nChannels) + nChannel];
				}
				else if (nChannel == nDriven)
				{
					nExpect = FloatToS24(pExpect[nFrame]);
				}
				else
				{
					nExpect = 0;
				}
				CHECK(nGot == nExpect, "impulse on %u, frame %u channel %u: got %d, expected %d",
				      nDriven, nFrame, nChannel, nGot, nExpect);
			}
		}
	}
}

static void TestFrequencyResponse(void)
{
	/* across the pass band, the transition and the stop band */
	static const double Freqs[] = { 500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0, 4000.0, 6000.0, 12000.0 };
	static const char *Names[] = { "FIR", "IIR" };
	uint32_t nPoint;
	uint32_t nChannel;

	printf("frequency response\n");
	printf("  %4s %8s %12s %12s\n", "", "Hz", "measured dB", "computed dB");

	for (nChannel = 0u; nChannel < 2u; nChannel++)
	{
		for (nPoint = 0u; nPoint < (sizeof(Freqs) / sizeof(Freqs[0])); nPoint++)
		{
			double fComputed = ComputedGainDb((int)nChannel, Freqs[nPoint]);
			double fMeasured = MeasuredGainDb(nChannel, Freqs[nPoint]);

			printf("  %4s %8.0f %12.2f %12.2f\n", Names[nChannel], Freqs[nPoint], fMeasured, fComputed);

			/* far down the stop band the 24-bit rounding floor dominates */
			if (fComputed > -60.0)
			{
				CHECK(fabs(fMeasured - fComputed) < GAIN_TOL_DB,
				      "%s %.0f Hz: measured %.3f dB, computed %.3f dB",
				      Names[nChannel], Freqs[nPoint], fMeasured, fComputed);
			}
		}
	}
}

int main(void)
{
	TestConversions();
	TestFirImpulse();
	TestFirMatchesConvolution();
	TestBiquadMatchesDifferenceEquation();
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
