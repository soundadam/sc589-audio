"""E04's chain on the Mac against scipy.signal.lfilter.

    uv run python -m unittest discover -s coursework/E04-overlap-save

AudioProcess() runs the C in firmware/audio_loopback/dsp/ (dsplab.cdsp); the
reference is the same coefficients in double precision through lfilter: the
3:1 decimator, the FIR one hop (OLS_HOP samples) late, then zero-stuffing and
the interpolation filter. The only differences left are float rounding and
the 24-bit quantisation at both ends.
"""

import sys
import unittest
from pathlib import Path

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))

import design  # noqa: E402
from dsplab import cdsp  # noqa: E402

# Frames run through the chain: a few seconds' worth of hops
FRAMES = 3 * design.HOP * 24
# Largest allowed difference against the reference, in full scale; one
# 24-bit step is 1.2e-7
TOL = 2e-6


def reference(x, h):
    """x at the codec rate through the chain with h as its 16 kHz filter."""
    proto = design.prototype()
    low = signal.lfilter(proto, 1.0, x)[::design.FACTOR]
    low = signal.lfilter(h, 1.0, low)
    low = np.concatenate([np.zeros(design.HOP), low[:-design.HOP]])
    up = np.zeros(len(x))
    up[::design.FACTOR] = low[:len(up[::design.FACTOR])]
    return signal.lfilter(design.FACTOR * proto, 1.0, up)


class Chain(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rng = np.random.default_rng(4)
        cls.x = rng.uniform(-0.25, 0.25, size=(FRAMES, 4))
        cls.short, cls.long = (design.lowpass(n) for n in design.FIR_ORDERS)

    def check(self, y, channel, h):
        ref = reference(self.x[:, channel], h)
        err = np.abs(y[:, channel] - ref).max()
        self.assertLess(err, TOL, "channel %d off by %.3g" % (channel, err))

    def test_even_channels_overlap_save(self):
        y = cdsp.process(self.x, experiment=4)
        for channel in (0, 2):
            self.check(y, channel, self.short)

    def test_odd_channels_partitioned(self):
        y = cdsp.process(self.x, experiment=4)
        for channel in (1, 3):
            self.check(y, channel, self.long)

    def test_block_size_does_not_matter(self):
        a = cdsp.process(self.x, experiment=4)
        b = cdsp.process(self.x, experiment=4, block=7)
        self.assertEqual(np.abs(a - b).max(), 0.0)

    def test_impulse_delay(self):
        """The chain's impulse response peaks where the filters' centres add up."""
        ir = cdsp.impulse(3 * (design.HOP + len(self.long)), experiment=4)
        proto = design.prototype()
        centre = (len(proto) - 1) // 2 * 2
        for channel, h in ((0, self.short), (1, self.long)):
            expect = centre + design.FACTOR * (design.HOP + (len(h) - 1) / 2)
            self.assertLessEqual(abs(np.argmax(ir[:, channel]) - expect), design.FACTOR,
                                 "channel %d peaks at %d, expected %.1f"
                                 % (channel, np.argmax(ir[:, channel]), expect))


if __name__ == "__main__":
    unittest.main()
