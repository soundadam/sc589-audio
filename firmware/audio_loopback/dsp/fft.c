/*!
 * @file      fft.c
 * @brief     Real FFT and its inverse for a power-of-two length N.
 */

#include <math.h>
#include "fft.h"
#include "speed.h"

#ifndef M_PI
#define M_PI    (3.14159265358979323846)
#endif

int32_t RealFftInit(RealFft *pFft, float *pTwiddle, uint32_t nLen)
{
	uint32_t nLog2 = 0u;
	uint32_t k;

	if ((nLen < 4u) || ((nLen & (nLen - 1u)) != 0u))
	{
		return -1;
	}
	while ((2u << nLog2) < nLen)
	{
		nLog2++;
	}

	for (k = 0u; k < nLen / 2u; k++)
	{
		double fAngle = -2.0 * M_PI * (double)k / (double)nLen;
		pTwiddle[2u * k] = (float)cos(fAngle);
		pTwiddle[(2u * k) + 1u] = (float)sin(fAngle);
	}

	pFft->pTwiddle = pTwiddle;
	pFft->nLen = nLen;
	pFft->nLog2 = nLog2;
	return 0;
}

/*
 * In-place radix-2 FFT of the N / 2 complex values in pData, re, im
 * interleaved. fSign is -1 for the forward transform, +1 for the inverse
 * (unscaled): it is the sign of the twiddles' imaginary parts.
 */
static void ComplexFft(const RealFft *pFft, float *pData, float fSign)
{
	const float *pW = pFft->pTwiddle;
	uint32_t nPoints = pFft->nLen / 2u;
	uint32_t nLog2 = pFft->nLog2;
	uint32_t i;
	uint32_t j;
	uint32_t nSpan;

	/* bit-reversed order, so the butterflies below can run in place */
	for (i = 0u; i < nPoints; i++)
	{
		uint32_t nRev = 0u;
		uint32_t nBits = i;
		uint32_t nBit;

		for (nBit = 0u; nBit < nLog2; nBit++)
		{
			nRev = (nRev << 1u) | (nBits & 1u);
			nBits >>= 1u;
		}
		if (nRev > i)
		{
			float fRe = pData[2u * i];
			float fIm = pData[(2u * i) + 1u];
			pData[2u * i] = pData[2u * nRev];
			pData[(2u * i) + 1u] = pData[(2u * nRev) + 1u];
			pData[2u * nRev] = fRe;
			pData[(2u * nRev) + 1u] = fIm;
		}
	}

	/*
	 * Stage by stage, a butterfly joins two transforms of nSpan points into
	 * one of 2 nSpan. Its twiddle is e^{-j 2 pi j / (2 nSpan)}, which is
	 * entry j N / (2 nSpan) of the N-point table.
	 */
	for (nSpan = 1u; nSpan < nPoints; nSpan <<= 1u)
	{
		uint32_t nStride = pFft->nLen / (2u * nSpan);

		for (j = 0u; j < nSpan; j++)
		{
			float fWr = pW[2u * j * nStride];
			float fWi = fSign * -pW[(2u * j * nStride) + 1u];

			for (i = j; i < nPoints; i += 2u * nSpan)
			{
				float *pA = &pData[2u * i];
				float *pB = &pData[2u * (i + nSpan)];
				float fBr = (pB[0] * fWr) - (pB[1] * fWi);
				float fBi = (pB[0] * fWi) + (pB[1] * fWr);

				pB[0] = pA[0] - fBr;
				pB[1] = pA[1] - fBi;
				pA[0] = pA[0] + fBr;
				pA[1] = pA[1] + fBi;
			}
		}
	}
}

void RealFftForward(const RealFft *pFft, float *pData)
{
	const float *pW = pFft->pTwiddle;
	uint32_t nHalf = pFft->nLen / 2u;
	uint32_t k;
	float fRe;
	float fIm;

	/* Z = FFT of z[n] = x[2n] + j x[2n+1], N / 2 points */
	ComplexFft(pFft, pData, -1.0f);

	/*
	 * With M = N / 2 and W = e^{-j 2 pi k / N}, the even and odd samples'
	 * spectra are E[k] = (Z[k] + Z*[M-k]) / 2 and O[k] = (Z[k] - Z*[M-k]) / 2j,
	 * and X[k] = E[k] + W O[k]. Since X[M-k] = (E[k] - W O[k])*, one pass
	 * over k <= M / 2 fills both halves in place.
	 */
	fRe = pData[0];
	fIm = pData[1];
	pData[0] = fRe + fIm;
	pData[1] = 0.0f;
	pData[2u * nHalf] = fRe - fIm;
	pData[(2u * nHalf) + 1u] = 0.0f;

	for (k = 1u; k <= nHalf / 2u; k++)
	{
		float *pK = &pData[2u * k];
		float *pM = &pData[2u * (nHalf - k)];
		float fEr = 0.5f * (pK[0] + pM[0]);
		float fEi = 0.5f * (pK[1] - pM[1]);
		/* O = (Z[k] - Z*[M-k]) / 2j */
		float fOr = 0.5f * (pK[1] + pM[1]);
		float fOi = -0.5f * (pK[0] - pM[0]);
		float fWr = pW[2u * k];
		float fWi = pW[(2u * k) + 1u];
		float fTr = (fWr * fOr) - (fWi * fOi);
		float fTi = (fWr * fOi) + (fWi * fOr);

		pK[0] = fEr + fTr;
		pK[1] = fEi + fTi;
		/* at k = M / 2, pK and pM are the same bin and these agree with the above */
		pM[0] = fEr - fTr;
		pM[1] = -(fEi - fTi);
	}
}

void RealFftInverse(const RealFft *pFft, float *pData)
{
	const float *pW = pFft->pTwiddle;
	uint32_t nHalf = pFft->nLen / 2u;
	uint32_t k;
	float fX0;
	float fXm;

	/*
	 * The forward pass backwards, without its halves: from X[k] and
	 * X[M-k], E = X[k] + X*[M-k] and O = (X[k] - X*[M-k]) W*, then
	 * Z[k] = E + jO is twice the forward pass's Z. The unscaled N / 2-point
	 * inverse of that gives 2 (N / 2) = N times x.
	 */
	fX0 = pData[0];
	fXm = pData[2u * nHalf];
	pData[0] = fX0 + fXm;
	pData[1] = fX0 - fXm;

	for (k = 1u; k <= nHalf / 2u; k++)
	{
		float *pK = &pData[2u * k];
		float *pM = &pData[2u * (nHalf - k)];
		float fEr = pK[0] + pM[0];
		float fEi = pK[1] - pM[1];
		float fDr = pK[0] - pM[0];
		float fDi = pK[1] + pM[1];
		/* O = D W*, W = e^{-j 2 pi k / N} */
		float fWr = pW[2u * k];
		float fWi = -pW[(2u * k) + 1u];
		float fOr = (fDr * fWr) - (fDi * fWi);
		float fOi = (fDr * fWi) + (fDi * fWr);

		/* Z[k] = E + jO; Z[M-k] = E* + j O*, by the same symmetry as the forward pass */
		pK[0] = fEr - fOi;
		pK[1] = fEi + fOr;
		pM[0] = fEr + fOi;
		pM[1] = -fEi + fOr;
	}

	ComplexFft(pFft, pData, 1.0f);
}
