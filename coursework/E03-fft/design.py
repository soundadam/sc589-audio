"""E03 FFT tables, written to firmware/audio_loopback/dsp/fft_tables.{c,h}.

    uv run coursework/E03-fft/design.py

The assignment fixes N = 32 and asks for the twiddle factors and other
constants to be set up ahead of time (README.md). Everything the transform
looks up is computed here once and compiled into the firmware as const arrays:
the twiddle factors, the bit-reversal permutations for N and N/2 points, and
the per-bin gains of the frequency-domain demonstration on odd channels.
"""

from pathlib import Path

import numpy as np

from dsplab import ccode

REPO = Path(__file__).resolve().parents[2]
FIRMWARE = REPO / "firmware" / "audio_loopback"

SAMPLE_RATE = 48000
N = 32
LOG2N = int(np.log2(N))
assert 1 << LOG2N == N

# Odd channels keep bins up to KEEP_HZ and zero the rest: an ideal low-pass on
# each 32-sample frame, to show a frequency-domain operation and what framing
# without overlap does to it
KEEP_HZ = 3000.0


def bin_hz():
    return SAMPLE_RATE / N


def twiddles():
    """W_N^k = exp(-j 2 pi k / N) for k = 0 .. N/2 - 1, as (cos, sin): W = cos - j sin."""
    k = np.arange(N // 2)
    w = np.exp(-2j * np.pi * k / N)
    # cos(pi/2) comes out as 6e-17: snap such round-off to the exact zero
    w = np.where(np.abs(w.real) < 1e-12, 0, w.real) + 1j * np.where(np.abs(w.imag) < 1e-12, 0, w.imag)
    return w.real, -w.imag


def bit_reverse(bits):
    """Index n with its `bits` low bits reversed, for n = 0 .. 2^bits - 1."""
    n = np.arange(1 << bits)
    r = np.zeros_like(n)
    for b in range(bits):
        r |= ((n >> b) & 1) << (bits - 1 - b)
    return r


def bin_gain():
    """Gain per bin k = 0 .. N/2 for the odd channels."""
    k = np.arange(N // 2 + 1)
    return (k * bin_hz() <= KEEP_HZ).astype(float)


def main():
    cos, sin = twiddles()
    tables = [
        ccode.Table(
            name="FftCos",
            dim="FFT_N / 2u",
            values=cos,
            comment="Real part of the twiddle factor W_N^k = exp(-j 2 pi k / N),\n"
                    "k = 0 .. N/2 - 1. A transform of N/2 points takes every\n"
                    "other entry.",
            defines=[("FFT_SAMPLE_RATE", SAMPLE_RATE), ("FFT_N", N), ("FFT_LOG2N", LOG2N)],
        ),
        ccode.Table(
            name="FftSin",
            dim="FFT_N / 2u",
            values=sin,
            comment="sin(2 pi k / N), k = 0 .. N/2 - 1: W_N^k = FftCos[k] - j FftSin[k].",
        ),
        ccode.Table(
            name="FftBitRev",
            dim="FFT_N",
            values=bit_reverse(LOG2N),
            comment="n with its FFT_LOG2N bits reversed: where input n goes before\n"
                    "the in-place butterflies of an N-point transform.",
            ctype="uint16_t",
            per_line=8,
        ),
        ccode.Table(
            name="FftBitRevHalf",
            dim="FFT_N / 2u",
            values=bit_reverse(LOG2N - 1),
            comment="The same for N/2 points, which the real-input transform uses.",
            ctype="uint16_t",
            per_line=8,
        ),
        ccode.Table(
            name="FftBinGain",
            dim="FFT_N / 2u + 1u",
            values=bin_gain(),
            comment="Gain per bin k = 0 .. N/2 applied on odd channels: 1 up to\n"
                    "FFT_KEEP_HZ, 0 above.",
            defines=[("FFT_KEEP_HZ", int(KEEP_HZ))],
            per_line=6,
        ),
    ]
    ccode.write(
        FIRMWARE / "dsp" / "fft_tables",
        tables,
        brief="Twiddle factors, bit-reversal tables and bin gains for E03.",
        details="N = %d points (%d stages) at fs = %d Hz, %.0f Hz per bin. Odd\n"
                "channels keep bins up to %.0f Hz."
                % (N, LOG2N, SAMPLE_RATE, bin_hz(), KEEP_HZ),
        generator="uv run coursework/E03-fft/design.py",
    )
    print("wrote %s.{c,h}" % (FIRMWARE / "dsp" / "fft_tables").relative_to(REPO))


if __name__ == "__main__":
    main()
