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
#include "../multirate.c"
#include "../tone.c"
#include "../lowpass_coeffs.c"
#include "../multirate_coeffs.c"

#ifndef M_PI
#define M_PI            (3.14159265358979323846)
#endif

#define FS              ((double)MULTIRATE_SAMPLE_RATE)
#define LOW_FS          (FS / MULTIRATE_FACTOR)
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
/*
 * The same below -60 dB, where float rounding in the prototype's long sums
 * starts to show, and the level under which only an upper bound is checked
 */
#define STOP_TOL_DB     (0.5)
#define FLOOR_DB        (-110.0)
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
 * Gain in dB at fHz of an FIR h[0] .. h[nTaps-1] running at fFs.
 */
static double FirGainDb(const float *pH, uint32_t nTaps, double fHz, double fFs)
{
	double fW = 2.0 * M_PI * fHz / fFs;
	double fRe = 0.0;
	double fIm = 0.0;
	uint32_t nTap;

	for (nTap = 0u; nTap < nTaps; nTap++)
	{
		fRe += pH[nTap] * cos(fW * nTap);
		fIm -= pH[nTap] * sin(fW * nTap);
	}
	return 20.0 * log10(sqrt((fRe * fRe) + (fIm * fIm)));
}

/*
 * Gain in dB at fHz of a cascade of nSections biquad.h sections running at fFs.
 */
static double SosGainDb(const float *pSos, uint32_t nSections, double fHz, double fFs)
{
	double fW = 2.0 * M_PI * fHz / fFs;
	double fGain = 1.0;
	uint32_t nSection;

	for (nSection = 0u; nSection < nSections; nSection++)
	{
		const float *pC = &pSos[nSection * BIQUAD_COEFFS];
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
 * Where a tone at fHz lands once sampled at the low rate: folded into
 * [0, LOW_FS / 2].
 */
static double AliasHz(double fHz)
{
	double fFolded = fmod(fHz, LOW_FS);

	return (fFolded > (LOW_FS / 2.0)) ? (LOW_FS - fFolded) : fFolded;
}

/*
 * Gain of an even channel from a tone at fHz in to its alias out, computed
 * from the coefficient tables: decimator at fHz, IIR and interpolator at the
 * alias. The interpolator's gain of M makes up for the zeros it inserts.
 */
static double ComputedChainGainDb(double fHz)
{
	double fAlias = AliasHz(fHz);

	return FirGainDb(MultirateTaps, MULTIRATE_TAPS, fHz, FS)
	     + SosGainDb(Rate8kIirSos, RATE8K_IIR_SECTIONS, fAlias, LOW_FS)
	     + FirGainDb(MultirateTaps, MULTIRATE_TAPS, fAlias, FS);
}

/*
 * Runs a stream in 64-frame blocks, as audio_path.c does, with a tone on
 * channel 0 and silence elsewhere.
 */
static void RunTone(double fHz)
{
	uint32_t nFrame;
	uint32_t nBlock;

	AudioProcessInit();
	memset(In, 0, sizeof(In));

	for (nFrame = 0u; nFrame < TONE_FRAMES; nFrame++)
	{
		In[nFrame * TEST_CHANNELS] = FloatToS24((float)(TONE_LEVEL * sin(2.0 * M_PI * fHz * nFrame / FS)));
	}

	for (nBlock = 0u; nBlock < TONE_BLOCKS; nBlock++)
	{
		AudioProcess(&In[nBlock * BLOCK_FRAMES * TEST_CHANNELS],
		             &Out[nBlock * BLOCK_FRAMES * TEST_CHANNELS],
		             BLOCK_FRAMES, TEST_CHANNELS);
	}
}

/*
 * Power of channel nChannel of Out once the filters have settled, in squared
 * full-scale units.
 */
static double SettledPower(const int32_t *pFrames, uint32_t nChannel)
{
	double fSum = 0.0;
	uint32_t nFrame;

	for (nFrame = TONE_SETTLE; nFrame < TONE_FRAMES; nFrame++)
	{
		double fX = (double)pFrames[(nFrame * TEST_CHANNELS) + nChannel] / S24_SCALE;
		fSum += fX * fX;
	}
	return fSum / (TONE_FRAMES - TONE_SETTLE);
}

static void TestDecimator(void)
{
	float Delay[MULTIRATE_TAPS];
	Decimator Dec;
	uint32_t nFrame;
	uint32_t nTap;
	uint32_t nDone;
	uint32_t nOut = 0u;

	printf("decimator against filtering then keeping every M-th sample\n");

	Noise(SigIn, REF_FRAMES, 0.5f);
	for (nFrame = 0u; nFrame < REF_FRAMES; nFrame += MULTIRATE_FACTOR)
	{
		double fAcc = 0.0;
		for (nTap = 0u; (nTap < MULTIRATE_TAPS) && (nTap <= nFrame); nTap++)
		{
			fAcc += (double)MultirateTaps[nTap] * SigIn[nFrame - nTap];
		}
		SigRef[nFrame / MULTIRATE_FACTOR] = (float)fAcc;
	}

	/* fed in odd-sized blocks, so the phase is anywhere at a block start */
	DecimatorInit(&Dec, MultirateTaps, Delay, MULTIRATE_TAPS, MULTIRATE_FACTOR);
	for (nDone = 0u; nDone < REF_FRAMES; nDone += ODD_BLOCK)
	{
		uint32_t nChunk = ((REF_FRAMES - nDone) < ODD_BLOCK) ? (REF_FRAMES - nDone) : ODD_BLOCK;
		nOut += DecimatorProcess(&Dec, &SigIn[nDone], nChunk, &SigOut[nOut]);
	}

	CHECK(nOut == MULTIRATE_LOW_FRAMES(REF_FRAMES, MULTIRATE_FACTOR),
	      "%u low-rate samples from %u frames", nOut, REF_FRAMES);
	CHECK(MaxAbsDiff(SigOut, SigRef, nOut) < FILTER_TOL, "max error %g", MaxAbsDiff(SigOut, SigRef, nOut));
}

static void TestInterpolator(void)
{
	float Delay[MULTIRATE_TAPS / MULTIRATE_FACTOR];
	Interpolator Int;
	uint32_t nFrame;
	uint32_t nTap;
	uint32_t nDone;
	uint32_t nUsed = 0u;
	uint32_t nBad = 0u;

	printf("interpolator against zero stuffing then filtering\n");

	/* low-rate input in SigIn, the reference at the codec rate in SigRef */
	Noise(SigIn, REF_FRAMES, 0.5f);
	for (nFrame = 0u; nFrame < REF_FRAMES; nFrame++)
	{
		double fAcc = 0.0;
		for (nTap = 0u; (nTap < MULTIRATE_TAPS) && (nTap <= nFrame); nTap++)
		{
			if (((nFrame - nTap) % MULTIRATE_FACTOR) == 0u)
			{
				fAcc += (double)MultirateTaps[nTap] * SigIn[(nFrame - nTap) / MULTIRATE_FACTOR];
			}
		}
		SigRef[nFrame] = (float)(MULTIRATE_FACTOR * fAcc);
	}

	InterpolatorInit(&Int, MultirateTaps, Delay, MULTIRATE_TAPS, MULTIRATE_FACTOR);
	for (nDone = 0u; nDone < REF_FRAMES; nDone += ODD_BLOCK)
	{
		uint32_t nChunk = ((REF_FRAMES - nDone) < ODD_BLOCK) ? (REF_FRAMES - nDone) : ODD_BLOCK;
		uint32_t nDue = InterpolatorInputs(&Int, nChunk);
		uint32_t nTaken = InterpolatorProcess(&Int, &SigIn[nUsed], &SigOut[nDone], nChunk);

		nBad += (nTaken != nDue) ? 1u : 0u;
		nUsed += nTaken;
	}

	CHECK(nBad == 0u, "%u blocks took a different count than InterpolatorInputs() gave", nBad);
	CHECK(nUsed == MULTIRATE_LOW_FRAMES(REF_FRAMES, MULTIRATE_FACTOR),
	      "%u low-rate samples for %u frames", nUsed, REF_FRAMES);
	CHECK(MaxAbsDiff(SigOut, SigRef, REF_FRAMES) < FILTER_TOL,
	      "max error %g", MaxAbsDiff(SigOut, SigRef, REF_FRAMES));
}

/*
 * An impulse on an even channel comes out of that channel only, shaped by
 * decimator, IIR and interpolator in turn; odd channels carry the tone
 * whatever their input; channels past the maximum pass straight through.
 * The expected outputs are the units run on their own.
 */
static void TestChannels(void)
{
	const uint32_t nChannels = AUDIO_PROCESS_MAX_CHANNELS + 2u;
	const uint32_t nFrames = 4u * BLOCK_FRAMES;
	const float fAmp = 0.5f;
	float DecState[MULTIRATE_TAPS];
	float IirState[RATE8K_IIR_SECTIONS * BIQUAD_STATES];
	float IntState[MULTIRATE_TAPS / MULTIRATE_FACTOR];
	Decimator Dec;
	BiquadCascade Iir;
	Interpolator Int;
	Tone Gen;
	uint32_t nLow;
	uint32_t nDriven;
	uint32_t nChannel;
	uint32_t nFrame;
	uint32_t nBad;

	printf("channels\n");

	/* SigRef: the even-channel chain's response to the impulse */
	memset(SigIn, 0, sizeof(SigIn));
	SigIn[0] = fAmp;
	DecimatorInit(&Dec, MultirateTaps, DecState, MULTIRATE_TAPS, MULTIRATE_FACTOR);
	BiquadInit(&Iir, Rate8kIirSos, IirState, RATE8K_IIR_SECTIONS);
	InterpolatorInit(&Int, MultirateTaps, IntState, MULTIRATE_TAPS, MULTIRATE_FACTOR);
	nLow = DecimatorProcess(&Dec, SigIn, nFrames, SigOut);
	BiquadProcess(&Iir, SigOut, SigOut, nLow);
	(void)InterpolatorProcess(&Int, SigOut, SigRef, nFrames);

	/* SigOut: the tone through the interpolator */
	ToneInit(&Gen, Rate8kTone, RATE8K_TONE_LEN);
	InterpolatorInit(&Int, MultirateTaps, IntState, MULTIRATE_TAPS, MULTIRATE_FACTOR);
	nLow = InterpolatorInputs(&Int, nFrames);
	ToneProcess(&Gen, SigIn, nLow);
	(void)InterpolatorProcess(&Int, SigIn, SigOut, nFrames);

	for (nDriven = 0u; nDriven < AUDIO_PROCESS_MAX_CHANNELS; nDriven++)
	{
		AudioProcessInit();
		memset(In, 0, sizeof(In));
		In[nDriven] = FloatToS24(fAmp);
		for (nFrame = 0u; nFrame < nFrames; nFrame++)
		{
			for (nChannel = AUDIO_PROCESS_MAX_CHANNELS; nChannel < nChannels; nChannel++)
			{
				In[(nFrame * nChannels) + nChannel] = (int32_t)((nFrame + 1u) * 1000u * nChannel);
			}
		}
		for (nFrame = 0u; nFrame < nFrames; nFrame += BLOCK_FRAMES)
		{
			AudioProcess(&In[nFrame * nChannels], &Out[nFrame * nChannels], BLOCK_FRAMES, nChannels);
		}

		for (nChannel = 0u; nChannel < nChannels; nChannel++)
		{
			nBad = 0u;
			for (nFrame = 0u; nFrame < nFrames; nFrame++)
			{
				int32_t nExpect;

				if (nChannel >= AUDIO_PROCESS_MAX_CHANNELS)
				{
					nExpect = In[(nFrame * nChannels) + nChannel];
				}
				else if ((nChannel % 2u) == 1u)
				{
					nExpect = FloatToS24(SigOut[nFrame]);
				}
				else
				{
					nExpect = (nChannel == nDriven) ? FloatToS24(SigRef[nFrame]) : 0;
				}
				nBad += (Out[(nFrame * nChannels) + nChannel] != nExpect) ? 1u : 0u;
			}
			CHECK(nBad == 0u, "impulse on %u: %u frames of channel %u differ", nDriven, nBad, nChannel);
		}
	}
}

static void TestFrequencyResponse(void)
{
	/* the pass band and transition, then tones that alias at the low rate */
	static const double Freqs[] = { 500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0, 3500.0,
	                                5000.0, 7000.0, 9000.0, 13000.0, 19000.0 };
	uint32_t nPoint;

	printf("frequency response, tone in on channel 0, its alias out\n");
	printf("  %8s %8s %12s %12s\n", "Hz", "alias", "measured dB", "computed dB");

	for (nPoint = 0u; nPoint < (sizeof(Freqs) / sizeof(Freqs[0])); nPoint++)
	{
		double fComputed = ComputedChainGainDb(Freqs[nPoint]);
		double fMeasured;

		RunTone(Freqs[nPoint]);
		fMeasured = 10.0 * log10(SettledPower(Out, 0u) / SettledPower(In, 0u));

		printf("  %8.0f %8.0f %12.2f %12.2f\n", Freqs[nPoint], AliasHz(Freqs[nPoint]), fMeasured, fComputed);

		if (fComputed > -60.0)
		{
			CHECK(fabs(fMeasured - fComputed) < GAIN_TOL_DB,
			      "%.0f Hz: measured %.3f dB, computed %.3f dB", Freqs[nPoint], fMeasured, fComputed);
		}
		else if (fComputed > FLOOR_DB)
		{
			CHECK(fabs(fMeasured - fComputed) < STOP_TOL_DB,
			      "%.0f Hz: measured %.3f dB, computed %.3f dB", Freqs[nPoint], fMeasured, fComputed);
		}
		else
		{
			CHECK(fMeasured < FLOOR_DB + STOP_TOL_DB,
			      "%.0f Hz: measured %.3f dB, computed %.3f dB", Freqs[nPoint], fMeasured, fComputed);
		}
	}
}

/*
 * The tone channel carries the table's tone at its frequency, scaled by the
 * interpolator's pass band gain there; what is left over is its images at
 * k LOW_FS +- f, each scaled by the stop band.
 */
static void TestTone(void)
{
	double fTone = (double)RATE8K_TONE_HZ;
	double fTablePower = 0.0;
	double fRe = 0.0;
	double fIm = 0.0;
	double fImagePower = 0.0;
	double fToneGain;
	double fToneDb;
	double fImageDb;
	double fGotToneDb;
	double fGotImageDb;
	uint32_t nIndex;
	uint32_t nFrame;
	uint32_t nCount = TONE_FRAMES - TONE_SETTLE;

	printf("tone channel\n");

	for (nIndex = 0u; nIndex < RATE8K_TONE_LEN; nIndex++)
	{
		fTablePower += (double)Rate8kTone[nIndex] * Rate8kTone[nIndex];
	}
	fTablePower /= RATE8K_TONE_LEN;

	fToneGain = pow(10.0, FirGainDb(MultirateTaps, MULTIRATE_TAPS, fTone, FS) / 20.0);
	fToneDb = 10.0 * log10(fTablePower * fToneGain * fToneGain);
	for (nIndex = 1u; (nIndex * LOW_FS) - fTone < (FS / 2.0); nIndex++)
	{
		double fBelow = (nIndex * LOW_FS) - fTone;
		double fAbove = (nIndex * LOW_FS) + fTone;

		fImagePower += pow(10.0, FirGainDb(MultirateTaps, MULTIRATE_TAPS, fBelow, FS) / 10.0);
		if (fAbove < (FS / 2.0))
		{
			fImagePower += pow(10.0, FirGainDb(MultirateTaps, MULTIRATE_TAPS, fAbove, FS) / 10.0);
		}
	}
	fImageDb = 10.0 * log10(fTablePower * fImagePower);

	/* channel 0's input does not reach the tone channel */
	RunTone(fTone);
	/* the tone's own component, by correlation over whole periods */
	for (nFrame = TONE_SETTLE; nFrame < TONE_FRAMES; nFrame++)
	{
		double fX = (double)Out[(nFrame * TEST_CHANNELS) + 1u] / S24_SCALE;
		fRe += fX * cos(2.0 * M_PI * fTone * nFrame / FS);
		fIm += fX * sin(2.0 * M_PI * fTone * nFrame / FS);
	}
	/* a sine of amplitude A correlates to A N / 2; its power is A^2 / 2 */
	fGotToneDb = 10.0 * log10(2.0 * ((fRe * fRe) + (fIm * fIm)) / ((double)nCount * nCount));
	fGotImageDb = 10.0 * log10(SettledPower(Out, 1u) - pow(10.0, fGotToneDb / 10.0));

	printf("  tone %.2f dB (computed %.2f), images %.2f dB (computed %.2f)\n",
	       fGotToneDb, fToneDb, fGotImageDb, fImageDb);
	CHECK(fabs(fGotToneDb - fToneDb) < GAIN_TOL_DB, "tone %.3f dB, computed %.3f dB", fGotToneDb, fToneDb);
	CHECK(fabs(fGotImageDb - fImageDb) < STOP_TOL_DB, "images %.3f dB, computed %.3f dB", fGotImageDb, fImageDb);
}

int main(void)
{
	TestConversions();
	TestFirImpulse();
	TestFirMatchesConvolution();
	TestBiquadMatchesDifferenceEquation();
	TestDecimator();
	TestInterpolator();
	TestBlockBoundary();
	TestChannels();
	TestFrequencyResponse();
	TestTone();

	if (nFailures != 0u)
	{
		printf("%u check(s) failed\n", nFailures);
		return 1;
	}

	printf("all tests passed\n");
	return 0;
}
