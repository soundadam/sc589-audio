"""E04 figures and numbers for the report, from the design, the dsp/ code run
on the Mac, and the board log when there is one.

    uv run coursework/E04-overlap-save/design.py     # coefficients for the firmware
    make board EXPERIMENT=E04                        # optional, writes build/E04/board.log
    uv run coursework/E04-overlap-save/figures.py    # writes build/E04/

The chain is run through dsplab.cdsp, the same C the board runs, and compared
with scipy.signal.lfilter on the same coefficients (test_chain.reference).
results.tex holds the numbers the report quotes as LaTeX macros; the board's
are there only when the log is, and the report shows a placeholder otherwise.
"""

import argparse
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

import design
from dsplab import boardlog, cdsp, plot, response
from test_chain import reference

REPO = design.REPO
OUT = REPO / "build" / "E04"
BOARD_LOG = OUT / "board.log"

DESIGN_STYLE = {"color": "C0"}
LONG_STYLE = {"color": "C1"}
MEASURED_STYLE = {"color": "C3"}

# Noise through the chain for the comparison with scipy: a few hops' worth
NOISE_FRAMES = design.FACTOR * design.HOP * 24
# Filter lengths for the multiplication-count figure
LENGTHS = 2 ** np.arange(3, 13)


def _macro(name, value):
    return "\\newcommand{\\%s}{%s}\n" % (name, value)


def _db(value):
    return "\\num{%.2f}" % (round(value, 2) + 0.0)


def _sci(value):
    return "\\num{%.1e}" % value


# Real multiplications of this firmware's code, per output sample.
#
# One N-point real FFT is an M = N/2-point complex radix-2 FFT, (M/2) log2 M
# butterflies of one complex product (4 real), plus the split pass: for
# k = 1 .. M/2 a twiddle product (4) and four halvings. The inverse split has
# no halvings. One partition's spectral product is M + 1 complex products.
def fft_muls(n_fft):
    m = n_fft // 2
    return 2 * m * int(math.log2(m)) + 4 * m


def ifft_muls(n_fft):
    m = n_fft // 2
    return 2 * m * int(math.log2(m)) + 2 * m


def ols_muls_per_sample(hop, parts):
    """Overlap-save with frames of 2 hop: one FFT, one inverse and P spectral products per hop."""
    n_fft = 2 * hop
    return (fft_muls(n_fft) + ifft_muls(n_fft) + 4 * parts * (hop + 1)) / hop


def plot_efficiency(path):
    fig, ax = plt.subplots(figsize=(plot.WIDTH_IN, 2.8))
    direct = LENGTHS.astype(float)
    # plain overlap-save: the hop covers the whole filter
    plain = [ols_muls_per_sample(int(L), 1) for L in LENGTHS]
    # partitioned, hop fixed at this firmware's
    parted = [ols_muls_per_sample(design.HOP, -(-int(L) // design.HOP)) for L in LENGTHS]
    ax.loglog(LENGTHS, direct, "o-", ms=3, label="逐点 FIR", **MEASURED_STYLE)
    ax.loglog(LENGTHS, plain, "s-", ms=3, label="重叠保留，帧长 2L", **DESIGN_STYLE)
    ax.loglog(LENGTHS, parted, "^-", ms=3, label="分段重叠保留，B = %d" % design.HOP, **LONG_STYLE)
    for L in (design.FIR_ORDERS[0] + 1, design.FIR_ORDERS[1] + 1):
        ax.axvline(L, color="0.5", lw=0.8, ls=":")
    ax.set_xlabel("滤波器长度 L / 抽头")
    ax.set_ylabel("每个输出的实数乘法")
    ax.legend()
    fig.savefig(path)
    plt.close(fig)


def plot_chain(series, path):
    """Per channel: the C chain's impulse response and its difference from scipy's."""
    fig, axes = plt.subplots(len(series), 1, figsize=(plot.WIDTH_IN, 2.1 * len(series)), squeeze=False)
    for ax, (label, ref, out) in zip(axes[:, 0], series):
        n = np.arange(len(ref))
        ax.plot(n, ref, lw=1.2, label="scipy lfilter", **DESIGN_STYLE)
        ax.plot(n, out, lw=0.8, ls="--", label="C 代码（Mac）", **MEASURED_STYLE)
        ax.set_title("%s，最大差 %.1e" % (label, np.abs(out - ref).max()))
        ax.set_ylabel("$h[n]$")
        ax.legend(loc="upper right")
        # the part above -80 dB of the peak, where there is something to see
        live = np.flatnonzero(np.abs(ref) > 1e-4 * np.abs(ref).max())
        ax.set_xlim(live[0] - 50, live[-1] + 50)
    axes[-1, 0].set_xlabel("采样序号 n（%d Hz）" % design.SAMPLE_RATE)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def plot_board(run, short, long, path):
    fig, axes = plt.subplots(2, 1, figsize=(plot.WIDTH_IN, 4.2))
    for ax, (name, h, label) in zip(axes, (("ols", short, "阶数 %d，重叠保留" % design.FIR_ORDERS[0]),
                                           ("partitioned", long, "阶数 %d，分段（4 段）" % design.FIR_ORDERS[1]))):
        board = run.impulse[name]
        expect = np.concatenate([np.zeros(design.HOP), h])[:len(board)]
        n = np.arange(len(board))
        ax.plot(n, expect, lw=1.2, label="设计，延迟 %d" % design.HOP, **DESIGN_STYLE)
        ax.plot(n, board, lw=0.8, ls="--", label="板上运算", **MEASURED_STYLE)
        ax.set_title("%s，最大差 %.1e" % (label, np.abs(board - expect).max()))
        ax.set_ylabel("$h[n]$")
        ax.legend(loc="upper right")
    axes[-1].set_xlabel("采样序号 n（%d Hz）" % design.LOW_RATE)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def board_macros(run, short, long):
    s = _macro("BoardRun", "")
    s += _macro("CclkMHz", "%.0f" % (run.cclk_hz / 1e6))
    budget = run.cclk_hz / design.SAMPLE_RATE
    s += _macro("CyclesBudget", "%.0f" % budget)
    for name, h in (("ols", short), ("partitioned", long)):
        board = run.impulse[name]
        expect = np.concatenate([np.zeros(design.HOP), h])[:len(board)]
        s += _macro({"ols": "BoardOlsErr", "partitioned": "BoardPartErr"}[name], _sci(np.abs(board - expect).max()))
    c = run.cycles
    names = {"fir255": "FirShort", "ols": "OlsShort", "fir1023": "FirLong", "partitioned": "OlsLong",
             "fft512": "Fft", "ifft512": "Ifft", "rfft512": "LibFft"}
    for key, macro in names.items():
        s += _macro(macro + "Cycles", "\\num{%d}" % c[key].cycles)
        s += _macro(macro + "PerSample", "%.0f" % c[key].per_sample)
    s += _macro("ShortSpeedup", "%.1f" % (c["fir255"].cycles / c["ols"].cycles))
    s += _macro("LongSpeedup", "%.1f" % (c["fir1023"].cycles / c["partitioned"].cycles))
    s += _macro("LibFftRatio", "%.2f" % (c["fft512"].cycles / c["rfft512"].cycles))
    # cycles per real multiplication counted above, to compare how well each loop runs
    hop = design.HOP
    s += _macro("FirShortPerMul", "%.1f" % (c["fir255"].per_sample / len(short)))
    s += _macro("FirLongPerMul", "%.1f" % (c["fir1023"].per_sample / len(long)))
    s += _macro("OlsShortPerMul", "%.1f" % (c["ols"].per_sample / ols_muls_per_sample(hop, 1)))
    s += _macro("OlsLongPerMul", "%.1f" % (c["partitioned"].per_sample / ols_muls_per_sample(hop, len(long) // hop)))
    s += _macro("FftPerMul", "%.1f" % (c["fft512"].cycles / fft_muls(design.FFT_LEN)))
    whole = c["audio_process"]
    peak = c["audio_process_peak"]
    s += _macro("AudioFrames", "%d" % whole.frames)
    s += _macro("AudioCycles", "\\num{%d}" % whole.cycles)
    s += _macro("AudioPerFrame", "%.0f" % whole.per_sample)
    s += _macro("AudioLoad", "%.0f" % (100.0 * whole.per_sample / budget))
    s += _macro("PeakFrames", "%d" % peak.frames)
    s += _macro("PeakCycles", "\\num{%d}" % peak.cycles)
    s += _macro("PeakLoad", "%.0f" % (100.0 * peak.per_sample / budget))
    return s


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--log", type=Path, default=BOARD_LOG, help="console log from make board")
    args = parser.parse_args()

    fs, low = design.SAMPLE_RATE, design.LOW_RATE
    proto = design.prototype()
    short, long = (design.lowpass(n) for n in design.FIR_ORDERS)
    proto_r = response.from_ba(proto, fs=fs)
    short_r = response.from_ba(short, fs=low, n=8192)
    long_r = response.from_ba(long, fs=low, n=8192)
    OUT.mkdir(parents=True, exist_ok=True)

    plot.magnitude([plot.Series("原型（%d 抽头）" % len(proto), proto_r, style=DESIGN_STYLE)], OUT / "prototype.pdf",
                   fmax_hz=fs / 2, ylim=(-120, 5), marks_hz=[design.PASS_HZ, low / 2])
    plot.magnitude([plot.Series("%d 阶" % design.FIR_ORDERS[0], short_r, style=DESIGN_STYLE),
                    plot.Series("%d 阶" % design.FIR_ORDERS[1], long_r, style=LONG_STYLE)], OUT / "fir.pdf",
                   fmax_hz=low / 2, ylim=(-110, 5), marks_hz=[design.FIR_CUTOFF_HZ])
    plot_efficiency(OUT / "efficiency.pdf")

    # the C chain against scipy: noise for the error figures, an impulse for the plot
    rng = np.random.default_rng(4)
    x = rng.uniform(-0.25, 0.25, size=(NOISE_FRAMES, 2))
    y = cdsp.process(x, experiment=4)
    err = [np.abs(y[:, c] - reference(x[:, c], h)).max() for c, h in ((0, short), (1, long))]
    n_ir = design.FACTOR * (2 * design.HOP + len(long))
    ir = cdsp.impulse(n_ir, experiment=4)
    unit = np.zeros(n_ir)
    unit[0] = 1.0
    ir_ref = [reference(unit, h) for h in (short, long)]
    plot_chain([("声道 0：%d 阶，重叠保留" % design.FIR_ORDERS[0], ir_ref[0], ir[:, 0]),
                ("声道 1：%d 阶，分段" % design.FIR_ORDERS[1], ir_ref[1], ir[:, 1])], OUT / "chain.pdf")
    blocked = cdsp.process(x, experiment=4, block=7)

    stop = proto_r.f >= low / 2
    passband = proto_r.f <= design.PASS_HZ
    hop_ms = design.HOP / low * 1000
    s = "% Generated by coursework/E04-overlap-save/figures.py; do not edit.\n"
    s += _macro("SampleRate", "%d" % fs)
    s += _macro("LowRate", "%d" % low)
    s += _macro("Factor", "%d" % design.FACTOR)
    s += _macro("BlockFrames", "%d" % cdsp.BLOCK_FRAMES)
    s += _macro("HopBlocks", "%d" % (design.FACTOR * design.HOP // cdsp.BLOCK_FRAMES))
    s += _macro("ProtoTaps", "%d" % len(proto))
    s += _macro("PassHz", "%.0f" % design.PASS_HZ)
    s += _macro("StopHz", "%.0f" % (low / 2))
    s += _macro("StopTargetDb", "%.0f" % design.STOP_DB)
    s += _macro("StopDb", _db(proto_r.mag_db[stop].max()))
    s += _macro("PassRippleDb", "%.3f" % np.ptp(proto_r.mag_db[passband]))
    s += _macro("FirCutoffHz", "%.0f" % design.FIR_CUTOFF_HZ)
    s += _macro("ShortOrder", "%d" % design.FIR_ORDERS[0])
    s += _macro("LongOrder", "%d" % design.FIR_ORDERS[1])
    s += _macro("ShortTaps", "%d" % len(short))
    s += _macro("LongTaps", "%d" % len(long))
    s += _macro("FftLen", "%d" % design.FFT_LEN)
    s += _macro("Hop", "%d" % design.HOP)
    s += _macro("Parts", "%d" % (len(long) // design.HOP))
    s += _macro("HopMs", "%.0f" % hop_ms)
    s += _macro("ShortStopDb", _db(short_r.mag_db[short_r.f >= 2500].max()))
    s += _macro("LongStopDb", _db(long_r.mag_db[long_r.f >= 2500].max()))
    s += _macro("ShortDelayMs", "%.1f" % (hop_ms + (len(short) - 1) / 2 / low * 1000 + (len(proto) - 1) / fs * 1000))
    s += _macro("LongDelayMs", "%.1f" % (hop_ms + (len(long) - 1) / 2 / low * 1000 + (len(proto) - 1) / fs * 1000))
    s += _macro("ChainErrShort", _sci(err[0]))
    s += _macro("ChainErrLong", _sci(err[1]))
    s += _macro("BlockInvariant", "相同" if np.array_equal(y, blocked) else "不同")
    s += _macro("NoiseSeconds", "%.1f" % (NOISE_FRAMES / fs))
    # multiplication counts per output sample, at the firmware's sizes
    hop = design.HOP
    s += _macro("FftMuls", "\\num{%d}" % fft_muls(design.FFT_LEN))
    s += _macro("IfftMuls", "\\num{%d}" % ifft_muls(design.FFT_LEN))
    s += _macro("SpecMuls", "\\num{%d}" % (4 * (hop + 1)))
    s += _macro("OlsShortMuls", "%.1f" % ols_muls_per_sample(hop, 1))
    s += _macro("OlsLongMuls", "%.1f" % ols_muls_per_sample(hop, len(long) // hop))
    s += _macro("MulRatioShort", "%.1f" % (len(short) / ols_muls_per_sample(hop, 1)))
    s += _macro("MulRatioLong", "%.1f" % (len(long) / ols_muls_per_sample(hop, len(long) // hop)))
    # a single 2048-point frame for the order-1023 filter, for comparison
    s += _macro("OlsLongPlainMuls", "%.1f" % ols_muls_per_sample(len(long), 1))
    s += _macro("LongPlainMs", "%.0f" % (len(long) / low * 1000))

    try:
        run = boardlog.read(args.log)
    except FileNotFoundError:
        run = None
    if run is not None and "partitioned" in run.impulse:
        plot_board(run, short, long, OUT / "board_impulse.pdf")
        s += board_macros(run, short, long)
    else:
        run = None

    (OUT / "results.tex").write_text(s)
    print("wrote %s%s" % (OUT.relative_to(REPO), "" if run else " (no board log)"))


if __name__ == "__main__":
    main()
