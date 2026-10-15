/*!
 * @file      biquad.c
 * @brief     Cascade of second-order IIR sections on float samples.
 */

#include <string.h>
#include "biquad.h"

void BiquadInit(BiquadCascade *pIir, const float *pCoeffs, float *pState, uint32_t nSections)
{
	pIir->pCoeffs = pCoeffs;
	pIir->pState = pState;
	pIir->nSections = nSections;
	memset(pState, 0, nSections * BIQUAD_STATES * sizeof(float));
}

void BiquadProcess(BiquadCascade *pIir, const float *pIn, float *pOut, uint32_t nFrames)
{
	uint32_t nFrame;
	uint32_t nSection;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		float fX = pIn[nFrame];

		for (nSection = 0u; nSection < pIir->nSections; nSection++)
		{
			const float *pC = &pIir->pCoeffs[nSection * BIQUAD_COEFFS];
			float *pS = &pIir->pState[nSection * BIQUAD_STATES];
			/* y = b0 x + s1;  s1 = b1 x - a1 y + s2;  s2 = b2 x - a2 y */
			float fY = (pC[0] * fX) + pS[0];

			pS[0] = (pC[1] * fX) - (pC[3] * fY) + pS[1];
			pS[1] = (pC[2] * fX) - (pC[4] * fY);
			fX = fY;
		}

		pOut[nFrame] = fX;
	}
}
