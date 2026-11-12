/*!
 * @file      speed.h
 * @brief     Turns the SHARC compiler's optimiser on for the file that includes it.
 *
 *            The CCES project builds its Debug configuration without -O, so
 *            the filter loops would otherwise run as plain unoptimised code.
 *            #pragma optimize_for_speed has the same effect as -O for the
 *            functions after it, whatever the project's settings. Include
 *            this after the other headers of a .c file that does the heavy
 *            per-sample work. Host compilers skip it.
 */

#ifndef __SPEED_H__
#define __SPEED_H__

#if defined(__ADSPSHARC__)
#pragma optimize_for_speed
#endif

#endif /* __SPEED_H__ */
