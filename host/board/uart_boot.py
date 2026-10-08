#!/usr/bin/env python3
"""Boot an EZ-Board over UART0 from the Mac, then read the console for the verdict.

Set SYS_BMODE to UART0 host boot (SC589: 7), then:

    python3 host/board/uart_boot.py adc_dac_playback_SC589_uartslave.ldr --wait 60
    python3 host/board/uart_boot.py app.ldr /dev/cu.usbserial-XXX --log run.log

and press reset while it waits. The .ldr comes from host/build/make_ldr.ps1.

Protocol (SC58x HRM, Boot Modes, "UART Slave Boot Mode"): the host sends '@'
for autobaud, the Boot ROM answers 0xBF, two bytes of the UART_CLK divisor,
0x00, then the host sends the stream in multiples of 1024 bytes while
honouring the processor's RTS on CTS. On the SC589 EZ-Board the divisor
arrives low byte first (27 at 57600 baud, i.e. SCLK0 ~25 MHz).

The Initcode in the stream reprograms the CGU, so SCLK0 changes while the
UART keeps its divisor and the processor's bit rate moves with it. The
stream is therefore sent in two rates: up to the end of each Init block at
the handshake rate, the rest at --init-sclk0 / (16 * divisor). The
application reopens UART0 at uart_log.BAUD and prints within milliseconds of
the last block, so the host switches back as soon as that block is out.

Exit codes are those of uart_log.py, plus 4 when the Boot ROM does not answer
and 5 when the processor stops accepting data.
"""

import argparse
import struct
import sys
import time

import uart_log

EXIT_NO_ACK = 4
EXIT_STALLED = 5

AUTOBAUD_CHAR = b"@"
ACK_FIRST, ACK_LAST = 0xBF, 0x00
# Slave boot modes take the stream in whole internal DMA buffers
BLOCK = 1024

# Block header: BCODE/flags word, target address, byte count, argument
HEADER = struct.Struct("<IIII")
BFLAG_FILL = 1 << 8
BFLAG_INIT = 1 << 11
BFLAG_FINAL = 1 << 15
HDRSIGNS = (0xAD, 0xAC, 0xAB)

# SCLK0 after the EZ-Board init code: 25 MHz CLKIN * MSEL 18 / SYSSEL 2 / S0SEL 2
# (CCES SHARC/ldr/init_code/SC58x_Init/sc589_init_Core0, sc5xx_init.h)
INIT_SCLK0_HZ = 112500000

# The FT232R TX FIFO still holds bytes after the OS buffer drains (256 bytes
# take ~45 ms at 57600 baud), and the Initcode must finish its CGU and DDR
# setup before data arrives at the new rate.
SETTLE_S = 0.5


class BootError(Exception):
    pass


def handshake(ser):
    """Send the autobaud character and return the UART_CLK divisor from the acknowledge."""
    ser.reset_input_buffer()
    ser.write(AUTOBAUD_CHAR)
    ser.flush()
    ack = ser.read(4)
    if len(ack) != 4 or ack[0] != ACK_FIRST or ack[3] != ACK_LAST:
        raise BootError("no Boot ROM acknowledge (got %r); is BMODE set to UART and the board just reset?" % ack)
    return ack[1] | (ack[2] << 8)


def init_block_ends(stream):
    """Return the stream offsets just past each Init block, in order."""
    ends = []
    offset = 0
    while offset + HEADER.size <= len(stream):
        code, _addr, count, _arg = HEADER.unpack_from(stream, offset)
        if code >> 24 not in HDRSIGNS:
            raise BootError("no block header at offset %d; not a binary SC5xx loader stream?" % offset)
        end = offset + HEADER.size + (0 if code & BFLAG_FILL else count)
        if code & BFLAG_INIT:
            ends.append(end)
        offset = end
    return ends


def bit_rate(sclk0_hz, divisor):
    """UART bit rate with 16x oversampling, which autobaud selects (EDBO cleared)."""
    return round(sclk0_hz / (16 * divisor))


def pad(stream):
    """Zero-pad the stream to a whole number of BLOCK bytes."""
    return stream + bytes(-len(stream) % BLOCK)


def plan(stream, handshake_baud, post_init_baud):
    """Split the padded stream into (bytes, baud) segments, switching rate after each Init block.

    The Boot ROM calls the Initcode as soon as it has read the Init block, so
    no byte past it may be on the line while the CGU changes.
    """
    padded = pad(stream)
    segments = []
    start, baud = 0, handshake_baud
    for end in init_block_ends(stream):
        segments.append((padded[start:end], baud))
        start, baud = end, post_init_baud
    segments.append((padded[start:], baud))
    return [(data, rate) for data, rate in segments if data]


def send(ser, segments, out=sys.stderr):
    """Write each segment at its rate; RTS/CTS flow control paces the bytes."""
    total = sum(len(data) for data, _ in segments)
    sent = 0
    for data, baud in segments:
        if ser.baudrate != baud:
            ser.flush()
            time.sleep(SETTLE_S)
            ser.baudrate = baud
            out.write("\nswitched to %d baud at byte %d\n" % (baud, sent))
        for offset in range(0, len(data), BLOCK):
            chunk = data[offset:offset + BLOCK]
            if ser.write(chunk) != len(chunk):
                raise BootError("processor stopped accepting data near byte %d" % sent)
            # the application prints within milliseconds of the last block, so
            # the host must know when that block is on the wire to switch back
            # in time; flush() returns late after a large write
            ser.flush()
            sent += len(chunk)
            out.write("\rsent %d / %d bytes" % (sent, total))
            out.flush()
    out.write("\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("ldr", help="binary loader stream from host/build/make_ldr.ps1")
    parser.add_argument("port", nargs="?", help="serial device (default: first USB serial port)")
    parser.add_argument("--baud", type=int, default=uart_log.BAUD, help="handshake and console rate")
    parser.add_argument("--init-sclk0", type=int, default=INIT_SCLK0_HZ,
                        help="SCLK0 in Hz once the stream's Initcode has run")
    parser.add_argument("--wait", type=float, default=0.0,
                        help="keep retrying the handshake this many seconds, so the board can be reset meanwhile")
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for a verdict after booting")
    parser.add_argument("--log", help="also write received lines to this file")
    args = parser.parse_args(argv)

    with open(args.ldr, "rb") as f:
        stream = f.read()
    try:
        init_block_ends(stream)
    except BootError as e:
        print(e, file=sys.stderr)
        return uart_log.EXIT_FAIL

    port = args.port or uart_log.find_port()
    if port is None:
        print("no USB serial port found (looked for %s)" % ", ".join(uart_log.PORT_GLOBS), file=sys.stderr)
        return uart_log.EXIT_NO_PORT

    import serial  # pyserial; imported here so the helpers work without it

    # write_timeout turns a processor that never releases RTS into an error
    with serial.Serial(port, args.baud, rtscts=True, timeout=1.0, write_timeout=10.0) as ser:
        if args.wait:
            print("press reset on the board within %.0f s" % args.wait, file=sys.stderr)
        deadline = time.monotonic() + args.wait
        while True:
            try:
                divisor = handshake(ser)
                break
            except BootError as e:
                if time.monotonic() >= deadline:
                    print(e, file=sys.stderr)
                    return EXIT_NO_ACK
        post_init_baud = bit_rate(args.init_sclk0, divisor)
        print("Boot ROM acknowledged on %s: divisor %d, SCLK0 ~%.1f MHz; %d baud after Initcode"
              % (port, divisor, 16 * divisor * args.baud / 1e6, post_init_baud), file=sys.stderr)

        start = time.monotonic()
        try:
            send(ser, plan(stream, args.baud, post_init_baud))
        except (BootError, serial.SerialTimeoutException) as e:
            print("\n%s" % (e or "write timed out; the processor holds RTS"), file=sys.stderr)
            return EXIT_STALLED
        print("boot stream sent in %.1f s; reading console at %d baud"
              % (time.monotonic() - start, args.baud), file=sys.stderr)

        ser.baudrate = args.baud
        # bytes received before the switch were sampled at the boot rate
        ser.reset_input_buffer()
        ser.timeout = 0.5
        log = open(args.log, "w") if args.log else None
        try:
            result = uart_log.watch(ser.readline, time.monotonic() + args.timeout, sys.stdout, log)
        finally:
            if log is not None:
                log.close()

    if result == uart_log.EXIT_TIMEOUT:
        print("no verdict within %.0f s" % args.timeout, file=sys.stderr)
    return result


if __name__ == "__main__":
    sys.exit(main())
