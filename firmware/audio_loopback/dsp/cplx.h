/*!
 * @file      cplx.h
 * @brief     Complex numbers on float, and the arithmetic the FFT needs.
 *
 *            A Cplx is two floats, real part first, the same layout as
 *            CCES's complex_float, so arrays of either can be compared
 *            element by element. The operations are written out here rather
 *            than taken from <complex.h>, which is C99's on the host and
 *            CCES's own on the SHARC. Pure C99, no CCES or board dependency.
 */

#ifndef __CPLX_H__
#define __CPLX_H__

typedef struct
{
	float re;
	float im;
} Cplx;

static inline Cplx CplxMake(float fRe, float fIm)
{
	Cplx z;

	z.re = fRe;
	z.im = fIm;
	return z;
}

static inline Cplx CplxAdd(Cplx a, Cplx b)
{
	return CplxMake(a.re + b.re, a.im + b.im);
}

static inline Cplx CplxSub(Cplx a, Cplx b)
{
	return CplxMake(a.re - b.re, a.im - b.im);
}

/* (a.re + j a.im)(b.re + j b.im): four real multiplies, two adds */
static inline Cplx CplxMul(Cplx a, Cplx b)
{
	return CplxMake((a.re * b.re) - (a.im * b.im), (a.re * b.im) + (a.im * b.re));
}

static inline Cplx CplxConj(Cplx a)
{
	return CplxMake(a.re, -a.im);
}

static inline Cplx CplxScale(Cplx a, float fGain)
{
	return CplxMake(a.re * fGain, a.im * fGain);
}

/* j a: a quarter turn, no multiply */
static inline Cplx CplxMulJ(Cplx a)
{
	return CplxMake(-a.im, a.re);
}

#endif /* __CPLX_H__ */
