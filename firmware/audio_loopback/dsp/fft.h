/*!
 * @file      fft.h
 * @brief     Real FFT and its inverse for a power-of-two length N.
 *
 *            The N real samples are packed as N / 2 complex ones, x[2n] +
 *            j x[2n+1], and taken through a radix-2 decimation-in-time
 *            complex FFT of length N / 2; one more pass splits the even and
 *            odd halves into the N / 2 + 1 bins of the real signal. The
 *            inverse runs the same steps backwards. Both work in place.
 *
 *            A spectrum is N + 2 floats, X[k] as re, im for k = 0 .. N / 2;
 *            X[0] and X[N/2] have zero imaginary parts. The forward
 *            transform is X[k] = sum_n x[n] e^{-j 2 pi k n / N}; the inverse
 *            is not scaled, so inverse(forward(x)) = N x.
 *
 *            Pure C99, no CCES or board dependency.
 */

#ifndef __FFT_H__
#define __FFT_H__

#include <stdint.h>

/* Floats in the spectrum of an nLen-point real signal */
#define FFT_SPECTRUM_FLOATS(nLen)   ((nLen) + 2u)
/* Floats of twiddle table an nLen-point transform needs */
#define FFT_TWIDDLE_FLOATS(nLen)    (nLen)

typedef struct
{
	const float *pTwiddle;  /* e^{-j 2 pi k / N} for k = 0 .. N/2 - 1, as re, im */
	uint32_t nLen;          /* N, a power of two, at least 4 */
	uint32_t nLog2;         /* log2(N / 2), the complex FFT's stages */
} RealFft;

/*
 * Fills the twiddle table and binds it to the transform.
 *
 * Parameters
 *  pFft        transform to set up
 *  pTwiddle    FFT_TWIDDLE_FLOATS(nLen) floats, written here and kept by reference
 *  nLen        transform length N, a power of two from 4 up
 *
 * Returns
 *  0 on success, -1 if nLen is not a power of two from 4 up
 *
 */
int32_t RealFftInit(RealFft *pFft, float *pTwiddle, uint32_t nLen);

/*
 * Spectrum of nLen real samples.
 *
 * Parameters
 *  pFft        transform set up by RealFftInit()
 *  pData       nLen samples in, FFT_SPECTRUM_FLOATS(nLen) floats of spectrum out
 *
 * Returns
 *  None
 *
 */
void RealFftForward(const RealFft *pFft, float *pData);

/*
 * nLen times the real signal whose spectrum is given.
 *
 * Parameters
 *  pFft        transform set up by RealFftInit()
 *  pData       FFT_SPECTRUM_FLOATS(nLen) floats of spectrum in, nLen samples out
 *
 * Returns
 *  None
 *
 */
void RealFftInverse(const RealFft *pFft, float *pData);

#endif /* __FFT_H__ */
