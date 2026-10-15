/*!
 * @file      tone.c
 * @brief     Tone generator that repeats a table of one period.
 */

#include "tone.h"

void ToneInit(Tone *pTone, const float *pTable, uint32_t nLen)
{
	pTone->pTable = pTable;
	pTone->nLen = nLen;
	pTone->nIndex = 0u;
}

void ToneProcess(Tone *pTone, float *pOut, uint32_t nFrames)
{
	uint32_t nIndex = pTone->nIndex;
	uint32_t nFrame;

	for (nFrame = 0u; nFrame < nFrames; nFrame++)
	{
		pOut[nFrame] = pTone->pTable[nIndex];
		nIndex = (nIndex + 1u == pTone->nLen) ? 0u : (nIndex + 1u);
	}

	pTone->nIndex = nIndex;
}
