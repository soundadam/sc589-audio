/*!
 * @file      biquad.h
 * @brief     Cascade of second-order IIR sections on float samples.
 *
 *            Each section is
 *
 *                       b0 + b1 z^-1 + b2 z^-2
 *                H(z) = ----------------------
 *                        1 + a1 z^-1 + a2 z^-2
 *
 *            in transposed direct form II, which needs two state values per
 *            section; the sections run in series, output of one into the
 *            next. State carries over between calls. Pure C99, no CCES or
 *            board dependency.
 */

#ifndef __BIQUAD_H__
#define __BIQUAD_H__

#include <stdint.h>

/* Coefficients per section, in the order b0 b1 b2 a1 a2 */
#define BIQUAD_COEFFS   (5u)
/* State values per section */
#define BIQUAD_STATES   (2u)

typedef struct
{
	const float *pCoeffs;   /* BIQUAD_COEFFS per section, first section first */
	float *pState;          /* BIQUAD_STATES per section, owned by the caller */
	uint32_t nSections;
} BiquadCascade;

/*
 * Binds a cascade to its coefficients and state and clears the state.
 *
 * Parameters
 *  pIir        cascade to set up
 *  pCoeffs     nSections * BIQUAD_COEFFS coefficients, kept by reference
 *  pState      nSections * BIQUAD_STATES floats, kept by reference
 *  nSections   number of second-order sections, at least 1
 *
 * Returns
 *  None
 *
 */
void BiquadInit(BiquadCascade *pIir, const float *pCoeffs, float *pState, uint32_t nSections);

/*
 * Filters nFrames samples through every section. pIn and pOut may be the
 * same array.
 *
 * Parameters
 *  pIir        cascade set up by BiquadInit()
 *  pIn         input samples
 *  pOut        output samples
 *  nFrames     number of samples
 *
 * Returns
 *  None
 *
 */
void BiquadProcess(BiquadCascade *pIir, const float *pIn, float *pOut, uint32_t nFrames);

#endif /* __BIQUAD_H__ */
