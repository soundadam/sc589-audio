/*!
 * @file      tone.h
 * @brief     Tone generator that repeats a table of one period.
 *
 *            The table holds a whole number of periods at the rate the tone
 *            is generated at, so repeating it is exact and never drifts.
 *            Pure C99, no CCES or board dependency.
 */

#ifndef __TONE_H__
#define __TONE_H__

#include <stdint.h>

typedef struct
{
	const float *pTable;    /* one period, nLen samples */
	uint32_t nLen;
	uint32_t nIndex;        /* index in pTable of the next sample */
} Tone;

/*
 * Binds a generator to its table and restarts it at the table's first sample.
 *
 * Parameters
 *  pTone       generator to set up
 *  pTable      nLen samples, kept by reference
 *  nLen        table length, at least 1
 *
 * Returns
 *  None
 *
 */
void ToneInit(Tone *pTone, const float *pTable, uint32_t nLen);

/*
 * Writes the next nFrames samples of the tone.
 *
 * Parameters
 *  pTone       generator set up by ToneInit()
 *  pOut        nFrames output samples
 *  nFrames     number of samples
 *
 * Returns
 *  None
 *
 */
void ToneProcess(Tone *pTone, float *pOut, uint32_t nFrames);

#endif /* __TONE_H__ */
