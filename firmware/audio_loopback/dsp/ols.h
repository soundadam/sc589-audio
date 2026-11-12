/*!
 * @file      ols.h
 * @brief     FIR filtering by overlap-save, plain or uniformly partitioned.
 *
 *            Frames of N = 2B samples hop by B: each frame is the previous
 *            B inputs followed by B new ones. Its spectrum, times the
 *            spectrum of a B-tap filter zero-padded to N, gives a circular
 *            convolution whose last B samples equal the linear one; the
 *            first B are wrapped around and dropped.
 *
 *            A filter longer than B is cut into P partitions of B taps,
 *            h_p[m] = h[pB + m]. Partition p acts on the input p hops back,
 *            so a frequency-domain delay line keeps the last P frame spectra
 *            and one frame's output spectrum is Y = sum_p H_p X_{now - p}.
 *            One inverse FFT then serves all P partitions. P = 1 is plain
 *            overlap-save.
 *
 *            Samples go in and come out one hop later: the output for the
 *            input x[n] is y[n - B], with y the filter's linear convolution.
 *
 *            Pure C99, no CCES or board dependency.
 */

#ifndef __OLS_H__
#define __OLS_H__

#include <stdint.h>
#include "fft.h"

/* Floats of filter spectra, or of delay line, for nParts partitions */
#define OLS_SPECTRA_FLOATS(nLen, nParts)    ((nParts) * FFT_SPECTRUM_FLOATS(nLen))

typedef struct
{
	const RealFft *pFft;    /* N-point transform, N = 2 nHop */
	const float *pSpectra;  /* nParts filter spectra, scaled by 1 / N */
	float *pFdl;            /* nParts past frame spectra, ring buffer */
	float *pFrame;          /* the last N inputs, oldest first */
	float *pWork;           /* FFT_SPECTRUM_FLOATS(N), the output spectrum */
	float *pOut;            /* the B outputs of the last frame */
	uint32_t nHop;          /* B */
	uint32_t nParts;        /* P */
	uint32_t nNewest;       /* slot in pFdl of the newest frame spectrum */
	uint32_t nFill;         /* inputs taken since the last frame */
} OlsFilter;

/*
 * Number of B-tap partitions an nTaps-tap filter needs.
 */
#define OLS_PARTS(nTaps, nHop)  (((nTaps) + (nHop) - 1u) / (nHop))

/*
 * Cuts a filter into partitions of N / 2 taps and stores their spectra,
 * scaled by 1 / N so that the inverse FFT needs no further scaling.
 *
 * Parameters
 *  pFft        N-point transform
 *  pTaps       h[0] .. h[nTaps-1]
 *  nTaps       filter length; the last partition is zero-padded
 *  pSpectra    OLS_SPECTRA_FLOATS(N, OLS_PARTS(nTaps, N / 2)) floats out
 *
 * Returns
 *  The number of partitions P
 *
 */
uint32_t OlsSpectra(const RealFft *pFft, const float *pTaps, uint32_t nTaps, float *pSpectra);

/*
 * Binds a filter to its spectra and buffers and clears its state.
 *
 * Parameters
 *  pOls        filter to set up
 *  pFft        N-point transform, shared between filters
 *  pSpectra    nParts spectra from OlsSpectra(), shared between filters
 *  nParts      P
 *  pFdl        OLS_SPECTRA_FLOATS(N, nParts) floats
 *  pFrame      N floats
 *  pWork       FFT_SPECTRUM_FLOATS(N) floats
 *  pOut        N / 2 floats
 *
 * Returns
 *  None
 *
 */
void OlsInit(OlsFilter *pOls, const RealFft *pFft, const float *pSpectra, uint32_t nParts,
             float *pFdl, float *pFrame, float *pWork, float *pOut);

/*
 * Moves the filter's frame boundaries: the next frame is transformed after
 * nHop - nOffset inputs instead of nHop, as if nOffset zeros had already come
 * in. The output is unchanged, still y[n - B]; only the hops at which the
 * FFTs run shift. Filters in step with each other can so take their frames in
 * different blocks. Call right after OlsInit().
 *
 * Parameters
 *  pOls        filter set up by OlsInit()
 *  nOffset     0 .. B - 1
 *
 * Returns
 *  None
 *
 */
void OlsStagger(OlsFilter *pOls, uint32_t nOffset);

/*
 * Filters nFrames samples. Blocks of any size may be given; a frame is
 * transformed whenever B new inputs have come in. pIn and pOut may be the
 * same array.
 *
 * Parameters
 *  pOls        filter set up by OlsInit()
 *  pIn         nFrames input samples
 *  pOut        nFrames output samples, B samples behind the input
 *  nFrames     samples in the block
 *
 * Returns
 *  None
 *
 */
void OlsProcess(OlsFilter *pOls, const float *pIn, float *pOut, uint32_t nFrames);

#endif /* __OLS_H__ */
