/*!
 * @file      filter_report.h
 * @brief     Checks the E03 FFT against the CCES library on the board and
 *            prints the result.
 *
 *            Runs the own transforms (dsp/fft.h) and the library's rfft32,
 *            cfft32 and ifft32 (trans.h) on the same 32-point frame, sends an
 *            impulse and noise through AudioProcess(), times each transform
 *            outside the audio path, and prints one comma-separated line per
 *            value:
 *
 *              clock,cclk,<Hz>                     core clock the cycles count
 *              fft,<name>,<k>,<re>,<im>            entry k of a vector
 *              ir,<name>,<n>,<h[n]>                impulse response, n from 0
 *              diff,<name>,<value>                 a measured difference
 *              cycles,<name>,<frames>,<cycles>     fewest core cycles of 8 runs
 *
 *            Vectors: re_in and im_in (the test frame), rfft_own (bins
 *            0 .. N/2), rifft_own, cfft_own, ifft_own (of cfft_own), and the
 *            library's rfft_lib, cfft_lib and ifft_lib (of cfft_own), each
 *            printed as returned so that the host reads off their scaling.
 *            Impulse responses: identity (channel 0) and lowpass (channel 1).
 *            diff,loopback_lsb is the largest difference, in 24-bit LSB,
 *            between an even channel's output and its input 32 samples
 *            earlier, on noise. Cycles: each transform on one frame, frame
 *            for one odd channel's FFT, gain and IFFT, and audio_process for
 *            a whole block of NUM_CHANNELS channels.
 *            host/dsplab/boardlog.py reads these lines from a console log.
 */

#ifndef __FILTER_REPORT_H__
#define __FILTER_REPORT_H__

/*
 * Prints the report on the console. Call after UartConsoleInit() and before
 * the audio path starts; leaves audio_process's frames cleared.
 */
void FilterReport(void);

#endif /* __FILTER_REPORT_H__ */
