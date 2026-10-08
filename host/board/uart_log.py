#!/usr/bin/env python3
"""Read the EZ-Board UART0 console and turn the loopback result into an exit code.

Run after resetting a board that boots from flash (ADR 0001, B1):

    python3 host/board/uart_log.py                      # auto-detect the port
    python3 host/board/uart_log.py /dev/cu.usbserial-XXX --log run.log

Every received line is echoed and, with --log, saved. The settings match
firmware/audio_loopback/platform/uart_console.h (57600 8N1, RTS/CTS).

The loopback never ends on its own, so its start line is the pass; a fail
line within PASS_HOLD_S after it still fails the run.

Exit codes: 0 pass line seen, 1 fail line seen, 2 timeout, 3 no port.
"""

import argparse
import glob
import sys
import time

BAUD = 57600

# Lines printed by firmware/audio_loopback/adc_dac_playback.c main()
PASS_LINES = ("Audio loopback running",)
# The final verdict line, plus the init failures the setup code prints
FAIL_MARKERS = ("Error", "failed", "Failed")
# The pass line comes just before audio DMA starts, and a buffer the loop
# cannot keep up with ends it with "Error" within milliseconds
PASS_HOLD_S = 2.0

EXIT_PASS, EXIT_FAIL, EXIT_TIMEOUT, EXIT_NO_PORT = 0, 1, 2, 3

PORT_GLOBS = ("/dev/cu.usbserial-*", "/dev/cu.usbmodem*", "/dev/ttyUSB*")


def verdict(line):
    """Return EXIT_PASS / EXIT_FAIL for a decisive line, None otherwise."""
    text = line.strip()
    if text in PASS_LINES:
        return EXIT_PASS
    if any(marker in text for marker in FAIL_MARKERS):
        return EXIT_FAIL
    return None


def watch(readline, deadline, out, log=None, clock=time.monotonic, hold=PASS_HOLD_S):
    """Feed lines from readline() until one is decisive or the deadline passes.

    A pass stands once `hold` seconds go by without a fail line.
    readline returns bytes and may return b"" on a read timeout.
    """
    passed_at = None
    while True:
        now = clock()
        if passed_at is not None and now >= passed_at + hold:
            return EXIT_PASS
        if passed_at is None and now >= deadline:
            return EXIT_TIMEOUT
        raw = readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="replace").rstrip("\r\n")
        out.write(line + "\n")
        out.flush()
        if log is not None:
            log.write(line + "\n")
            log.flush()
        result = verdict(line)
        if result == EXIT_FAIL:
            return result
        if result == EXIT_PASS and passed_at is None:
            passed_at = clock()


def find_port():
    ports = sorted(p for pattern in PORT_GLOBS for p in glob.glob(pattern))
    return ports[0] if ports else None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("port", nargs="?", help="serial device (default: first USB serial port)")
    parser.add_argument("--baud", type=int, default=BAUD)
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for a verdict")
    parser.add_argument("--log", help="also write received lines to this file")
    args = parser.parse_args(argv)

    port = args.port or find_port()
    if port is None:
        print("no USB serial port found (looked for %s)" % ", ".join(PORT_GLOBS), file=sys.stderr)
        return EXIT_NO_PORT

    import serial  # pyserial; imported here so verdict()/watch() work without it

    with serial.Serial(port, args.baud, rtscts=True, timeout=0.5) as ser:
        print("listening on %s at %d baud; reset the board now" % (port, args.baud), file=sys.stderr)
        log = open(args.log, "w") if args.log else None
        try:
            result = watch(ser.readline, time.monotonic() + args.timeout, sys.stdout, log)
        finally:
            if log is not None:
                log.close()

    if result == EXIT_TIMEOUT:
        print("no verdict within %.0f s" % args.timeout, file=sys.stderr)
    return result


if __name__ == "__main__":
    sys.exit(main())
