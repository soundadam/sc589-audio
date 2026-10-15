/*!
 * @file      filter_report.h
 * @brief     Measures the low-pass filters on the board and prints the result.
 *
 *            Runs the FIR and the IIR of audio_process on an impulse and
 *            times them on one FRAMES_PER_BUFFER block, outside the audio
 *            path, and prints one comma-separated line per value:
 *
 *              clock,cclk,<Hz>                     core clock the cycles count
 *              ir,<filter>,<n>,<h[n]>              impulse response, n from 0
 *              cycles,<filter>,<frames>,<cycles>   core cycles for one block
 *
 *            <filter> is fir, iir, or audio_process for a whole block of
 *            NUM_CHANNELS channels including the fixed/float conversions.
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
