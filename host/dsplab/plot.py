"""Figure style and the plots the lab reports share.

Each plot takes a list of Series, so one call compares any number of filters,
or a design against what the board measured.
"""

from dataclasses import dataclass, field

import matplotlib

matplotlib.use("pdf")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

# Text width of an A4 report with 2.5 cm margins
WIDTH_IN = 6.0

plt.rcParams.update({
    # Latin text in Times New Roman, Chinese falls back to Songti, as the reports require
    "font.family": ["Times New Roman", "Songti SC"],
    "mathtext.fontset": "stix",
    "font.size": 9,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "axes.unicode_minus": False,
    "legend.fontsize": 8,
    "savefig.bbox": "tight",
})


@dataclass
class Series:
    label: str
    response: object = None         # dsplab.response.Response
    impulse: np.ndarray = None
    style: dict = field(default_factory=dict)


def _finish(fig, path):
    fig.savefig(path)
    plt.close(fig)


def _khz(ax, fmax_hz):
    ax.set_xlim(0, fmax_hz / 1000.0)
    ax.set_xlabel("频率 / kHz")


def impulse(series, path, *, title=None):
    """One panel per series: the impulse response as stems."""
    fig, axes = plt.subplots(len(series), 1, figsize=(WIDTH_IN, 1.9 * len(series)), squeeze=False)
    for ax, s in zip(axes[:, 0], series):
        n = np.arange(len(s.impulse))
        ax.stem(n, s.impulse, basefmt=" ", markerfmt=".", **s.style)
        ax.set_title(s.label)
        ax.set_ylabel("$h[n]$")
    axes[-1, 0].set_xlabel("$n$ / 采样点")
    if title:
        fig.suptitle(title)
    fig.tight_layout()
    _finish(fig, path)


def magnitude(series, path, *, fmax_hz, ylim=(-100, 5), marks_hz=()):
    fig, ax = plt.subplots(figsize=(WIDTH_IN, 2.8))
    for s in series:
        ax.plot(s.response.f / 1000.0, s.response.mag_db, label=s.label, **s.style)
    for f in marks_hz:
        ax.axvline(f / 1000.0, color="0.5", lw=0.8, ls=":")
    ax.axhline(-3, color="0.5", lw=0.6, ls="--")
    ax.set_ylim(*ylim)
    ax.set_ylabel("幅度 / dB")
    _khz(ax, fmax_hz)
    ax.legend()
    _finish(fig, path)


def phase(series, path, *, fmax_hz):
    fig, ax = plt.subplots(figsize=(WIDTH_IN, 2.8))
    for s in series:
        keep = s.response.f <= fmax_hz
        ax.plot(s.response.f[keep] / 1000.0, s.response.phase_deg[keep], label=s.label, **s.style)
    ax.set_ylabel("相位 / (°)")
    _khz(ax, fmax_hz)
    ax.legend()
    _finish(fig, path)


def group_delay(series, path, *, fmax_hz, fs):
    """Group delay in samples on the left axis, milliseconds on the right."""
    fig, ax = plt.subplots(figsize=(WIDTH_IN, 2.8))
    for s in series:
        keep = s.response.f <= fmax_hz
        ax.plot(s.response.f[keep] / 1000.0, s.response.gd[keep], label=s.label, **s.style)
    ax.set_ylabel("群延迟 / 采样点")
    ax.set_ylim(bottom=0)
    right = ax.secondary_yaxis("right", functions=(lambda n: n / fs * 1000.0, lambda ms: ms * fs / 1000.0))
    right.set_ylabel("ms")
    _khz(ax, fmax_hz)
    ax.legend()
    _finish(fig, path)
