/*!
 * @file      test_audio_process.c
 * @brief     Host-side tests for the dsp/ sources.
 *
 *            The sources are included directly rather than linked, so the
 *            tests can see the static conversion helpers in audio_process.c.
 *            Expected values come from the same coefficient tables the
 *            firmware uses, recomputed here in double precision; the FFTs are
 *            checked against the DFT sums written out directly. Builds with
 *            any C99 compiler; see Makefile.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../audio_process.c"
#include "../fft.c"
#include "../fft_tables.c"
#include "../fir.c"
#include "../biquad.c"
#include "../multirate.c"
#include "../tone.c"
#include "../lowpass_coeffs.c"
#include "../multirate_coeffs.c"

#ifndef M_PI
#define M_PI            (3.14159265358979323846)
#endif

#define TEST_CHANNELS   (4u)
#define BLOCK_FRAMES    (64u)
/* Frames per channel in the AudioProcess() streams */
#define STREAM_FRAMES   (64u * BLOCK_FRAMES)
/* Allowed difference between a float filter and its double reference */
#define FILTER_TOL      (1e-5)
/*
 * Allowed difference between a float transform and the double DFT, for
 * inputs within [-1, 1): bins grow to N, and each of log2 N stages adds a
 * rounding of about 2^-24 of that
 */
#define FFT_TOL         (1e-5)
/*
 * The same for a forward and inverse transform in turn, whose output is back
 * within [-1, 1): 2 log2 N roundings of up to 2^-24 each, about 6e-7
 */
#define ROUND_TRIP_TOL  (1e-6)
/* ROUND_TRIP_TOL in 24-bit LSB (2^-23 each), plus the output rounding */
#define ROUND_TRIP_LSB  (9)
/* Samples per filter in the float-versus-double comparisons */
#define REF_FRAMES      (4096u)
/* An awkward block size, to split streams off the 64-frame grid */
#define ODD_BLOCK       (7u)
/* Random frames per transform test */
#define FFT_TRIALS      (200u)

static int32_t In[STREAM_FRAMES * TEST_CHANNELS];
static int32_t Out[STREAM_FRAMES * TEST_CHANNELS];
static int32_t Ref[STREAM_FRAMES * TEST_CHANNELS];

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

/* Uniform in [-1, 1), from the same rand() stream as Noise() */
static float Uniform(void)
{
	return 2.0f * ((float)rand() / ((float)RAND_MAX + 1.0f)) - 1.0f;
}

/*
 * X[k] = sum_n x[n] e^{-+j 2 pi n k / N} in double precision, for every k;
 * the inverse also divides by N. pRe/pIm in, pOutRe/pOutIm out.
 */
static void Dft(const double *pRe, const double *pIm, double *pOutRe, double *pOutIm, int bInverse)
{
	double fSign = bInverse ? 1.0 : -1.0;
	double fScale = bInverse ? (1.0 / FFT_N) : 1.0;
	uint32_t k;
	uint32_t n;

	for (k = 0u; k < FFT_N; k++)
	{
		double fRe = 0.0;
		double fIm = 0.0;

		for (n = 0u; n < FFT_N; n++)
		{
			double fArg = fSign * 2.0 * M_PI * (double)((n * k) % FFT_N) / FFT_N;
			fRe += (pRe[n] * cos(fArg)) - (pIm[n] * sin(fArg));
			fIm += (pRe[n] * sin(fArg)) + (pIm[n] * cos(fArg));
		}
		pOutRe[k] = fRe * fScale;
		pOutIm[k] = fIm * fScale;
	}
}

static double CplxErr(Cplx z, double fRe, double fIm)
{
	return hypot((double)z.re - fRe, (double)z.im - fIm);
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

	/* in place, pOut == pIn, as platform/filter_report.c calls it */
	memcpy(Out, In, nFrames * TEST_CHANNELS * sizeof(int32_t));
	AudioProcessInit();
	for (nDone = 0u; nDone < nFrames; nDone += BLOCK_FRAMES)
	{
		AudioProcess(&Out[nDone * TEST_CHANNELS], &Out[nDone * TEST_CHANNELS],
		             BLOCK_FRAMES, TEST_CHANNELS);
	}

	CHECK(memcmp(Ref, Out, nFrames * TEST_CHANNELS * sizeof(int32_t)) == 0,
	      "output differs when processed in place");
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
 * The tables: twiddles on the unit circle at the right angles, and the
 * bit-reversal tables permutations that undo themselves.
 */
static void TestFftTables(void)
{
	uint32_t n;
	uint32_t nBad = 0u;

	printf("fft tables\n");

	for (n = 0u; n < FFT_N / 2u; n++)
	{
		double fArg = 2.0 * M_PI * n / FFT_N;
		CHECK(fabs(FftCos[n] - cos(fArg)) < 1e-7 && fabs(FftSin[n] - sin(fArg)) < 1e-7,
		      "W^%u = %g - j %g", n, FftCos[n], FftSin[n]);
	}
	for (n = 0u; n < FFT_N; n++)
	{
		nBad += (FftBitRev[FftBitRev[n]] != n) ? 1u : 0u;
	}
	for (n = 0u; n < FFT_N / 2u; n++)
	{
		nBad += (FftBitRevHalf[FftBitRevHalf[n]] != n) ? 1u : 0u;
	}
	CHECK(nBad == 0u, "%u bit-reversal entries do not undo themselves", nBad);
	CHECK(FftBitRev[1] == FFT_N / 2u, "FftBitRev[1] = %u", FftBitRev[1]);
}

/*
 * The complex transforms against the DFT, and the inverse undoing the
 * forward, on random frames.
 */
static void TestFftComplex(void)
{
	Cplx x[FFT_N];
	Cplx X[FFT_N];
	Cplx y[FFT_N];
	double fRe[FFT_N];
	double fIm[FFT_N];
	double fDftRe[FFT_N];
	double fDftIm[FFT_N];
	double fFwdErr = 0.0;
	double fInvErr = 0.0;
	double fRoundErr = 0.0;
	uint32_t nTrial;
	uint32_t n;

	printf("complex fft and ifft against the dft\n");

	srand(2u);
	for (nTrial = 0u; nTrial < FFT_TRIALS; nTrial++)
	{
		for (n = 0u; n < FFT_N; n++)
		{
			x[n] = CplxMake(Uniform(), Uniform());
			fRe[n] = x[n].re;
			fIm[n] = x[n].im;
		}
		FftForward(x, X);
		Dft(fRe, fIm, fDftRe, fDftIm, 0);
		for (n = 0u; n < FFT_N; n++)
		{
			fFwdErr = fmax(fFwdErr, CplxErr(X[n], fDftRe[n], fDftIm[n]));
		}

		/* the inverse of the float spectrum, and of the exact one */
		FftInverse(X, y);
		for (n = 0u; n < FFT_N; n++)
		{
			fRoundErr = fmax(fRoundErr, CplxErr(y[n], fRe[n], fIm[n]));
			fRe[n] = X[n].re;
			fIm[n] = X[n].im;
		}
		Dft(fRe, fIm, fDftRe, fDftIm, 1);
		for (n = 0u; n < FFT_N; n++)
		{
			fInvErr = fmax(fInvErr, CplxErr(y[n], fDftRe[n], fDftIm[n]));
		}
	}

	printf("  max error: fft %.2e, ifft %.2e, ifft(fft(x)) - x %.2e\n", fFwdErr, fInvErr, fRoundErr);
	CHECK(fFwdErr < FFT_TOL, "fft error %g", fFwdErr);
	CHECK(fInvErr < ROUND_TRIP_TOL, "ifft error %g", fInvErr);
	CHECK(fRoundErr < ROUND_TRIP_TOL, "round trip error %g", fRoundErr);
}

/*
 * The real-input transform against the DFT and against the complex FFT of
 * the same samples; its inverse against the inverse DFT of a full Hermitian
 * spectrum, and undoing the forward transform.
 */
static void TestFftReal(void)
{
	float x[FFT_N];
	float y[FFT_N];
	Cplx xc[FFT_N];
	Cplx Xc[FFT_N];
	Cplx X[FFT_REAL_BINS];
	double fRe[FFT_N];
	double fIm[FFT_N];
	double fDftRe[FFT_N];
	double fDftIm[FFT_N];
	double fFwdErr = 0.0;
	double fCfftErr = 0.0;
	double fInvErr = 0.0;
	double fRoundErr = 0.0;
	uint32_t nTrial;
	uint32_t n;
	uint32_t k;

	printf("real fft and ifft against the dft\n");

	srand(3u);
	for (nTrial = 0u; nTrial < FFT_TRIALS; nTrial++)
	{
		for (n = 0u; n < FFT_N; n++)
		{
			x[n] = Uniform();
			xc[n] = CplxMake(x[n], 0.0f);
			fRe[n] = x[n];
			fIm[n] = 0.0;
		}
		FftRealForward(x, X);
		FftForward(xc, Xc);
		Dft(fRe, fIm, fDftRe, fDftIm, 0);
		for (k = 0u; k < FFT_REAL_BINS; k++)
		{
			fFwdErr = fmax(fFwdErr, CplxErr(X[k], fDftRe[k], fDftIm[k]));
			fCfftErr = fmax(fCfftErr, CplxErr(X[k], Xc[k].re, Xc[k].im));
		}
		CHECK(X[0].im == 0.0f && X[FFT_N / 2u].im == 0.0f, "bins 0 and N/2 not real");

		FftRealInverse(X, y);
		/* the full spectrum: X[N-k] = X[k]* */
		for (k = 0u; k < FFT_N; k++)
		{
			Cplx Xk = (k <= FFT_N / 2u) ? X[k] : CplxConj(X[FFT_N - k]);
			fRe[k] = Xk.re;
			fIm[k] = Xk.im;
		}
		Dft(fRe, fIm, fDftRe, fDftIm, 1);
		for (n = 0u; n < FFT_N; n++)
		{
			fInvErr = fmax(fInvErr, fabs(y[n] - fDftRe[n]));
			fRoundErr = fmax(fRoundErr, fabs((double)y[n] - x[n]));
		}
	}

	printf("  max error: rfft %.2e (against cfft %.2e), rifft %.2e, rifft(rfft(x)) - x %.2e\n",
	       fFwdErr, fCfftErr, fInvErr, fRoundErr);
	CHECK(fFwdErr < FFT_TOL, "rfft error %g", fFwdErr);
	CHECK(fCfftErr < FFT_TOL, "rfft differs from cfft by %g", fCfftErr);
	CHECK(fInvErr < ROUND_TRIP_TOL, "rifft error %g", fInvErr);
	CHECK(fRoundErr < ROUND_TRIP_TOL, "round trip error %g", fRoundErr);

	/* a real signal has no imaginary part at DC and Nyquist: it is ignored */
	FftRealForward(x, X);
	X[0].im = 1.0f;
	X[FFT_N / 2u].im = -1.0f;
	FftRealInverse(X, SigOut);
	CHECK(MaxAbsDiff(SigOut, y, FFT_N) == 0.0, "imaginary parts of bins 0 and N/2 changed the output");
}

/*
 * A cosine at bin k lands in bin k alone, with magnitude N/2 (N at DC and
 * Nyquist).
 */
static void TestFftBins(void)
{
	float x[FFT_N];
	Cplx X[FFT_REAL_BINS];
	uint32_t k;
	uint32_t n;
	uint32_t j;

	printf("one cosine per bin\n");

	for (k = 0u; k < FFT_REAL_BINS; k++)
	{
		double fExpect = ((k == 0u) || (k == FFT_N / 2u)) ? FFT_N : (FFT_N / 2.0);
		double fLeak = 0.0;

		for (n = 0u; n < FFT_N; n++)
		{
			x[n] = (float)cos(2.0 * M_PI * (double)((n * k) % FFT_N) / FFT_N);
		}
		FftRealForward(x, X);
		for (j = 0u; j < FFT_REAL_BINS; j++)
		{
			if (j != k)
			{
				fLeak = fmax(fLeak, hypot(X[j].re, X[j].im));
			}
		}
		CHECK(fabs(X[k].re - fExpect) < FFT_TOL && fabs(X[k].im) < FFT_TOL,
		      "bin %u: %g + j %g, expected %g", k, X[k].re, X[k].im, fExpect);
		CHECK(fLeak < FFT_TOL, "bin %u leaks %g into other bins", k, fLeak);
	}
}

/*
 * Even channels come out as their input delayed by FFT_N samples, to within
 * the rounding of FFT and IFFT in float; odd channels as each frame low-passed on its own, computed here with
 * the double DFT; channels past the maximum pass straight through.
 */
static void TestChannels(void)
{
	const uint32_t nChannels = AUDIO_PROCESS_MAX_CHANNELS + 2u;
	const uint32_t nFrames = 32u * FFT_N;
	double fRe[FFT_N];
	double fIm[FFT_N];
	double fXRe[FFT_N];
	double fXIm[FFT_N];
	double fY[FFT_N];
	uint32_t nChannel;
	uint32_t nFrame;
	uint32_t nBlock;
	uint32_t k;
	int32_t nDelayErr = 0;
	int32_t nPassErr = 0;
	int32_t nLowErr = 0;

	printf("channels: delayed copy, per-frame low-pass, pass-through\n");

	srand(4u);
	for (nFrame = 0u; nFrame < nFrames * nChannels; nFrame++)
	{
		In[nFrame] = FloatToS24(0.9f * Uniform());
	}

	AudioProcessInit();
	for (nFrame = 0u; nFrame < nFrames; nFrame += BLOCK_FRAMES)
	{
		AudioProcess(&In[nFrame * nChannels], &Out[nFrame * nChannels], BLOCK_FRAMES, nChannels);
	}

	for (nChannel = 0u; nChannel < nChannels; nChannel++)
	{
		for (nFrame = 0u; nFrame < nFrames; nFrame++)
		{
			int32_t nGot = Out[(nFrame * nChannels) + nChannel];

			if (nChannel >= AUDIO_PROCESS_MAX_CHANNELS)
			{
				nPassErr = (int32_t)fmax(nPassErr, abs(nGot - In[(nFrame * nChannels) + nChannel]));
			}
			else if (nFrame < FFT_N)
			{
				/* the first frame is still filling: silence */
				nDelayErr = (int32_t)fmax(nDelayErr, abs(nGot));
			}
			else if ((nChannel % 2u) == 0u)
			{
				nDelayErr = (int32_t)fmax(nDelayErr, abs(nGot - In[((nFrame - FFT_N) * nChannels) + nChannel]));
			}
		}
	}

	/* odd channels: each input frame, DFT, mask, inverse DFT, then out a frame later */
	for (nChannel = 1u; nChannel < AUDIO_PROCESS_MAX_CHANNELS; nChannel += 2u)
	{
		for (nBlock = 0u; nBlock + 1u < nFrames / FFT_N; nBlock++)
		{
			for (k = 0u; k < FFT_N; k++)
			{
				fRe[k] = S24ToFloat(In[(((nBlock * FFT_N) + k) * nChannels) + nChannel]);
				fIm[k] = 0.0;
			}
			Dft(fRe, fIm, fXRe, fXIm, 0);
			for (k = 0u; k < FFT_N; k++)
			{
				double fGain = FftBinGain[(k <= FFT_N / 2u) ? k : (FFT_N - k)];
				fXRe[k] *= fGain;
				fXIm[k] *= fGain;
			}
			Dft(fXRe, fXIm, fY, fIm, 1);
			for (k = 0u; k < FFT_N; k++)
			{
				int32_t nGot = Out[((((nBlock + 1u) * FFT_N) + k) * nChannels) + nChannel];
				int32_t nExpect = (int32_t)lround(fY[k] * S24_SCALE);
				nLowErr = (int32_t)fmax(nLowErr, abs(nGot - nExpect));
			}
		}
	}

	printf("  max error in LSB: delayed copy %d, low-pass %d, pass-through %d\n", nDelayErr, nLowErr, nPassErr);
	CHECK(nDelayErr <= ROUND_TRIP_LSB, "even channels differ from the delayed input by %d LSB", nDelayErr);
	CHECK(nLowErr <= ROUND_TRIP_LSB, "odd channels differ from the per-frame low-pass by %d LSB", nLowErr);
	CHECK(nPassErr == 0, "channels past the maximum changed by %d LSB", nPassErr);
}

int main(void)
{
	TestConversions();
	TestFirImpulse();
	TestFirMatchesConvolution();
	TestBiquadMatchesDifferenceEquation();
	TestDecimator();
	TestInterpolator();
	TestFftTables();
	TestFftComplex();
	TestFftReal();
	TestFftBins();
	TestChannels();
	TestBlockBoundary();

	if (nFailures != 0u)
	{
		printf("%u check(s) failed\n", nFailures);
		return 1;
	}

	printf("all tests passed\n");
	return 0;
}
