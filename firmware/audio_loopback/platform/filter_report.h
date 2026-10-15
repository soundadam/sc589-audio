/*!
 * @file      filter_report.h
 * @brief     Measures the multirate chain on the board and prints the result.
 *
 *            Runs an impulse through AudioProcess() and times each stage of
 *            the chain on one FRAMES_PER_BUFFER block, outside the audio
 *            path, and prints one comma-separated line per value:
 *
 *              clock,cclk,<Hz>                     core clock the cycles count
 *              ir,<name>,<n>,<h[n]>                impulse response, n from 0
 *              cycles,<stage>,<frames>,<cycles>    core cycles for one block
 *
 *            The impulse response is chain, channel 0 of AudioProcess(). A
 *            <stage> is decimator, iir (on the low-rate samples of one
 *            block), interpolator, or audio_process for a whole block of
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
