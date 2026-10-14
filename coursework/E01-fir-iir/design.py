"""E01 low-pass filters, written to firmware/audio_loopback/dsp/lowpass_coeffs.{c,h}.

    uv run coursework/E01-fir-iir/design.py

The assignment fixes the orders and the cut-off (README.md); the rest are
choices this script makes and the report explains.
"""

from pathlib import Path

import numpy as np
from scipy import signal

from dsplab import ccode

REPO = Path(__file__).resolve().parents[2]
FIRMWARE = REPO / "firmware" / "audio_loopback"

FIR_TAPS = 32           # order 31
IIR_ORDER = 8           # four second-order sections
CUTOFF_HZ = 2000.0
FIR_WINDOW = "hamming"


def sample_rate():
    return ccode.read_define(FIRMWARE / "adc_dac_playback.h", "SAMPLE_RATE")


def fir(fs):
    """Windowed sinc, unity gain at DC; CUTOFF_HZ is its -6 dB point."""
    return signal.firwin(FIR_TAPS, CUTOFF_HZ, fs=fs, window=FIR_WINDOW)


def iir(fs):
    """Butterworth, CUTOFF_HZ at -3 dB, as sections of [b0 b1 b2 1 a1 a2].

    scipy puts the whole gain (about 5e-8 here) in the first section. Each
    section is rescaled to unity gain at DC instead, so no section's output
    sits near the bottom of the float range while the next one amplifies it.
    """
    sos = signal.butter(IIR_ORDER, CUTOFF_HZ, fs=fs, output="sos")
    for row in sos:
        row[:3] *= row[3:].sum() / row[:3].sum()
    return sos


def main():
    fs = sample_rate()
    h = fir(fs)
    sos = iir(fs)
    tables = [
        ccode.Table(
            name="LowpassFirCoeffs",
            dim="LOWPASS_FIR_TAPS",
            values=h,
            comment="FIR taps h[0] .. h[N-1]; symmetric, so linear phase with a\n"
                    "group delay of (N - 1) / 2 samples.",
            defines=[("LOWPASS_FIR_TAPS", FIR_TAPS)],
        ),
        ccode.Table(
            name="LowpassIirSos",
            dim="LOWPASS_IIR_SECTIONS * BIQUAD_COEFFS",
            # biquad.h takes b0 b1 b2 a1 a2; a0 is 1
            values=np.delete(sos, 3, axis=1),
            comment="IIR second-order sections, BIQUAD_COEFFS per section in the\n"
                    "order biquad.h expects, input section first.",
            defines=[("LOWPASS_IIR_SECTIONS", IIR_ORDER // 2)],
            per_line=5,
        ),
    ]
    ccode.write(
        FIRMWARE / "dsp" / "lowpass_coeffs",
        tables,
        brief="Low-pass coefficients for the E01 FIR and IIR comparison.",
        details="Cut-off %.0f Hz at fs = %d Hz. FIR: %d taps, %s window, -6 dB at\n"
                "the cut-off. IIR: order %d Butterworth, -3 dB at the cut-off, each\n"
                "section scaled to unity gain at DC."
                % (CUTOFF_HZ, fs, FIR_TAPS, FIR_WINDOW, IIR_ORDER),
        generator="uv run coursework/E01-fir-iir/design.py",
        includes=["dsp/biquad.h"],
    )
    print("wrote %s.{c,h}" % (FIRMWARE / "dsp" / "lowpass_coeffs").relative_to(REPO))


if __name__ == "__main__":
    main()
