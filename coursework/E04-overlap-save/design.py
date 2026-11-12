"""E04 rate-change filter and the two FIR filters at 16 kHz, written to
firmware/audio_loopback/dsp/ols_coeffs.{c,h}.

    uv run coursework/E04-overlap-save/design.py

The assignment fixes the processing rate (16 kHz), the FIR order (255) and the
partitioned filter's order (1023, as four partitions of 256); the rest are
choices this script makes and the report explains.
"""

from pathlib import Path

from scipy import signal

from dsplab import ccode

REPO = Path(__file__).resolve().parents[2]
FIRMWARE = REPO / "firmware" / "audio_loopback"

# The codecs' rate; written into the header as OLS_SAMPLE_RATE, which
# filter_report.c checks against the firmware's SAMPLE_RATE at compile time
SAMPLE_RATE = 48000
LOW_RATE = 16000
FACTOR = SAMPLE_RATE // LOW_RATE

# One prototype serves the 3:1 decimator and the 1:3 interpolator, as in E02.
# Everything below PASS_HZ is kept, and from LOW_RATE / 2 up, where the 16 kHz
# stream aliases, STOP_DB is the target.
PASS_HZ = 6000.0
STOP_DB = 60.0

# Overlap-save: frames of FFT_LEN samples hop by HOP = FFT_LEN / 2, so one
# B-tap partition fits each frame without its circular convolution wrapping
# into the kept half.
FFT_LEN = 512
HOP = FFT_LEN // 2

# The two filters at LOW_RATE: order 255 (one partition) and order 1023 (four
# partitions), the same Hamming-windowed low-pass so that their transition
# bands compare.
FIR_CUTOFF_HZ = 2000.0
FIR_ORDERS = (255, 1023)


def prototype():
    """Kaiser-windowed low-pass at SAMPLE_RATE, rounded up to a multiple of FACTOR taps."""
    stop_hz = LOW_RATE / 2.0
    taps, beta = signal.kaiserord(STOP_DB, (stop_hz - PASS_HZ) / (SAMPLE_RATE / 2.0))
    taps = -(-taps // FACTOR) * FACTOR
    return signal.firwin(taps, (PASS_HZ + stop_hz) / 2.0, fs=SAMPLE_RATE, window=("kaiser", beta))


def lowpass(order):
    """Hamming-windowed low-pass of the given order at LOW_RATE, -6 dB at FIR_CUTOFF_HZ."""
    return signal.firwin(order + 1, FIR_CUTOFF_HZ, fs=LOW_RATE, window="hamming")


def main():
    h = prototype()
    short, long = (lowpass(n) for n in FIR_ORDERS)
    tables = [
        ccode.Table(
            name="Rate16kTaps",
            dim="OLS_RATE_TAPS",
            values=h,
            comment="Low-pass prototype h[0] .. h[N-1] at the codec rate, unity gain\n"
                    "at DC; the decimator applies it as is, the interpolator scaled\n"
                    "by OLS_FACTOR.",
            defines=[("OLS_SAMPLE_RATE", SAMPLE_RATE), ("OLS_FACTOR", FACTOR),
                     ("OLS_RATE_TAPS", len(h))],
        ),
        ccode.Table(
            name="Fir255Taps",
            dim="OLS_FIR255_TAPS",
            values=short,
            comment="Order-255 low-pass at the low rate, h[0] .. h[255]: one\n"
                    "partition of OLS_HOP taps. Overlap-save frames are OLS_FFT_LEN\n"
                    "samples and hop by OLS_HOP.",
            defines=[("OLS_FFT_LEN", FFT_LEN), ("OLS_HOP", HOP), ("OLS_FIR255_TAPS", len(short))],
        ),
        ccode.Table(
            name="Fir1023Taps",
            dim="OLS_FIR1023_TAPS",
            values=long,
            comment="Order-1023 low-pass at the low rate, h[0] .. h[1023], for the\n"
                    "partitioned filter: OLS_FIR1023_PARTS partitions of OLS_HOP taps.",
            defines=[("OLS_FIR1023_TAPS", len(long)), ("OLS_FIR1023_PARTS", len(long) // HOP)],
        ),
    ]
    ccode.write(
        FIRMWARE / "dsp" / "ols_coeffs",
        tables,
        brief="Rate-change filter and the overlap-save FIR filters for E04.",
        details="fs = %d Hz, low rate %d Hz (factor %d). Prototype: %d-tap\n"
                "Kaiser-windowed low-pass, pass band to %.0f Hz, %.0f dB target from\n"
                "%.0f Hz. FIR filters: Hamming-windowed low-passes of order %d and %d\n"
                "at the low rate, -6 dB at %.0f Hz. Overlap-save: %d-point FFT, hop %d."
                % (SAMPLE_RATE, LOW_RATE, FACTOR, len(h), PASS_HZ, STOP_DB, LOW_RATE / 2.0,
                   FIR_ORDERS[0], FIR_ORDERS[1], FIR_CUTOFF_HZ, FFT_LEN, HOP),
        generator="uv run coursework/E04-overlap-save/design.py",
    )
    print("wrote %s.{c,h}" % (FIRMWARE / "dsp" / "ols_coeffs").relative_to(REPO))


if __name__ == "__main__":
    main()
