/*!
 * @file      fir.c
 * @brief     Direct-form FIR filter on float samples, block at a time.
 */

#include <string.h>
#include "fir.h"

void FirInit(FirFilter *pFir, const float *pCoeffs, float *pDelay, uint32_t nTaps)
{
	pFir->pCoeffs = pCoeffs;
	pFir->pDelay = pDelay;
	pFir->nTaps = nTaps;
	pFir->nNewest = 0u;
	memset(pDelay, 0, nTaps * sizeof(float));
}

void FirProcess(FirFilter *pFir, const float *pIn, float *pOut, uint32_t nFrames)
{
	const float *pH = pFir->pCoeffs;
	float *pX = pFir->pDelay;
	uint32_t nTaps = pFir->nTaps;
	uint32_t nNewest = pFir->nNewest;
	uint32_t nFrame;
	uint32_t nTap;
	float fAcc;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		nNewest = (nNewest + 1u == nTaps) ? 0u : (nNewest + 1u);
		pX[nNewest] = pIn[nFrame];

		/*
		 * x[n-k] is pX[nNewest - k] until the index passes 0, then wraps to
		 * the end; two loops keep the modulo out of the multiply-accumulate.
		 */
		fAcc = 0.0f;
		for (nTap = 0u; nTap <= nNewest; nTap++)
		{
			fAcc += pH[nTap] * pX[nNewest - nTap];
		}
		for (; nTap < nTaps; nTap++)
		{
			fAcc += pH[nTap] * pX[nNewest + nTaps - nTap];
		}
		pOut[nFrame] = fAcc;
	}

	pFir->nNewest = nNewest;
}
