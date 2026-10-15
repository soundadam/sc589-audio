"""E02 figures and numbers for the report, from the design, the dsp/ code run
on the Mac, and the board log when there is one.

    uv run coursework/E02-multirate/design.py      # coefficients for the firmware
    make board                                     # optional, writes build/E02/board.log
    uv run coursework/E02-multirate/figures.py     # writes build/E02/

The chain is measured through dsplab.cdsp, which runs the same C the board
runs on the same 24-bit samples. results.tex holds the numbers the report
quotes as LaTeX macros, so the text never repeats a value by hand; the board's
numbers are there only when the log is, and the report leaves their section
out otherwise.
"""

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

import design
from dsplab import boardlog, cdsp, plot, response

REPO = design.REPO
OUT = REPO / "build" / "E02"
BOARD_LOG = OUT / "board.log"

# Input tones for the measured chain response, across the whole codec band
SWEEP_STEP_HZ = 100.0
# Frames per tone, and the leading part left out while the filters settle
TONE_FRAMES = design.SAMPLE_RATE // 2
SETTLE_FRAMES = TONE_FRAMES // 4
TONE_LEVEL = 0.5
# Frames of the tone channel taken for its spectrum: one second, so every
# whole-hertz tone and image sits on a bin
SPECTRUM_FRAMES = design.SAMPLE_RATE

DESIGN_STYLE = {"color": "C0"}
MEASURED_STYLE = {"color": "C3"}


def _macro(name, value):
    return "\\newcommand{\\%s}{%s}\n" % (name, value)


def _db(value):
    """Gain in dB for the text: a true minus sign, and no -0.00."""
    return "\\num{%.2f}" % (round(value, 2) + 0.0)


def _curve(f, db):
    """A Response from a measured magnitude, for plot.magnitude."""
    return response.Response(np.asarray(f), 10.0 ** (np.asarray(db) / 20.0) + 0j, np.full(len(f), np.nan))


def alias_hz(f):
    """Where a tone at f lands once sampled at the low rate."""
    folded = np.mod(f, design.LOW_RATE)
    return np.where(folded > design.LOW_RATE / 2, design.LOW_RATE - folded, folded)


def computed_chain_db(f, h, sos):
    """Even-channel gain from a tone at f in to its alias out: decimator, IIR, interpolator."""
    fs = design.SAMPLE_RATE
    a = alias_hz(f)
    return (response.from_ba(h, fs=fs).at(f) + response.from_sos(sos, fs=design.LOW_RATE).at(a)
            + response.from_ba(h, fs=fs).at(a))


def measured_chain_db(freqs):
    """Even-channel gain per input tone, from the C chain."""
    t = np.arange(TONE_FRAMES) / design.SAMPLE_RATE
    gains = []
    for f in freqs:
        x = np.zeros((TONE_FRAMES, 2))
        x[:, 0] = TONE_LEVEL * np.sin(2 * np.pi * f * t)
        y = cdsp.process(x)[SETTLE_FRAMES:, 0]
        gains.append(10 * np.log10(np.mean(y ** 2) / np.mean(x[SETTLE_FRAMES:, 0] ** 2) + 1e-30))
    return np.array(gains)


def tone_spectrum():
    """Spectrum of the tone channel in dB of full scale, Blackman window."""
    y = cdsp.process(np.zeros((SETTLE_FRAMES + SPECTRUM_FRAMES, 2)))[SETTLE_FRAMES:, 1]
    w = np.blackman(len(y))
    spec = np.abs(np.fft.rfft(y * w)) / (w.sum() / 2)
    return np.fft.rfftfreq(len(y), 1 / design.SAMPLE_RATE), 20 * np.log10(spec + 1e-12)


def plot_spectrum(f, db, path):
    fig, ax = plt.subplots(figsize=(plot.WIDTH_IN, 2.8))
    ax.plot(f / 1000.0, db, lw=0.7, **MEASURED_STYLE)
    for k in range(1, int(design.SAMPLE_RATE / 2 // design.LOW_RATE) + 1):
        ax.axvline(k * design.LOW_RATE / 1000.0, color="0.5", lw=0.8, ls=":")
    ax.set_ylim(-160, 0)
    ax.set_xlim(0, design.SAMPLE_RATE / 2000.0)
    ax.set_xlabel("频率 / kHz")
    ax.set_ylabel("幅度 / dBFS")
    fig.savefig(path)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--log", type=Path, default=BOARD_LOG,
                        help="console log from make board")
    args = parser.parse_args()

    fs = design.SAMPLE_RATE
    h = design.prototype()
    sos = design.iir()
    proto = response.from_ba(h, fs=fs)
    low_iir = response.from_sos(sos, fs=design.LOW_RATE)
    stop = proto.f >= design.LOW_RATE / 2
    passband = proto.f <= design.PASS_HZ

    OUT.mkdir(parents=True, exist_ok=True)

    plot.magnitude([plot.Series("原型（%d 抽头）" % len(h), proto, style=DESIGN_STYLE)], OUT / "prototype.pdf",
                   fmax_hz=fs / 2, ylim=(-120, 5), marks_hz=[design.PASS_HZ, design.LOW_RATE / 2])
    plot.magnitude([plot.Series("IIR（%d 阶，fs = %d Hz）" % (design.IIR_ORDER, design.LOW_RATE), low_iir, style=DESIGN_STYLE)], OUT / "iir.pdf",
                   fmax_hz=design.LOW_RATE / 2, ylim=(-60, 5), marks_hz=[design.IIR_CUTOFF_HZ])

    freqs = np.arange(SWEEP_STEP_HZ / 2, fs / 2, SWEEP_STEP_HZ)
    measured = measured_chain_db(freqs)
    computed = computed_chain_db(freqs, h, sos)
    plot.magnitude([
        plot.Series("由系数计算", _curve(freqs, computed), style=DESIGN_STYLE),
        plot.Series("C 代码实测", _curve(freqs, measured), style={**MEASURED_STYLE, "ls": "--"}),
    ], OUT / "chain.pdf", fmax_hz=fs / 2, ylim=(-140, 5),
        marks_hz=[k * design.LOW_RATE / 2 for k in range(1, 6)])

    f, spec = tone_spectrum()
    plot_spectrum(f, spec, OUT / "tone.pdf")
    near_tone = np.abs(f - design.TONE_HZ) < SWEEP_STEP_HZ
    images = np.abs(alias_hz(f) - design.TONE_HZ) < SWEEP_STEP_HZ
    images &= ~near_tone

    s = "% Generated by coursework/E02-multirate/figures.py; do not edit.\n"
    s += _macro("SampleRate", "%d" % fs)
    s += _macro("LowRate", "%d" % design.LOW_RATE)
    s += _macro("Factor", "%d" % design.FACTOR)
    s += _macro("BlockFrames", "%d" % cdsp.BLOCK_FRAMES)
    s += _macro("LowPerBlockMin", "%d" % (cdsp.BLOCK_FRAMES // design.FACTOR))
    s += _macro("LowPerBlockMax", "%d" % -(-cdsp.BLOCK_FRAMES // design.FACTOR))
    s += _macro("Taps", "%d" % len(h))
    s += _macro("BranchTaps", "%d" % (len(h) // design.FACTOR))
    s += _macro("PassHz", "%.0f" % design.PASS_HZ)
    s += _macro("StopHz", "%.0f" % (design.LOW_RATE / 2))
    s += _macro("StopTargetDb", "%.0f" % design.STOP_DB)
    s += _macro("StopDb", _db(proto.mag_db[stop].max()))
    s += _macro("PassRippleDb", "%.3f" % np.ptp(proto.mag_db[passband]))
    s += _macro("ProtoDelay", "%.1f" % ((len(h) - 1) / 2))
    s += _macro("ChainDelayMs", "%.2f" % ((len(h) - 1) / fs * 1000))
    s += _macro("IirOrder", "%d" % design.IIR_ORDER)
    s += _macro("IirSections", "%d" % len(sos))
    s += _macro("IirCutoffHz", "%.0f" % design.IIR_CUTOFF_HZ)
    s += _macro("ToneHz", "%.0f" % design.TONE_HZ)
    s += _macro("ToneLevel", "%.2f" % design.TONE_LEVEL)
    s += _macro("ToneLen", "%d" % len(design.tone()))
    s += _macro("ToneDb", _db(spec[near_tone].max()))
    s += _macro("ToneImageDb", _db(spec[images].max()))
    s += _macro("SweepStepHz", "%.0f" % SWEEP_STEP_HZ)
    s += _macro("SweepMaxErrDb", "%.3f" % np.abs(measured - computed)[computed > -60].max())
    s += _macro("AliasWorstDb", _db(measured[freqs > design.LOW_RATE / 2].max()))
    s += _macro("DecMuls", "%d" % len(h))
    s += _macro("DecMulsPerIn", "%d" % (len(h) // design.FACTOR))
    s += _macro("IntMuls", "%d" % (len(h) // design.FACTOR + 1))
    s += _macro("IirMuls", "%d" % (5 * len(sos)))

    try:
        run = boardlog.read(args.log)
    except FileNotFoundError:
        run = None
    if run is not None and "chain" in run.impulse:
        mac = cdsp.impulse(len(run.impulse["chain"]))[:, 0]
        plot.impulse([plot.Series("声道 0（降采样、IIR、升采样）", impulse=mac, measured=run.impulse["chain"])],
                      OUT / "board_impulse.pdf", reference="Mac 上的 C 代码")
        budget = run.cclk_hz / fs
        whole = run.cycles["audio_process"]
        s += _macro("BoardRun", "")
        s += _macro("ChainIrLen", "%d" % len(mac))
        s += _macro("ChainIrErr", "\\num{%.1e}" % np.abs(run.impulse["chain"] - mac).max())
        s += _macro("CclkMHz", "%.0f" % (run.cclk_hz / 1e6))
        s += _macro("CyclesBudget", "%.0f" % budget)
        s += _macro("DecCycles", "%d" % run.cycles["decimator"].cycles)
        s += _macro("IirCycles", "%d" % run.cycles["iir"].cycles)
        s += _macro("IirFrames", "%d" % run.cycles["iir"].frames)
        s += _macro("IntCycles", "%d" % run.cycles["interpolator"].cycles)
        s += _macro("AudioCycles", "%d" % whole.cycles)
        s += _macro("AudioPerFrame", "%.0f" % whole.per_sample)
        s += _macro("AudioLoad", "%.0f" % (100.0 * whole.per_sample / budget))
        # the phase-0 block the report times carries the most low-rate samples
        dec_macs = (-(-cdsp.BLOCK_FRAMES // design.FACTOR)) * len(h)
        int_macs = cdsp.BLOCK_FRAMES * (len(h) // design.FACTOR)
        s += _macro("DecCyclesPerMac", "%.0f" % (run.cycles["decimator"].cycles / dec_macs))
        s += _macro("IntCyclesPerMac", "%.0f" % (run.cycles["interpolator"].cycles / int_macs))

    (OUT / "results.tex").write_text(s)
    print("wrote %s%s" % (OUT.relative_to(REPO), "" if run else " (no board log)"))


if __name__ == "__main__":
    main()
