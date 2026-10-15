/*!
 * @file      multirate.h
 * @brief     FIR decimator and interpolator for an integer rate factor M.
 *
 *            The decimator keeps every M-th output of the low-pass
 *            y[n] = sum_k h[k] x[n-k] and computes only those. The
 *            interpolator inserts M - 1 zeros after each low-rate sample and
 *            filters with M h[k]; it skips the products with the zeros, so
 *            each output sums N / M taps (one polyphase branch).
 *
 *            Both count a phase from 0 to M - 1 per high-rate sample, starting
 *            at 0 after Init: the decimator emits a sample on the input at
 *            phase 0, and the interpolator takes a new low-rate sample for the
 *            output at phase 0. A decimator and an interpolator fed the same
 *            number of high-rate frames therefore produce and consume the same
 *            number of low-rate samples, whatever the block sizes. State
 *            carries over between calls. Pure C99, no CCES or board
 *            dependency.
 */

#ifndef __MULTIRATE_H__
#define __MULTIRATE_H__

#include <stdint.h>

typedef struct
{
	const float *pCoeffs;   /* h[0] .. h[nTaps-1] */
	float *pDelay;          /* nTaps past inputs, owned by the caller */
	uint32_t nTaps;
	uint32_t nFactor;
	uint32_t nNewest;       /* index in pDelay of x[n] */
	uint32_t nPhase;        /* phase of the next input */
} Decimator;

typedef struct
{
	const float *pCoeffs;   /* h[0] .. h[nTaps-1], nTaps a multiple of nFactor */
	float *pDelay;          /* nTaps / nFactor past low-rate inputs, owned by the caller */
	uint32_t nTaps;
	uint32_t nFactor;
	uint32_t nNewest;       /* index in pDelay of the newest low-rate input */
	uint32_t nPhase;        /* phase of the next output */
} Interpolator;

/*
 * Largest number of low-rate samples nFrames high-rate frames produce or
 * consume: ceil(nFrames / nFactor). Sizes the low-rate work buffers.
 */
#define MULTIRATE_LOW_FRAMES(nFrames, nFactor)  (((nFrames) + (nFactor) - 1u) / (nFactor))

/*
 * Binds a decimator to its coefficients and delay line and clears its state.
 *
 * Parameters
 *  pDec        decimator to set up
 *  pCoeffs     nTaps coefficients, kept by reference
 *  pDelay      nTaps floats of state, kept by reference
 *  nTaps       filter length, at least 1
 *  nFactor     rate factor M, at least 1
 *
 * Returns
 *  None
 *
 */
void DecimatorInit(Decimator *pDec, const float *pCoeffs, float *pDelay,
                   uint32_t nTaps, uint32_t nFactor);

/*
 * Takes nFrames high-rate samples and writes the low-rate samples due among
 * them.
 *
 * Parameters
 *  pDec        decimator set up by DecimatorInit()
 *  pIn         nFrames high-rate input samples
 *  nFrames     number of input samples
 *  pOut        room for MULTIRATE_LOW_FRAMES(nFrames, nFactor) low-rate samples
 *
 * Returns
 *  The number of low-rate samples written
 *
 */
uint32_t DecimatorProcess(Decimator *pDec, const float *pIn, uint32_t nFrames, float *pOut);

/*
 * Binds an interpolator to its coefficients and delay line and clears its state.
 *
 * Parameters
 *  pInt        interpolator to set up
 *  pCoeffs     nTaps coefficients of the prototype at the high rate, kept by reference
 *  pDelay      nTaps / nFactor floats of state, kept by reference
 *  nTaps       filter length, a multiple of nFactor
 *  nFactor     rate factor M, at least 1
 *
 * Returns
 *  None
 *
 */
void InterpolatorInit(Interpolator *pInt, const float *pCoeffs, float *pDelay,
                      uint32_t nTaps, uint32_t nFactor);

/*
 * Number of low-rate samples the next nFrames outputs of InterpolatorProcess()
 * consume.
 */
uint32_t InterpolatorInputs(const Interpolator *pInt, uint32_t nFrames);

/*
 * Writes nFrames high-rate samples, consuming low-rate samples from pIn as
 * their phase comes up.
 *
 * Parameters
 *  pInt        interpolator set up by InterpolatorInit()
 *  pIn         InterpolatorInputs(pInt, nFrames) low-rate input samples
 *  pOut        nFrames high-rate output samples
 *  nFrames     number of output samples
 *
 * Returns
 *  The number of low-rate samples consumed
 *
 */
uint32_t InterpolatorProcess(Interpolator *pInt, const float *pIn, float *pOut, uint32_t nFrames);

#endif /* __MULTIRATE_H__ */
