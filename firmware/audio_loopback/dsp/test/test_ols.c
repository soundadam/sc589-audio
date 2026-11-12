/*!
 * @file      test_ols.c
 * @brief     Host-side tests for the real FFT and overlap-save filtering.
 *
 *            The FFT is checked against a direct DFT in double precision;
 *            overlap-save, plain and partitioned, against direct convolution
 *            in double precision, delayed by one hop. The filters here are
 *            random, so the tests do not depend on a design. Builds with any
 *            C99 compiler; see Makefile.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../fft.c"
#include "../ols.c"
#include "../fir.c"

#ifndef M_PI
#define M_PI            (3.14159265358979323846)
#endif

/* Largest transform tested */
#define MAX_LEN         (1024u)
/* The transform overlap-save uses in E04, and its hop */
#define OLS_LEN         (512u)
#define OLS_HOP         (OLS_LEN / 2u)
/* Longest filter tested: four partitions */
#define MAX_TAPS        (4u * OLS_HOP)
#define MAX_PARTS       OLS_PARTS(MAX_TAPS, OLS_HOP)
/* Samples per filter run: several hops, not a whole number of them */
#define SIG_FRAMES      (5000u)
/*
 * Allowed error relative to the largest output, for float against double;
 * the FFT's rounding grows like log2 N, well inside these
 */
#define FFT_TOL         (2e-6)
#define OLS_TOL         (2e-6)

static float Twiddle[FFT_TWIDDLE_FLOATS(MAX_LEN)];
static float Data[FFT_SPECTRUM_FLOATS(MAX_LEN)];
static double Ref[FFT_SPECTRUM_FLOATS(MAX_LEN)];

static float Taps[MAX_TAPS];
static float Spectra[OLS_SPECTRA_FLOATS(OLS_LEN, MAX_PARTS)];
static float Fdl[OLS_SPECTRA_FLOATS(OLS_LEN, MAX_PARTS)];
static float Frame[OLS_LEN];
static float Work[FFT_SPECTRUM_FLOATS(OLS_LEN)];
static float Held[OLS_HOP];

static float SigIn[SIG_FRAMES];
static float SigOut[SIG_FRAMES];
static double SigRef[SIG_FRAMES];
static float FirDelay[MAX_TAPS];

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
 * Fills pSig with uniform noise in [-fLevel, fLevel), the same every run
 * for the same seed.
 */
static void Noise(float *pSig, uint32_t nFrames, float fLevel, unsigned nSeed)
{
	uint32_t n;

	srand(nSeed);
	for (n = 0u; n < nFrames; n++)
	{
		pSig[n] = fLevel * (2.0f * ((float)rand() / ((float)RAND_MAX + 1.0f)) - 1.0f);
	}
}

static void TestFftInit(void)
{
	RealFft Fft;

	printf("FFT lengths\n");
	CHECK(RealFftInit(&Fft, Twiddle, 0u) != 0, "length 0 accepted");
	CHECK(RealFftInit(&Fft, Twiddle, 2u) != 0, "length 2 accepted");
	CHECK(RealFftInit(&Fft, Twiddle, 96u) != 0, "length 96 accepted");
	CHECK(RealFftInit(&Fft, Twiddle, 512u) == 0, "length 512 refused");
	CHECK(Fft.nLog2 == 8u, "512 points: %u complex stages, expected 8", Fft.nLog2);
}

/*
 * Forward transform against a direct DFT, then the inverse back to N x.
 */
static void TestFftMatchesDft(void)
{
	static const uint32_t Lengths[] = { 4u, 8u, 16u, 32u, 64u, 512u, 1024u };
	uint32_t nIndex;

	printf("Real FFT against the DFT\n");
	for (nIndex = 0u; nIndex < sizeof(Lengths) / sizeof(Lengths[0]); nIndex++)
	{
		uint32_t nLen = Lengths[nIndex];
		RealFft Fft;
		float In[MAX_LEN];
		double fPeak = 0.0;
		double fErr = 0.0;
		uint32_t k;
		uint32_t n;

		(void)RealFftInit(&Fft, Twiddle, nLen);
		Noise(In, nLen, 1.0f, 100u + nLen);

		for (k = 0u; k <= nLen / 2u; k++)
		{
			double fRe = 0.0;
			double fIm = 0.0;
			for (n = 0u; n < nLen; n++)
			{
				double fAngle = -2.0 * M_PI * (double)((k * n) % nLen) / (double)nLen;
				fRe += In[n] * cos(fAngle);
				fIm += In[n] * sin(fAngle);
			}
			Ref[2u * k] = fRe;
			Ref[(2u * k) + 1u] = fIm;
		}

		memcpy(Data, In, nLen * sizeof(float));
		RealFftForward(&Fft, Data);
		for (k = 0u; k < FFT_SPECTRUM_FLOATS(nLen); k++)
		{
			fPeak = fmax(fPeak, fabs(Ref[k]));
			fErr = fmax(fErr, fabs(Data[k] - Ref[k]));
		}
		CHECK(fErr <= FFT_TOL * fPeak * log2((double)nLen),
		      "N = %u: forward error %.3g of peak %.3g", nLen, fErr, fPeak);
		CHECK((Data[1] == 0.0f) && (Data[nLen + 1u] == 0.0f),
		      "N = %u: DC or Nyquist bin not real", nLen);

		RealFftInverse(&Fft, Data);
		fErr = 0.0;
		for (n = 0u; n < nLen; n++)
		{
			fErr = fmax(fErr, fabs(Data[n] / (double)nLen - In[n]));
		}
		CHECK(fErr <= FFT_TOL * log2((double)nLen),
		      "N = %u: inverse(forward(x)) / N off by %.3g", nLen, fErr);
	}
}

/*
 * Direct convolution of SigIn with Taps in double, delayed by nDelay.
 */
static void Convolve(uint32_t nTaps, uint32_t nDelay)
{
	uint32_t n;
	uint32_t m;

	for (n = 0u; n < SIG_FRAMES; n++)
	{
		double fAcc = 0.0;
		for (m = 0u; (m < nTaps) && (m + nDelay <= n); m++)
		{
			fAcc += (double)Taps[m] * SigIn[n - nDelay - m];
		}
		SigRef[n] = fAcc;
	}
}

/*
 * Runs SigIn through an overlap-save filter staggered by nOffset, in blocks
 * of nBlock, and returns the largest error against SigRef relative to its
 * peak.
 */
static double RunOls(const RealFft *pFft, uint32_t nParts, uint32_t nBlock, uint32_t nOffset)
{
	OlsFilter Ols;
	double fPeak = 0.0;
	double fErr = 0.0;
	uint32_t n;

	OlsInit(&Ols, pFft, Spectra, nParts, Fdl, Frame, Work, Held);
	OlsStagger(&Ols, nOffset);
	for (n = 0u; n < SIG_FRAMES; n += nBlock)
	{
		uint32_t nRun = ((SIG_FRAMES - n) < nBlock) ? (SIG_FRAMES - n) : nBlock;
		OlsProcess(&Ols, &SigIn[n], &SigOut[n], nRun);
	}
	for (n = 0u; n < SIG_FRAMES; n++)
	{
		fPeak = fmax(fPeak, fabs(SigRef[n]));
		fErr = fmax(fErr, fabs(SigOut[n] - SigRef[n]));
	}
	return fErr / fPeak;
}

/*
 * Plain and partitioned overlap-save against direct convolution, with
 * filters that fill their partitions or not, in blocks of different sizes.
 */
static void TestOlsMatchesConvolution(void)
{
	static const uint32_t TapCounts[] = { 1u, 100u, 256u, 300u, 1000u, 1024u };
	static const uint32_t Blocks[] = { 1u, 7u, 21u, 256u, 1000u };
	RealFft Fft;
	uint32_t nIndex;
	uint32_t nBlock;

	printf("Overlap-save against direct convolution\n");
	(void)RealFftInit(&Fft, Twiddle, OLS_LEN);
	Noise(SigIn, SIG_FRAMES, 0.5f, 7u);

	for (nIndex = 0u; nIndex < sizeof(TapCounts) / sizeof(TapCounts[0]); nIndex++)
	{
		uint32_t nTaps = TapCounts[nIndex];
		uint32_t nParts;

		Noise(Taps, nTaps, 0.1f, 11u + nTaps);
		nParts = OlsSpectra(&Fft, Taps, nTaps, Spectra);
		CHECK(nParts == OLS_PARTS(nTaps, OLS_HOP), "%u taps: %u partitions", nTaps, nParts);
		Convolve(nTaps, OLS_HOP);

		for (nBlock = 0u; nBlock < sizeof(Blocks) / sizeof(Blocks[0]); nBlock++)
		{
			double fErr = RunOls(&Fft, nParts, Blocks[nBlock], 0u);
			CHECK(fErr <= OLS_TOL * (double)nParts,
			      "%u taps, %u partition(s), blocks of %u: error %.3g of peak",
			      nTaps, nParts, Blocks[nBlock], fErr);
		}
	}
}

/*
 * Staggered frame boundaries leave the output as it was.
 */
static void TestOlsStagger(void)
{
	static const uint32_t Offsets[] = { 1u, 64u, 192u, OLS_HOP - 1u, OLS_HOP };
	RealFft Fft;
	uint32_t nParts;
	uint32_t nIndex;

	printf("Overlap-save staggered\n");
	(void)RealFftInit(&Fft, Twiddle, OLS_LEN);
	Noise(SigIn, SIG_FRAMES, 0.5f, 7u);
	Noise(Taps, 1000u, 0.1f, 13u);
	nParts = OlsSpectra(&Fft, Taps, 1000u, Spectra);
	Convolve(1000u, OLS_HOP);

	for (nIndex = 0u; nIndex < sizeof(Offsets) / sizeof(Offsets[0]); nIndex++)
	{
		double fErr = RunOls(&Fft, nParts, 21u, Offsets[nIndex]);
		CHECK(fErr <= OLS_TOL * (double)nParts, "staggered by %u: error %.3g of peak",
		      Offsets[nIndex], fErr);
	}
}

/*
 * In place: pIn and pOut the same array.
 */
static void TestOlsInPlace(void)
{
	RealFft Fft;
	OlsFilter Ols;
	uint32_t nParts;
	uint32_t n;
	double fErr = 0.0;

	printf("Overlap-save in place\n");
	(void)RealFftInit(&Fft, Twiddle, OLS_LEN);
	Noise(SigIn, SIG_FRAMES, 0.5f, 7u);
	Noise(Taps, MAX_TAPS, 0.1f, 3u);
	nParts = OlsSpectra(&Fft, Taps, MAX_TAPS, Spectra);
	Convolve(MAX_TAPS, OLS_HOP);

	OlsInit(&Ols, &Fft, Spectra, nParts, Fdl, Frame, Work, Held);
	memcpy(SigOut, SigIn, sizeof(SigIn));
	for (n = 0u; n < SIG_FRAMES; n += 64u)
	{
		uint32_t nRun = ((SIG_FRAMES - n) < 64u) ? (SIG_FRAMES - n) : 64u;
		OlsProcess(&Ols, &SigOut[n], &SigOut[n], nRun);
	}
	for (n = 0u; n < SIG_FRAMES; n++)
	{
		fErr = fmax(fErr, fabs(SigOut[n] - SigRef[n]));
	}
	CHECK(fErr <= 1e-5, "in place: error %.3g", fErr);
}

/*
 * The time-domain FIR, as the reference implementation for the cycle
 * comparison, gives the same output as overlap-save one hop earlier.
 */
static void TestFirAgreesWithOls(void)
{
	RealFft Fft;
	FirFilter Fir;
	OlsFilter Ols;
	uint32_t nParts;
	uint32_t n;
	double fErr = 0.0;
	static float FirOut[SIG_FRAMES];

	printf("Direct FIR against overlap-save\n");
	(void)RealFftInit(&Fft, Twiddle, OLS_LEN);
	Noise(SigIn, SIG_FRAMES, 0.5f, 9u);
	Noise(Taps, OLS_HOP, 0.1f, 5u);
	nParts = OlsSpectra(&Fft, Taps, OLS_HOP, Spectra);

	FirInit(&Fir, Taps, FirDelay, OLS_HOP);
	FirProcess(&Fir, SigIn, FirOut, SIG_FRAMES);
	OlsInit(&Ols, &Fft, Spectra, nParts, Fdl, Frame, Work, Held);
	OlsProcess(&Ols, SigIn, SigOut, SIG_FRAMES);
	for (n = OLS_HOP; n < SIG_FRAMES; n++)
	{
		fErr = fmax(fErr, fabs(SigOut[n] - FirOut[n - OLS_HOP]));
	}
	CHECK(fErr <= 1e-5, "FIR and overlap-save differ by %.3g", fErr);
}

int main(void)
{
	TestFftInit();
	TestFftMatchesDft();
	TestOlsMatchesConvolution();
	TestOlsStagger();
	TestOlsInPlace();
	TestFirAgreesWithOls();

	if (nFailures != 0u)
	{
		printf("%u check(s) failed\n", nFailures);
		return 1;
	}

	printf("all tests passed\n");
	return 0;
}
