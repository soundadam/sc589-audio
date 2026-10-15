"""Frequency response of a filter, from its coefficients or its impulse response."""

import warnings
from dataclasses import dataclass

import numpy as np
from scipy import signal

# Below this |H| the phase is undefined and the group delay of a zero on the
# unit circle is a spike, so the group delay is masked there
GD_FLOOR = 1e-3


@dataclass
class Response:
    f: np.ndarray       # Hz
    h: np.ndarray       # complex response at f
    gd: np.ndarray      # group delay in samples, NaN where |h| < GD_FLOOR

    @property
    def mag_db(self):
        return 20.0 * np.log10(np.maximum(np.abs(self.h), 1e-12))

    @property
    def phase_deg(self):
        return np.degrees(np.unwrap(np.angle(self.h)))

    def at(self, f_hz):
        """Gain in dB at f_hz, interpolated."""
        return np.interp(f_hz, self.f, self.mag_db)


def _grid(fs, n):
    return np.linspace(0.0, fs / 2.0, n, endpoint=False)


def _group_delay(b, a, f, fs, h):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        _, gd = signal.group_delay((b, a), w=f, fs=fs)
    return np.where(np.abs(h) < GD_FLOOR, np.nan, gd)


def from_ba(b, a=1.0, *, fs, n=4096):
    f = _grid(fs, n)
    _, h = signal.freqz(b, a, worN=f, fs=fs)
    return Response(f, h, _group_delay(b, np.atleast_1d(a), f, fs, h))


def from_sos(sos, *, fs, n=4096):
    f = _grid(fs, n)
    _, h = signal.freqz_sos(sos, worN=f, fs=fs)
    b, a = signal.sos2tf(sos)
    return Response(f, h, _group_delay(b, a, f, fs, h))


def from_impulse(ir, *, fs, n=4096):
    """Response of a measured impulse response, taken as an FIR of its length."""
    return from_ba(np.asarray(ir, dtype=float), 1.0, fs=fs, n=n)


def sos_impulse(sos, n):
    """First n samples of the impulse response of a second-order-section cascade."""
    x = np.zeros(n)
    x[0] = 1.0
    return signal.sosfilt(sos, x)
