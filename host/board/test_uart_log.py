"""Tests for uart_log.watch(); no serial port or pyserial needed.

    python3 -m unittest discover tools
"""

import io
import unittest

import uart_log

PASS = uart_log.PASS_LINES[0].encode()


def lines(*chunks):
    it = iter(chunks)
    return lambda: next(it, b"")


class FakeClock:
    """Advances one second per call so a stream of empty reads times out."""

    def __init__(self):
        self.t = 0.0

    def __call__(self):
        self.t += 1.0
        return self.t


class WatchTest(unittest.TestCase):
    def run_watch(self, readline, deadline=100.0):
        out = io.StringIO()
        return uart_log.watch(readline, deadline, out, clock=FakeClock()), out.getvalue()

    def test_pass_line_after_noise_and_read_timeouts(self):
        result, out = self.run_watch(lines(b"boot\r\n", b"", PASS + b"\r\n", b"after\r\n"))
        self.assertEqual(result, uart_log.EXIT_PASS)
        self.assertEqual(out, "boot\n%s\nafter\n" % PASS.decode())

    def test_fail_line_soon_after_the_pass_line_is_a_fail(self):
        result, _ = self.run_watch(lines(PASS + b"\r\n", b"Error\r\n"))
        self.assertEqual(result, uart_log.EXIT_FAIL)

    def test_pass_stands_past_the_deadline(self):
        result, _ = self.run_watch(lines(PASS + b"\r\n"), deadline=2.0)
        self.assertEqual(result, uart_log.EXIT_PASS)

    def test_codec_init_failure_is_a_fail(self):
        result, _ = self.run_watch(lines(b"ADAU1979: adi_adau1979_Open failed\r\n", PASS + b"\r\n"))
        self.assertEqual(result, uart_log.EXIT_FAIL)

    def test_pass_text_inside_a_longer_line_is_not_a_pass(self):
        result, _ = self.run_watch(lines(b"waiting: " + PASS + b"\r\n"), deadline=5.0)
        self.assertEqual(result, uart_log.EXIT_TIMEOUT)

    def test_garbage_bytes_do_not_raise(self):
        result, out = self.run_watch(lines(b"\xff\xfe\r\n", b"Error\r\n"))
        self.assertEqual(result, uart_log.EXIT_FAIL)
        self.assertIn("�", out)


if __name__ == "__main__":
    unittest.main()
