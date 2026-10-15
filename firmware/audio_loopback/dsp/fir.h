/*!
 * @file      fir.h
 * @brief     Direct-form FIR filter on float samples, block at a time.
 *
 *            y[n] = sum_{k=0}^{N-1} h[k] x[n-k]. The last N inputs sit in a
 *            circular delay line, so the filter keeps its state between
 *            calls and a stream may be fed in blocks of any size. Pure C99,
 *            no CCES or board dependency.
 */

#ifndef __FIR_H__
#define __FIR_H__

#include <stdint.h>

typedef struct
{
	const float *pCoeffs;   /* h[0] .. h[nTaps-1] */
	float *pDelay;          /* nTaps past inputs, owned by the caller */
	uint32_t nTaps;
	uint32_t nNewest;       /* index in pDelay of x[n] */
} FirFilter;

/*
 * Binds a filter to its coefficients and delay line and clears the delay line.
 *
 * Parameters
 *  pFir        filter to set up
 *  pCoeffs     nTaps coefficients, kept by reference
 *  pDelay      nTaps floats of state, kept by reference
 *  nTaps       filter length, at least 1
 *
 * Returns
 *  None
 *
 */
void FirInit(FirFilter *pFir, const float *pCoeffs, float *pDelay, uint32_t nTaps);

/*
 * Filters nFrames samples. pIn and pOut may be the same array.
 *
 * Parameters
 *  pFir        filter set up by FirInit()
 *  pIn         input samples
 *  pOut        output samples
 *  nFrames     number of samples
 *
 * Returns
 *  None
 *
 */
void FirProcess(FirFilter *pFir, const float *pIn, float *pOut, uint32_t nFrames);

#endif /* __FIR_H__ */
