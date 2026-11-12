/*!
 * @file      fft.c
 * @brief     Radix-2 FFT and IFFT of FFT_N points on float samples.
 *
 *            See fft.h for the transforms; the tables come from
 *            coursework/E03-fft/design.py.
 */

#include <stdint.h>

#include "fft.h"

#define FFT_HALF    (FFT_N / 2u)

/* W_N^k, or W_N^-k when bInverse is set, for k = 0 .. N/2 - 1 */
static inline Cplx Twiddle(uint32_t k, int bInverse)
{
	return CplxMake(FftCos[k], bInverse ? FftSin[k] : -FftSin[k]);
}

/*
 * In-place decimation-in-time butterflies over n points (n a power of two,
 * at most FFT_N), on data already in bit-reversed order. Unscaled.
 *
 * Stage by stage the sub-transforms double in size, nSpan = 2, 4, .. n; the
 * butterfly j of a sub-transform of nSpan points uses W_nSpan^j, which is
 * W_N^(j N / nSpan) in the table.
 */
static void Radix2(Cplx *pX, uint32_t n, int bInverse)
{
	uint32_t nSpan;

	for (nSpan = 2u; nSpan <= n; nSpan <<= 1)
	{
		uint32_t nHalf = nSpan >> 1;
		uint32_t nStride = FFT_N / nSpan;
		uint32_t j;

		for (j = 0u; j < nHalf; j++)
		{
			Cplx w = Twiddle(j * nStride, bInverse);
			uint32_t i;

			for (i = j; i < n; i += nSpan)
			{
				Cplx t = CplxMul(w, pX[i + nHalf]);

				pX[i + nHalf] = CplxSub(pX[i], t);
				pX[i] = CplxAdd(pX[i], t);
			}
		}
	}
}

void FftForward(const Cplx *pIn, Cplx *pOut)
{
	uint32_t n;

	for (n = 0u; n < FFT_N; n++)
	{
		pOut[FftBitRev[n]] = pIn[n];
	}
	Radix2(pOut, FFT_N, 0);
}

void FftInverse(const Cplx *pIn, Cplx *pOut)
{
	const float fScale = 1.0f / (float)FFT_N;
	uint32_t n;

	for (n = 0u; n < FFT_N; n++)
	{
		pOut[FftBitRev[n]] = pIn[n];
	}
	Radix2(pOut, FFT_N, 1);
	for (n = 0u; n < FFT_N; n++)
	{
		pOut[n] = CplxScale(pOut[n], fScale);
	}
}

/*
 * z[m] = x[2m] + j x[2m+1] has the M = N/2 point transform Z[k] = E[k] + j O[k],
 * where E and O are the transforms of the even and odd samples. Each is the
 * transform of a real sequence, so E[M-k]* = E[k] and O[M-k]* = O[k], and
 *
 *     E[k] = (Z[k] + Z[M-k]*) / 2,    O[k] = -j (Z[k] - Z[M-k]*) / 2,
 *     X[k] = E[k] + W_N^k O[k],       k = 0 .. M.
 */
void FftRealForward(const float *pIn, Cplx *pOut)
{
	Cplx z[FFT_HALF];
	uint32_t m;
	uint32_t k;

	for (m = 0u; m < FFT_HALF; m++)
	{
		z[FftBitRevHalf[m]] = CplxMake(pIn[2u * m], pIn[(2u * m) + 1u]);
	}
	Radix2(z, FFT_HALF, 0);

	/* k = 0 and k = M: E and O are real there, and W_N^M = -1 */
	pOut[0] = CplxMake(z[0].re + z[0].im, 0.0f);
	pOut[FFT_HALF] = CplxMake(z[0].re - z[0].im, 0.0f);

	for (k = 1u; k < FFT_HALF; k++)
	{
		Cplx zk = z[k];
		Cplx zc = CplxConj(z[FFT_HALF - k]);
		Cplx e = CplxScale(CplxAdd(zk, zc), 0.5f);
		Cplx o = CplxScale(CplxMulJ(CplxSub(zc, zk)), 0.5f);

		pOut[k] = CplxAdd(e, CplxMul(Twiddle(k, 0), o));
	}
}

/*
 * The steps of FftRealForward() backwards: for k = 0 .. M-1, X[M-k]* = E[k] - W_N^k O[k], so
 *
 *     E[k] = (X[k] + X[M-k]*) / 2,    O[k] = W_N^-k (X[k] - X[M-k]*) / 2,
 *     Z[k] = E[k] + j O[k],
 *
 * then z = IFFT_M(Z) carries the even samples in its real part and the odd
 * ones in its imaginary part.
 */
void FftRealInverse(const Cplx *pIn, float *pOut)
{
	const float fScale = 1.0f / (float)FFT_HALF;
	Cplx z[FFT_HALF];
	uint32_t k;
	uint32_t m;

	for (k = 0u; k < FFT_HALF; k++)
	{
		/* bins 0 and M of a real signal are real: drop any imaginary part */
		Cplx xk = (k == 0u) ? CplxMake(pIn[0].re, 0.0f) : pIn[k];
		Cplx xc = (k == 0u) ? CplxMake(pIn[FFT_HALF].re, 0.0f) : CplxConj(pIn[FFT_HALF - k]);
		Cplx e = CplxScale(CplxAdd(xk, xc), 0.5f);
		Cplx o = CplxScale(CplxMul(Twiddle(k, 1), CplxSub(xk, xc)), 0.5f);

		z[FftBitRevHalf[k]] = CplxAdd(e, CplxMulJ(o));
	}
	Radix2(z, FFT_HALF, 1);

	for (m = 0u; m < FFT_HALF; m++)
	{
		pOut[2u * m] = z[m].re * fScale;
		pOut[(2u * m) + 1u] = z[m].im * fScale;
	}
}
