"""E02 rate-change filter, 8 kHz low-pass and test tone, written to
firmware/audio_loopback/dsp/multirate_coeffs.{c,h}.

    uv run coursework/E02-multirate/design.py

The assignment fixes the rates, the 2 kHz IIR cut-off and the block diagram
(README.md); the rest are choices this script makes and the report explains.
"""

from math import gcd
from pathlib import Path

import numpy as np
from scipy import signal

from dsplab import ccode

REPO = Path(__file__).resolve().parents[2]
FIRMWARE = REPO / "firmware" / "audio_loopback"

# The codecs' rate; written into the header as MULTIRATE_SAMPLE_RATE, which
# filter_report.c checks against the firmware's SAMPLE_RATE at compile time
SAMPLE_RATE = 48000
LOW_RATE = 8000
FACTOR = SAMPLE_RATE // LOW_RATE

# One prototype serves both the 6:1 decimator (anti-alias) and the 1:6
# interpolator (anti-image). Everything below PASS_HZ is kept, and from
# LOW_RATE / 2 up, where the 8 kHz stream aliases, STOP_DB is the target.
PASS_HZ = 3000.0
STOP_DB = 60.0

IIR_ORDER = 4           # two second-order sections
IIR_CUTOFF_HZ = 2000.0

TONE_HZ = 1000.0
TONE_LEVEL = 0.25       # of full scale


def prototype():
    """Kaiser-windowed low-pass at LOW_RATE, rounded up to a multiple of FACTOR taps.

    The cut-off sits midway between PASS_HZ and LOW_RATE / 2, the band the
    Kaiser formula sizes the filter for. A multiple of FACTOR taps gives every
    interpolator branch the same length.
    """
    stop_hz = LOW_RATE / 2.0
    taps, beta = signal.kaiserord(STOP_DB, (stop_hz - PASS_HZ) / (SAMPLE_RATE / 2.0))
    taps = -(-taps // FACTOR) * FACTOR
    return signal.firwin(taps, (PASS_HZ + stop_hz) / 2.0, fs=SAMPLE_RATE, window=("kaiser", beta))


def iir():
    """Butterworth at LOW_RATE, IIR_CUTOFF_HZ at -3 dB, as sections of [b0 b1 b2 1 a1 a2]."""
    return signal.butter(IIR_ORDER, IIR_CUTOFF_HZ, fs=LOW_RATE, output="sos")


def tone():
    """One period of the tone at LOW_RATE, so the table repeats without a seam."""
    n = LOW_RATE // gcd(LOW_RATE, int(TONE_HZ))
    return TONE_LEVEL * np.sin(2.0 * np.pi * TONE_HZ * np.arange(n) / LOW_RATE)


def main():
    h = prototype()
    sos = iir()
    t = tone()
    tables = [
        ccode.Table(
            name="MultirateTaps",
            dim="MULTIRATE_TAPS",
            values=h,
            comment="Low-pass prototype h[0] .. h[N-1] at the codec rate, unity gain\n"
                    "at DC; the decimator applies it as is, the interpolator scaled\n"
                    "by MULTIRATE_FACTOR.",
            defines=[("MULTIRATE_SAMPLE_RATE", SAMPLE_RATE), ("MULTIRATE_FACTOR", FACTOR),
                     ("MULTIRATE_TAPS", len(h))],
        ),
        ccode.Table(
            name="Rate8kIirSos",
            dim="RATE8K_IIR_SECTIONS * BIQUAD_COEFFS",
            # biquad.h takes b0 b1 b2 a1 a2; a0 is 1
            values=np.delete(sos, 3, axis=1),
            comment="IIR low-pass at the low rate, second-order sections with\n"
                    "BIQUAD_COEFFS each in the order biquad.h expects.",
            defines=[("RATE8K_IIR_SECTIONS", len(sos))],
            per_line=5,
        ),
        ccode.Table(
            name="Rate8kTone",
            dim="RATE8K_TONE_LEN",
            values=t,
            comment="One period of the test tone at the low rate.",
            defines=[("RATE8K_TONE_HZ", int(TONE_HZ)), ("RATE8K_TONE_LEN", len(t))],
        ),
    ]
    ccode.write(
        FIRMWARE / "dsp" / "multirate_coeffs",
        tables,
        brief="Rate-change filter, low-rate IIR and test tone for E02.",
        details="fs = %d Hz, low rate %d Hz (factor %d). Prototype: %d-tap\n"
                "Kaiser-windowed low-pass, pass band to %.0f Hz, %.0f dB target from\n"
                "%.0f Hz. IIR: order %d Butterworth, -3 dB at %.0f Hz at the low rate.\n"
                "Tone: %.0f Hz at %.2f of full scale."
                % (SAMPLE_RATE, LOW_RATE, FACTOR, len(h), PASS_HZ, STOP_DB, LOW_RATE / 2.0,
                   IIR_ORDER, IIR_CUTOFF_HZ, TONE_HZ, TONE_LEVEL),
        generator="uv run coursework/E02-multirate/design.py",
        includes=["dsp/biquad.h"],
    )
    print("wrote %s.{c,h}" % (FIRMWARE / "dsp" / "multirate_coeffs").relative_to(REPO))


if __name__ == "__main__":
    main()
