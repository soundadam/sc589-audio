/*!
 * @file      ols.c
 * @brief     FIR filtering by overlap-save, plain or uniformly partitioned.
 */

#include <string.h>
#include "ols.h"
#include "speed.h"

uint32_t OlsSpectra(const RealFft *pFft, const float *pTaps, uint32_t nTaps, float *pSpectra)
{
	uint32_t nLen = pFft->nLen;
	uint32_t nHop = nLen / 2u;
	uint32_t nParts = OLS_PARTS(nTaps, nHop);
	float fScale = 1.0f / (float)nLen;
	uint32_t nPart;
	uint32_t k;

	for (nPart = 0u; nPart < nParts; nPart++)
	{
		float *pH = &pSpectra[nPart * FFT_SPECTRUM_FLOATS(nLen)];
		uint32_t nFirst = nPart * nHop;
		uint32_t nCount = ((nTaps - nFirst) < nHop) ? (nTaps - nFirst) : nHop;

		/* h_p zero-padded to N, so its circular convolution with a frame wraps only into the first B outputs */
		memset(pH, 0, FFT_SPECTRUM_FLOATS(nLen) * sizeof(float));
		memcpy(pH, &pTaps[nFirst], nCount * sizeof(float));
		RealFftForward(pFft, pH);
		for (k = 0u; k < FFT_SPECTRUM_FLOATS(nLen); k++)
		{
			pH[k] *= fScale;
		}
	}
	return nParts;
}

void OlsInit(OlsFilter *pOls, const RealFft *pFft, const float *pSpectra, uint32_t nParts,
             float *pFdl, float *pFrame, float *pWork, float *pOut)
{
	uint32_t nLen = pFft->nLen;

	pOls->pFft = pFft;
	pOls->pSpectra = pSpectra;
	pOls->pFdl = pFdl;
	pOls->pFrame = pFrame;
	pOls->pWork = pWork;
	pOls->pOut = pOut;
	pOls->nHop = nLen / 2u;
	pOls->nParts = nParts;
	pOls->nNewest = 0u;
	pOls->nFill = 0u;
	memset(pFdl, 0, OLS_SPECTRA_FLOATS(nLen, nParts) * sizeof(float));
	memset(pFrame, 0, nLen * sizeof(float));
	memset(pWork, 0, FFT_SPECTRUM_FLOATS(nLen) * sizeof(float));
	memset(pOut, 0, pOls->nHop * sizeof(float));
}

void OlsStagger(OlsFilter *pOls, uint32_t nOffset)
{
	/* frame and held outputs are all zeros, so the offset inputs are zeros too */
	pOls->nFill = nOffset % pOls->nHop;
}

/*
 * One hop: transforms the frame, sums the partitions' products and keeps
 * the B valid samples of the inverse.
 */
static void OlsFrame(OlsFilter *pOls)
{
	const RealFft *pFft = pOls->pFft;
	uint32_t nLen = pFft->nLen;
	uint32_t nHop = pOls->nHop;
	uint32_t nParts = pOls->nParts;
	uint32_t nFloats = FFT_SPECTRUM_FLOATS(nLen);
	float *pY = pOls->pWork;
	uint32_t nPart;
	uint32_t nSlot;
	uint32_t k;

	/* the newest frame spectrum replaces the oldest in the delay line */
	nSlot = (pOls->nNewest + 1u == nParts) ? 0u : (pOls->nNewest + 1u);
	pOls->nNewest = nSlot;
	memcpy(&pOls->pFdl[nSlot * nFloats], pOls->pFrame, nLen * sizeof(float));
	RealFftForward(pFft, &pOls->pFdl[nSlot * nFloats]);

	/* Y = sum_p H_p X_{now - p}, bin by bin as re, im pairs */
	memset(pY, 0, nFloats * sizeof(float));
	for (nPart = 0u; nPart < nParts; nPart++)
	{
		const float *pH = &pOls->pSpectra[nPart * nFloats];
		const float *pX = &pOls->pFdl[nSlot * nFloats];

		for (k = 0u; k < nFloats; k += 2u)
		{
			pY[k] += (pH[k] * pX[k]) - (pH[k + 1u] * pX[k + 1u]);
			pY[k + 1u] += (pH[k] * pX[k + 1u]) + (pH[k + 1u] * pX[k]);
		}
		nSlot = (nSlot == 0u) ? (nParts - 1u) : (nSlot - 1u);
	}

	/* the spectra carry the 1 / N, so the unscaled inverse is the convolution */
	RealFftInverse(pFft, pY);
	memcpy(pOls->pOut, &pY[nHop], nHop * sizeof(float));

	/* the new half of this frame is the old half of the next */
	memcpy(pOls->pFrame, &pOls->pFrame[nHop], nHop * sizeof(float));
}

void OlsProcess(OlsFilter *pOls, const float *pIn, float *pOut, uint32_t nFrames)
{
	uint32_t nHop = pOls->nHop;
	uint32_t nDone = 0u;

	while (nDone < nFrames)
	{
		uint32_t nFill = pOls->nFill;
		uint32_t nRun = nHop - nFill;
		float *pNew = &pOls->pFrame[nHop + nFill];
		const float *pPrev = &pOls->pOut[nFill];
		uint32_t n;

		if (nRun > (nFrames - nDone))
		{
			nRun = nFrames - nDone;
		}
		/* read each input before its output overwrites it, so pIn may be pOut */
		for (n = 0u; n < nRun; n++)
		{
			float fIn = pIn[nDone + n];
			pOut[nDone + n] = pPrev[n];
			pNew[n] = fIn;
		}
		nDone += nRun;
		pOls->nFill = nFill + nRun;

		if (pOls->nFill == nHop)
		{
			OlsFrame(pOls);
			pOls->nFill = 0u;
		}
	}
}
