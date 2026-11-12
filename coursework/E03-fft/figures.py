"""E03 figures and numbers for the report, from the dsp/ code run on the Mac
and the board log when there is one.

    uv run coursework/E03-fft/design.py      # tables for the firmware
    make board                               # optional, writes build/E03/board.log
    uv run coursework/E03-fft/figures.py     # writes build/E03/

The transforms in dsp/fft.c are called directly through dsplab.cdsp's
library, and AudioProcess() as the board runs it; numpy's double-precision FFT
is the reference. results.tex holds the numbers the report quotes as LaTeX
macros; the board's numbers (the comparison with the CCES library, and cycle
counts) are there only when the log is, and the report leaves their section
out otherwise.
"""

import argparse
import ctypes
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

import design
from dsplab import boardlog, cdsp, plot

REPO = design.REPO
OUT = REPO / "build" / "E03"
BOARD_LOG = OUT / "board.log"
FFT_C = REPO / "firmware" / "audio_loopback" / "dsp" / "fft.c"

N = design.N
M = N // 2
FS = design.SAMPLE_RATE

# Random frames for the error statistics
TRIALS = 2000
# Noise through AudioProcess(): frames, and its level against full scale
NOISE_FRAMES = 48000
NOISE_LEVEL = 0.9
# Two tones for the frequency-domain demonstration: one kept, one removed
DEMO_KEEP_HZ = 1000.0
DEMO_CUT_HZ = 7000.0
DEMO_LEVEL = 0.4
DEMO_FRAMES = FS
# Impulse responses printed
IR_LEN = 4 * N

OWN_STYLE = {"color": "C3"}
REF_STYLE = {"color": "C0"}

_c = np.ctypeslib.ndpointer(np.complex64, flags="C_CONTIGUOUS")
_f = np.ctypeslib.ndpointer(np.float32, flags="C_CONTIGUOUS")


def _macro(name, value):
    return "\\newcommand{\\%s}{%s}\n" % (name, value)


def _sci(value):
    return "\\num{%.1e}" % value


class Fft:
    """The four transforms of dsp/fft.c; a Cplx is two floats, as complex64."""

    def __init__(self):
        lib = cdsp.library()
        for name, args in [("FftForward", [_c, _c]), ("FftInverse", [_c, _c]),
                           ("FftRealForward", [_f, _c]), ("FftRealInverse", [_c, _f])]:
            getattr(lib, name).argtypes = args
            getattr(lib, name).restype = None
        self.lib = lib

    def forward(self, x):
        y = np.empty(N, np.complex64)
        self.lib.FftForward(np.ascontiguousarray(x, np.complex64), y)
        return y

    def inverse(self, X):
        y = np.empty(N, np.complex64)
        self.lib.FftInverse(np.ascontiguousarray(X, np.complex64), y)
        return y

    def real_forward(self, x):
        y = np.empty(M + 1, np.complex64)
        self.lib.FftRealForward(np.ascontiguousarray(x, np.float32), y)
        return y

    def real_inverse(self, X):
        y = np.empty(N, np.float32)
        self.lib.FftRealInverse(np.ascontiguousarray(X, np.complex64), y)
        return y


def errors(fft, rng):
    """Largest error of each transform against numpy over TRIALS random frames, |x| < 1."""
    worst = dict.fromkeys(["cfft", "ifft", "cround", "rfft", "rifft", "rround"], 0.0)
    for _ in range(TRIALS):
        z = (rng.uniform(-1, 1, N) + 1j * rng.uniform(-1, 1, N)).astype(np.complex64)
        x = rng.uniform(-1, 1, N).astype(np.float32)
        Z = np.fft.fft(z.astype(complex))
        X = np.fft.rfft(x.astype(float))
        worst["cfft"] = max(worst["cfft"], np.abs(fft.forward(z) - Z).max())
        worst["ifft"] = max(worst["ifft"], np.abs(fft.inverse(Z.astype(np.complex64)) - np.fft.ifft(Z.astype(np.complex64).astype(complex))).max())
        worst["cround"] = max(worst["cround"], np.abs(fft.inverse(fft.forward(z)) - z).max())
        worst["rfft"] = max(worst["rfft"], np.abs(fft.real_forward(x) - X).max())
        Xs = X.astype(np.complex64)
        worst["rifft"] = max(worst["rifft"], np.abs(fft.real_inverse(Xs) - np.fft.irfft(Xs.astype(complex), N)).max())
        worst["rround"] = max(worst["rround"], np.abs(fft.real_inverse(fft.real_forward(x)) - x).max())
    return worst


def plot_spectrum(x, own, path):
    """|X[k]| of a test frame: numpy against dsp/fft.c, and their difference."""
    ref = np.fft.rfft(x.astype(float))
    k = np.arange(M + 1)
    fig, (top, bottom) = plt.subplots(2, 1, figsize=(plot.WIDTH_IN, 3.6), sharex=True,
                                      gridspec_kw={"height_ratios": [2, 1]})
    top.stem(k, np.abs(ref), linefmt="C0-", markerfmt="C0o", basefmt=" ", label="numpy（双精度）")
    top.plot(k, np.abs(own), "x", ms=5, mew=0.9, label="FftRealForward", **OWN_STYLE)
    top.set_ylabel("$|X[k]|$")
    top.legend()
    bottom.stem(k, np.abs(own - ref), linefmt="C3-", markerfmt="C3.", basefmt=" ")
    bottom.set_yscale("log")
    bottom.set_ylabel("误差")
    bottom.set_xlabel("频点 k（每点 %.0f Hz）" % design.bin_hz())
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def plot_loopback(x, y, path):
    """Even channel: input against output 32 samples later, and the difference in LSB."""
    n = np.arange(4 * N)
    fig, (top, bottom) = plt.subplots(2, 1, figsize=(plot.WIDTH_IN, 3.6), sharex=True,
                                      gridspec_kw={"height_ratios": [2, 1]})
    top.plot(n, x[n], label="输入 x[n]", **REF_STYLE)
    top.plot(n, y[n + N], "--", label="输出 y[n+%d]" % N, **OWN_STYLE)
    for b in range(0, 4 * N + 1, N):
        top.axvline(b, color="0.5", lw=0.6, ls=":")
        bottom.axvline(b, color="0.5", lw=0.6, ls=":")
    top.set_ylabel("幅度")
    top.legend(loc="upper right")
    bottom.plot(n, (y[n + N] - x[n]) * cdsp.S24_SCALE, ".", ms=3, **OWN_STYLE)
    bottom.set_ylabel("差 / LSB")
    bottom.set_xlabel("采样序号 n")
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def plot_demo(x, y, path):
    """Spectra of the odd channel's input and output, Blackman window."""
    w = np.blackman(len(x))
    f = np.fft.rfftfreq(len(x), 1 / FS)

    def db(v):
        return 20 * np.log10(np.abs(np.fft.rfft(v * w)) / (w.sum() / 2) + 1e-12)

    fig, ax = plt.subplots(figsize=(plot.WIDTH_IN, 2.8))
    ax.plot(f / 1000.0, db(x), lw=0.8, label="输入", **REF_STYLE)
    ax.plot(f / 1000.0, db(y), lw=0.8, label="输出", **OWN_STYLE)
    ax.axvline(design.KEEP_HZ / 1000.0, color="0.5", lw=0.8, ls=":")
    ax.set_xlim(0, FS / 2000.0)
    ax.set_ylim(-140, 0)
    ax.set_xlabel("频率 / kHz")
    ax.set_ylabel("幅度 / dBFS")
    ax.legend()
    fig.savefig(path)
    plt.close(fig)
    return f, db(x), db(y)


def lines_of(path, signature):
    """First and last line of the C function whose definition starts with `signature`."""
    text = path.read_text().splitlines()
    first = next(i for i, l in enumerate(text) if l.startswith(signature)) + 1
    last = next(i for i in range(first, len(text)) if text[i] == "}") + 1
    return first, last


def fit_scale(got, ref):
    """Real gain g minimising |got - g ref|, and the largest error left after it."""
    g = np.real(np.vdot(ref, got)) / np.real(np.vdot(ref, ref))
    return g, np.abs(got - g * ref).max()


def _upper_bins(lib):
    """What rfft32 leaves in bins N/2+1 .. N-1, for the text."""
    upper = lib[M + 1:]
    if np.abs(upper).max() < 1e-6:
        return "只写 $0$--$N/2$，其余为零"
    if np.abs(upper - np.conj(lib[1:M][::-1])).max() < 1e-3 * np.abs(lib).max():
        return "填满全部 %d 个频点，$X[N-k]=X[k]^*$" % N
    return "$N/2$ 以上的频点不是 $X[k]$ 的共轭，不可用"


def board_macros(run, fft):
    s = _macro("BoardRun", "")
    v = run.vectors
    x = v["re_in"].real.astype(np.float32)
    z = (v["re_in"].real + 1j * v["im_in"].real).astype(np.complex64)
    X = np.fft.fft(x.astype(float))
    Z = np.fft.fft(z.astype(complex))

    # own code: the board against numpy, and against the same C on the Mac
    s += _macro("BoardRfftErr", _sci(np.abs(v["rfft_own"] - X[:M + 1]).max()))
    s += _macro("BoardCfftErr", _sci(np.abs(v["cfft_own"] - Z).max()))
    s += _macro("BoardIfftErr", _sci(np.abs(v["ifft_own"] - z).max()))
    s += _macro("BoardRifftErr", _sci(np.abs(v["rifft_own"].real - x).max()))
    s += _macro("BoardMacDiff", _sci(max(np.abs(v["rfft_own"] - fft.real_forward(x)).max(),
                                         np.abs(v["cfft_own"] - fft.forward(z)).max())))

    # the library: its scaling read off the vectors rather than assumed
    g, err = fit_scale(v["cfft_lib"], Z)
    s += _macro("LibCfftGain", "%.4g" % g)
    s += _macro("LibCfftErr", _sci(err))
    g, err = fit_scale(v["rfft_lib"][:M + 1], X[:M + 1])
    s += _macro("LibRfftGain", "%.4g" % g)
    s += _macro("LibRfftErr", _sci(err))
    s += _macro("LibRfftUpper", _upper_bins(v["rfft_lib"]))
    g, err = fit_scale(v["ifft_lib"], np.fft.ifft(v["cfft_own"]))
    s += _macro("LibIfftGain", "%.4g" % g)
    s += _macro("LibIfftErr", _sci(err))

    s += _macro("LoopbackLsb", "%d" % run.diffs["loopback_lsb"])
    mac = cdsp.impulse(len(run.impulse["identity"]), experiment=3)
    s += _macro("BoardIrErr", _sci(max(np.abs(run.impulse["identity"] - mac[:, 0]).max(),
                                       np.abs(run.impulse["lowpass"] - mac[:, 1]).max())))
    plot.impulse([plot.Series("声道 0（FFT→IFFT）", impulse=mac[:, 0], measured=run.impulse["identity"]),
                  plot.Series("声道 1（FFT→保留 %d Hz 以下→IFFT）" % design.KEEP_HZ, impulse=mac[:, 1],
                              measured=run.impulse["lowpass"])],
                 OUT / "board_impulse.pdf", reference="Mac 上的 C 代码")

    c = run.cycles
    s += _macro("CclkMHz", "%.0f" % (run.cclk_hz / 1e6))
    budget = run.cclk_hz / FS
    s += _macro("CyclesBudget", "%.0f" % budget)
    s += _macro("FrameBudget", "%.0f" % (budget * N))
    for key, name in [("cfft_own", "CfftOwn"), ("ifft_own", "IfftOwn"), ("rfft_own", "RfftOwn"),
                      ("rifft_own", "RifftOwn"), ("cfft_lib", "CfftLib"), ("ifft_lib", "IfftLib"),
                      ("rfft_lib", "RfftLib"), ("frame", "Frame")]:
        s += _macro(name + "Cycles", "%d" % c[key].cycles)
    s += _macro("CfftRatio", "%.1f" % (c["cfft_own"].cycles / c["cfft_lib"].cycles))
    s += _macro("IfftRatio", "%.1f" % (c["ifft_own"].cycles / c["ifft_lib"].cycles))
    s += _macro("RfftRatio", "%.1f" % (c["rfft_own"].cycles / c["rfft_lib"].cycles))
    s += _macro("RealSaving", "%.0f" % (100.0 * (1 - c["rfft_own"].cycles / c["cfft_own"].cycles)))
    whole = c["audio_process"]
    s += _macro("AudioFrames", "%d" % whole.frames)
    s += _macro("AudioCycles", "%d" % whole.cycles)
    s += _macro("AudioPerFrame", "%.0f" % whole.per_sample)
    s += _macro("AudioLoad", "%.0f" % (100.0 * whole.per_sample / budget))
    return s


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--log", type=Path, default=BOARD_LOG, help="console log from make board")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    fft = Fft()
    rng = np.random.default_rng(3)
    worst = errors(fft, rng)

    x = rng.uniform(-1, 1, N).astype(np.float32)
    plot_spectrum(x, fft.real_forward(x), OUT / "spectrum.pdf")

    noise = NOISE_LEVEL * rng.uniform(-1, 1, (NOISE_FRAMES, 2))
    y = cdsp.process(noise, experiment=3)
    lsb = np.abs(np.round((y[N:, 0] - cdsp.from_s24(cdsp.to_s24(noise[:-N, 0]))) * cdsp.S24_SCALE)).max()
    plot_loopback(cdsp.from_s24(cdsp.to_s24(noise[:, 0])), y[:, 0], OUT / "loopback.pdf")

    t = np.arange(DEMO_FRAMES) / FS
    demo = np.zeros((DEMO_FRAMES, 2))
    demo[:, 1] = DEMO_LEVEL * (np.sin(2 * np.pi * DEMO_KEEP_HZ * t) + np.sin(2 * np.pi * DEMO_CUT_HZ * t))
    out = cdsp.process(demo, experiment=3)[:, 1]
    f, din, dout = plot_demo(demo[N:, 1], out[N:], OUT / "demo.pdf")
    near = lambda hz: np.abs(f - hz) < 50
    above = (f > design.KEEP_HZ + 2 * design.bin_hz()) & ~near(DEMO_CUT_HZ)

    s = "% Generated by coursework/E03-fft/figures.py; do not edit.\n"
    s += _macro("SampleRate", "%d" % FS)
    s += _macro("FftN", "%d" % N)
    s += _macro("FftHalf", "%d" % M)
    s += _macro("FftStages", "%d" % design.LOG2N)
    s += _macro("BinHz", "%.0f" % design.bin_hz())
    s += _macro("KeepHz", "%.0f" % design.KEEP_HZ)
    s += _macro("KeepBins", "%d" % int(design.bin_gain().sum()))
    s += _macro("FrameMs", "%.3f" % (N / FS * 1000))
    s += _macro("Trials", "%d" % TRIALS)
    s += _macro("CfftErr", _sci(worst["cfft"]))
    s += _macro("IfftErr", _sci(worst["ifft"]))
    s += _macro("CroundErr", _sci(worst["cround"]))
    s += _macro("RfftErr", _sci(worst["rfft"]))
    s += _macro("RifftErr", _sci(worst["rifft"]))
    s += _macro("RroundErr", _sci(worst["rround"]))
    s += _macro("RroundLsb", "%.1f" % (worst["rround"] * cdsp.S24_SCALE))
    s += _macro("NoiseSeconds", "%.0f" % (NOISE_FRAMES / FS))
    s += _macro("LoopbackMacLsb", "%d" % lsb)
    s += _macro("DemoKeepHz", "%.0f" % DEMO_KEEP_HZ)
    s += _macro("DemoCutHz", "%.0f" % DEMO_CUT_HZ)
    s += _macro("DemoKeepDb", "\\num{%.1f}" % (dout[near(DEMO_KEEP_HZ)].max() - din[near(DEMO_KEEP_HZ)].max()))
    s += _macro("DemoCutDb", "\\num{%.1f}" % (dout[near(DEMO_CUT_HZ)].max() - din[near(DEMO_CUT_HZ)].max()))
    s += _macro("DemoSpreadDb", "\\num{%.0f}" % dout[above].max())
    # operation counts: N/2 log2 N butterflies of one complex multiply and two complex adds
    bf = M * design.LOG2N
    s += _macro("Butterflies", "%d" % bf)
    s += _macro("FftMuls", "%d" % (4 * bf))
    s += _macro("FftAdds", "%d" % (6 * bf))
    s += _macro("DftMuls", "%d" % (4 * N * N))
    bfh = (M // 2) * (design.LOG2N - 1)
    s += _macro("HalfButterflies", "%d" % bfh)
    s += _macro("RealMuls", "%d" % (4 * bfh + 4 * (M - 1)))
    for name, signature in [("Radix", "static void Radix2"), ("RealFwd", "void FftRealForward"),
                            ("RealInv", "void FftRealInverse")]:
        first, last = lines_of(FFT_C, signature)
        s += _macro(name + "First", "%d" % first)
        s += _macro(name + "Last", "%d" % last)

    try:
        run = boardlog.read(args.log)
    except FileNotFoundError:
        run = None
    if run is not None and "rfft_lib" in run.vectors:
        s += board_macros(run, fft)
    else:
        run = None

    (OUT / "results.tex").write_text(s)
    print("wrote %s%s" % (OUT.relative_to(REPO), "" if run else " (no board log)"))


if __name__ == "__main__":
    main()
