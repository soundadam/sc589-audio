/*!
 * @file      multirate.c
 * @brief     FIR decimator and interpolator for an integer rate factor M.
 */

#include <string.h>
#include "multirate.h"

void DecimatorInit(Decimator *pDec, const float *pCoeffs, float *pDelay,
                   uint32_t nTaps, uint32_t nFactor)
{
	pDec->pCoeffs = pCoeffs;
	pDec->pDelay = pDelay;
	pDec->nTaps = nTaps;
	pDec->nFactor = nFactor;
	pDec->nNewest = 0u;
	pDec->nPhase = 0u;
	memset(pDelay, 0, nTaps * sizeof(float));
}

uint32_t DecimatorProcess(Decimator *pDec, const float *pIn, uint32_t nFrames, float *pOut)
{
	const float *pH = pDec->pCoeffs;
	float *pX = pDec->pDelay;
	uint32_t nTaps = pDec->nTaps;
	uint32_t nNewest = pDec->nNewest;
	uint32_t nPhase = pDec->nPhase;
	uint32_t nOut = 0u;
	uint32_t nFrame;
	uint32_t nTap;
	float fAcc;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		nNewest = (nNewest + 1u == nTaps) ? 0u : (nNewest + 1u);
		pX[nNewest] = pIn[nFrame];

		if (nPhase == 0u)
		{
			/* as in fir.c: two loops keep the wrap out of the multiply-accumulate */
			fAcc = 0.0f;
			for (nTap = 0u; nTap <= nNewest; nTap++)
			{
				fAcc += pH[nTap] * pX[nNewest - nTap];
			}
			for (; nTap < nTaps; nTap++)
			{
				fAcc += pH[nTap] * pX[nNewest + nTaps - nTap];
			}
			pOut[nOut++] = fAcc;
		}

		nPhase = (nPhase + 1u == pDec->nFactor) ? 0u : (nPhase + 1u);
	}

	pDec->nNewest = nNewest;
	pDec->nPhase = nPhase;
	return nOut;
}

void InterpolatorInit(Interpolator *pInt, const float *pCoeffs, float *pDelay,
                      uint32_t nTaps, uint32_t nFactor)
{
	pInt->pCoeffs = pCoeffs;
	pInt->pDelay = pDelay;
	pInt->nTaps = nTaps;
	pInt->nFactor = nFactor;
	pInt->nNewest = 0u;
	pInt->nPhase = 0u;
	memset(pDelay, 0, (nTaps / nFactor) * sizeof(float));
}

uint32_t InterpolatorInputs(const Interpolator *pInt, uint32_t nFrames)
{
	/* outputs at phase 0 among the next nFrames, the first at index (M - phase) mod M */
	uint32_t nFirst = (pInt->nPhase == 0u) ? 0u : (pInt->nFactor - pInt->nPhase);

	return (nFrames > nFirst) ? MULTIRATE_LOW_FRAMES(nFrames - nFirst, pInt->nFactor) : 0u;
}

uint32_t InterpolatorProcess(Interpolator *pInt, const float *pIn, float *pOut, uint32_t nFrames)
{
	const float *pH = pInt->pCoeffs;
	float *pX = pInt->pDelay;
	uint32_t nFactor = pInt->nFactor;
	uint32_t nLen = pInt->nTaps / nFactor;
	uint32_t nNewest = pInt->nNewest;
	uint32_t nPhase = pInt->nPhase;
	/* the zeros carry 1/M of the power: M restores unity gain in the pass band */
	float fGain = (float)nFactor;
	uint32_t nIn = 0u;
	uint32_t nFrame;
	uint32_t nTap;
	float fAcc;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		if (nPhase == 0u)
		{
			nNewest = (nNewest + 1u == nLen) ? 0u : (nNewest + 1u);
			pX[nNewest] = pIn[nIn++];
		}

		/*
		 * The zero-stuffed input is non-zero nPhase samples back and every M
		 * before that, so output phase p sums h[p], h[p + M], h[p + 2M], ...
		 */
		fAcc = 0.0f;
		for (nTap = 0u; nTap <= nNewest; nTap++)
		{
			fAcc += pH[nPhase + (nTap * nFactor)] * pX[nNewest - nTap];
		}
		for (; nTap < nLen; nTap++)
		{
			fAcc += pH[nPhase + (nTap * nFactor)] * pX[nNewest + nLen - nTap];
		}
		pOut[nFrame] = fGain * fAcc;

		nPhase = (nPhase + 1u == nFactor) ? 0u : (nPhase + 1u);
	}

	pInt->nNewest = nNewest;
	pInt->nPhase = nPhase;
	return nIn;
}
