"""Tests for the uart_boot helpers; no serial port or pyserial needed.

    python3 -m unittest discover tools
"""

import threading
import unittest

import uart_boot


def block(flags, payload=b"", count=None, core=0):
    """One loader block; the header checksum is not checked by uart_boot."""
    code = ((0xAD - core) << 24) | flags
    n = len(payload) if count is None else count
    return uart_boot.HEADER.pack(code, 0, n, 0) + payload


class FakeSerial:
    def __init__(self, reply):
        self.reply = reply
        self.written = b""

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.written += data

    def flush(self):
        pass

    def read(self, n):
        out, self.reply = self.reply[:n], self.reply[n:]
        return out


class HandshakeTest(unittest.TestCase):
    def test_returns_divisor_low_byte_first(self):
        ser = FakeSerial(bytes([0xBF, 0x1B, 0x01, 0x00]))
        self.assertEqual(uart_boot.handshake(ser), 0x011B)
        self.assertEqual(ser.written, b"@")

    def test_short_or_malformed_acknowledge_raises(self):
        for reply in (b"", bytes([0xBF, 0x1B]), bytes([0x00, 0x1B, 0x00, 0x00]), bytes([0xBF, 0x1B, 0x00, 0x01])):
            with self.subTest(reply=reply), self.assertRaises(uart_boot.BootError):
                uart_boot.handshake(FakeSerial(reply))


class PlanTest(unittest.TestCase):
    def stream(self):
        init = block(uart_boot.BFLAG_INIT, b"\x11" * 8)
        fill = block(uart_boot.BFLAG_FILL, count=4096)
        head = block(0, b"\x22" * 12) + fill + init
        return head, head + block(0, b"\x33" * 20) + block(uart_boot.BFLAG_FINAL, core=1)

    def test_init_block_end_skips_fill_counts(self):
        head, stream = self.stream()
        self.assertEqual(uart_boot.init_block_ends(stream), [len(head)])

    def test_rate_switches_right_after_init(self):
        head, stream = self.stream()
        stream += block(0, b"\x55" * uart_boot.BLOCK)
        segments = uart_boot.plan(stream, 57600, 260417)
        self.assertEqual([baud for _, baud in segments], [57600, 260417])
        self.assertEqual(segments[0][0], head)
        self.assertEqual(b"".join(data for data, _ in segments), uart_boot.pad(stream))

    def test_stream_without_init_is_one_segment(self):
        stream = block(0, b"\x44" * 4) + block(uart_boot.BFLAG_FINAL)
        self.assertEqual([baud for _, baud in uart_boot.plan(stream, 57600, 260417)], [57600])

    def test_rejects_non_loader_bytes(self):
        with self.assertRaises(uart_boot.BootError):
            uart_boot.init_block_ends(b"\x00" * 32)


class DrainSerial:
    """flush() blocks like tcdrain() under held RTS until the output is discarded."""

    def __init__(self, stuck):
        self.sent = threading.Event()
        if not stuck:
            self.sent.set()

    def flush(self):
        self.sent.wait()

    def reset_output_buffer(self):
        self.sent.set()


class DrainTest(unittest.TestCase):
    def test_returns_once_flush_does(self):
        uart_boot.drain(DrainSerial(stuck=False), timeout=1.0)

    def test_held_rts_raises_and_releases_flush(self):
        ser = DrainSerial(stuck=True)
        with self.assertRaises(uart_boot.BootError):
            uart_boot.drain(ser, timeout=0.05)
        self.assertTrue(ser.sent.is_set())


class PadTest(unittest.TestCase):
    def test_pads_to_whole_blocks_and_keeps_aligned_streams(self):
        for size in (1, uart_boot.BLOCK - 1, uart_boot.BLOCK, uart_boot.BLOCK + 1):
            with self.subTest(size=size):
                stream = b"\x5a" * size
                padded = uart_boot.pad(stream)
                self.assertEqual(len(padded) % uart_boot.BLOCK, 0)
                self.assertLess(len(padded) - size, uart_boot.BLOCK)
                self.assertTrue(padded.startswith(stream))
                self.assertEqual(padded[size:], bytes(len(padded) - size))


if __name__ == "__main__":
    unittest.main()
