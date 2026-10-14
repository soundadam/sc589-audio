/*!
 * @file      uart_console.c
 * @brief     Routes stdio (printf) to UART0, the EZ-Board USB-to-serial port.
 */

#include <sys/platform.h>
#include <builtins.h>
#include <services/pwr/adi_pwr.h>
#include <services/stdio/adi_stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "platform/uart_console.h"

/* EZ-Board CLKIN */
#define CLKIN_HZ                    (25000000u)

/* UART0 is the port wired to the on-board USB-to-serial bridge */
#define UART_CONSOLE_DEV_ID         (0u)

/*
 * Core cycles to hold the first console line back: 0.2 s at 450 MHz. After a
 * UART boot the host still has the end of the stream in its USB-to-serial
 * FIFO when the application starts, and only then switches back to
 * UART_CONSOLE_BAUD; anything printed before that is lost
 * (host/board/uart_boot.py).
 */
#define UART_CONSOLE_SETTLE_CYCLES  (90000000ull)

/*
 * UART0 pins, taken from the Pin Multiplexing add-in output of
 * vendor/adi/examples/services/stdio/stdio_char_echo/<board>/sharc/stdio_char_echo_Core1.
 * Applied read-modify-write
 * so pins configured elsewhere keep their function.
 */
#if defined(__ADSPSC589_FAMILY__)
/* TX PC13, RX PC14, RTS PC15, CTS PD00; all MUX function 0 */
#define UART0_PORTC_FER_BITS        ((1u << 13) | (1u << 14) | (1u << 15))
#define UART0_PORTC_MUX_MASK        ((3u << 26) | (3u << 28) | (3u << 30))
#define UART0_PORTD_FER_BITS        (1u << 0)
#define UART0_PORTD_MUX_MASK        (3u << 0)
#else
#error "uart_console.c: UART0 pin mapping is only known for ADSP-SC589"
#endif

static ADI_STDIO_DEVICE_HANDLE hStdioJtag = NULL;
static ADI_STDIO_DEVICE_HANDLE hStdioUart = NULL;

static void WaitForHost(void)
{
	unsigned long long Start = emuclk();

	while((emuclk() - Start) < UART_CONSOLE_SETTLE_CYCLES)
	{
	}
}

static void Uart0PinMux(void)
{
	*pREG_PORTC_MUX &= ~UART0_PORTC_MUX_MASK;
	*pREG_PORTD_MUX &= ~UART0_PORTD_MUX_MASK;
	*pREG_PORTC_FER |= UART0_PORTC_FER_BITS;
	*pREG_PORTD_FER |= UART0_PORTD_FER_BITS;
}

uint32_t UartConsoleInit(void)
{
	/* UART baud divisors are derived from SCLK, which the power service reports */
	if(adi_pwr_Init(0u, CLKIN_HZ) != ADI_PWR_SUCCESS)
	{
		return 1u;
	}

	Uart0PinMux();

	if(adi_stdio_Init(&hStdioJtag) != ADI_STDIO_RESULT_SUCCESS)
	{
		return 1u;
	}

	adi_stdio_RegisterUART();

	if(adi_stdio_OpenDevice(ADI_STDIO_DEVICE_TYPE_UART, UART_CONSOLE_DEV_ID, &hStdioUart) != ADI_STDIO_RESULT_SUCCESS)
	{
		return 1u;
	}

	if(adi_stdio_ControlDevice(hStdioUart, ADI_STDIO_COMMAND_SET_UART_BAUD_RATE, (void *)UART_CONSOLE_BAUD) != ADI_STDIO_RESULT_SUCCESS)
	{
		return 1u;
	}

	if(adi_stdio_RedirectStream(hStdioUart, ADI_STDIO_STREAM_ALL_CONSOLE_IO) != ADI_STDIO_RESULT_SUCCESS)
	{
		return 1u;
	}

	WaitForHost();

	return 0u;
}
