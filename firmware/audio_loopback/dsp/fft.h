/*!
 * @file      fft.h
 * @brief     Radix-2 FFT and IFFT of FFT_N points on float samples.
 *
 *            Decimation in time: the input is copied into bit-reversed order
 *            (FftBitRev), then log2 N stages of butterflies run in place,
 *            each taking its twiddle factors from the const tables in
 *            fft_tables.h; nothing is computed with sin or cos at run time.
 *
 *                X[k] = sum_{n=0}^{N-1} x[n] W_N^{nk},          W_N = e^{-j 2 pi / N}
 *                x[n] = 1/N sum_{k=0}^{N-1} X[k] W_N^{-nk}
 *
 *            A real input of N samples is transformed as N/2 complex ones,
 *            z[m] = x[2m] + j x[2m+1], and the spectra of the even and odd
 *            samples separated afterwards; the inverse runs the same steps
 *            backwards. Pure C99, no CCES or board dependency.
 */

#ifndef __FFT_H__
#define __FFT_H__

#include "cplx.h"
#include "fft_tables.h"

/* Bins of a real signal's spectrum, DC to Nyquist */
#define FFT_REAL_BINS   (FFT_N / 2u + 1u)

/*
 * N-point forward transform of complex input.
 *
 * Parameters
 *  pIn         FFT_N samples
 *  pOut        FFT_N bins; must not overlap pIn
 *
 * Returns
 *  None
 *
 */
void FftForward(const Cplx *pIn, Cplx *pOut);

/*
 * N-point inverse transform, scaled by 1/N so that it undoes FftForward().
 *
 * Parameters
 *  pIn         FFT_N bins
 *  pOut        FFT_N samples; must not overlap pIn
 *
 * Returns
 *  None
 *
 */
void FftInverse(const Cplx *pIn, Cplx *pOut);

/*
 * Forward transform of FFT_N real samples, through one transform of N/2
 * points. Only bins 0 .. N/2 are written; the rest are their conjugates,
 * X[N-k] = X[k]*.
 *
 * Parameters
 *  pIn         FFT_N real samples
 *  pOut        FFT_REAL_BINS bins; may not overlap pIn
 *
 * Returns
 *  None
 *
 */
void FftRealForward(const float *pIn, Cplx *pOut);

/*
 * Inverse of FftRealForward(): FFT_N real samples from bins 0 .. N/2.
 * The imaginary parts of bins 0 and N/2 are ignored, as a real signal has
 * none there.
 *
 * Parameters
 *  pIn         FFT_REAL_BINS bins
 *  pOut        FFT_N real samples; may not overlap pIn
 *
 * Returns
 *  None
 *
 */
void FftRealInverse(const Cplx *pIn, float *pOut);

#endif /* __FFT_H__ */
