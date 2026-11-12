/*!
 * @file      filter_report.h
 * @brief     Measures E04's overlap-save filters on the board and prints the result.
 *
 *            Runs a 16 kHz impulse through the plain and the partitioned
 *            overlap-save filters and times each way of filtering one hop
 *            (OLS_HOP low-rate samples) outside the audio path, then times
 *            AudioProcess() over the blocks of one hop. Prints one
 *            comma-separated line per value:
 *
 *              clock,cclk,<Hz>                     core clock the cycles count
 *              ir,<name>,<n>,<h[n]>                impulse response, n from 0
 *              cycles,<stage>,<frames>,<cycles>    core cycles for <frames> samples
 *
 *            The impulse responses are ols (order 255) and partitioned
 *            (order 1023) at 16 kHz, OLS_HOP samples late: h[n - OLS_HOP].
 *            The <stage>s are, for one hop:
 *
 *              fir255, fir1023         point-by-point FIR (fir.h)
 *              ols, partitioned        overlap-save, P = 1 and P = 4 (ols.h)
 *              fft512, ifft512         RealFftForward(), RealFftInverse() of one frame
 *              rfft512                 the CCES run-time library's rfft512() (trans.h)
 *
 *            and audio_process for the FRAMES_PER_BUFFER-frame blocks of one
 *            hop at the codec rate, all NUM_CHANNELS channels, with
 *            audio_process_peak its costliest block.
 *            host/dsplab/boardlog.py reads these lines from a console log.
 */

#ifndef __FILTER_REPORT_H__
#define __FILTER_REPORT_H__

/*
 * Prints the report on the console. Call after UartConsoleInit() and before
 * the audio path starts; leaves audio_process's filter state cleared.
 */
void FilterReport(void);

#endif /* __FILTER_REPORT_H__ */
