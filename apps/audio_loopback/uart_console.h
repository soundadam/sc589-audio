/*!
 * @file      uart_console.h
 * @brief     Routes stdio (printf) to UART0, the EZ-Board USB-to-serial port.
 *
 * @details
 *            Without an emulator the JTAG console is gone, so printf output
 *            only reaches the host through UART0 (P8 on the SC589 EZ-Board).
 *            Host side: 57600 8N1 with RTS/CTS, e.g.
 *            `screen /dev/cu.usbserial-XXX 57600`.
 */

#ifndef __UART_CONSOLE_H__
#define __UART_CONSOLE_H__

#include <stdint.h>

/* UART0 baud rate; the host terminal must match */
#define UART_CONSOLE_BAUD           (57600u)

/*
 * Bring up the power service, UART0 pins and the STDIO service, then
 * redirect all console streams to UART0. Call before any printf.
 * @return zero on success
 */
uint32_t UartConsoleInit(void);

#endif /* __UART_CONSOLE_H__ */
