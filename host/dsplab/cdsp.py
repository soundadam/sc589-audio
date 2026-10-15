"""Run the firmware's dsp/ sources on the Mac, through ctypes.

    from dsplab import cdsp
    y = cdsp.process(x)         # x: frames x channels, floats in [-1, 1)

The library is compiled from firmware/audio_loopback/dsp/*.c on first use and
again whenever a source changes, so an edit to the C shows up on the next call
without restarting Python. Each build gets its own file name, because a
process cannot unload a library it has loaded and would keep the old code.

The samples go through AudioProcess() exactly as on the board: 24-bit fixed
point in and out, 64-frame blocks, filter state carried between blocks.
"""

import ctypes
import hashlib
import subprocess
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
ROOT = REPO / "firmware" / "audio_loopback"     # the firmware include root
DSP = ROOT / "dsp"
OUT = REPO / "build" / "cdsp"

# Frames per AudioProcess() call: FRAMES_PER_BUFFER in the play-back path
BLOCK_FRAMES = 64
# Full scale of a 24-bit sample, as in audio_process.c
S24_SCALE = 2.0 ** 23

CC = ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-shared", "-fPIC"]

_loaded = {}


def _sources():
    return sorted(DSP.glob("*.c")) + sorted(DSP.glob("*.h"))


def library():
    """The library built from the current sources, compiled if needed."""
    digest = hashlib.sha256()
    for p in _sources():
        digest.update(p.name.encode())
        digest.update(p.read_bytes())
    key = digest.hexdigest()[:12]
    if key in _loaded:
        return _loaded[key]

    path = OUT / ("libdsp-%s%s" % (key, ".dylib" if sys.platform == "darwin" else ".so"))
    if not path.exists():
        OUT.mkdir(parents=True, exist_ok=True)
        for old in OUT.glob("libdsp-*"):
            old.unlink()
        subprocess.run(CC + ["-I%s" % ROOT, "-o", str(path)] + [str(p) for p in sorted(DSP.glob("*.c"))] + ["-lm"],
                       check=True)

    lib = ctypes.CDLL(str(path))
    lib.AudioProcessInit.argtypes = []
    lib.AudioProcessInit.restype = None
    lib.AudioProcess.argtypes = [
        np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS"),
        np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS"),
        ctypes.c_uint32,
        ctypes.c_uint32,
    ]
    lib.AudioProcess.restype = None
    _loaded[key] = lib
    return lib


def to_s24(x):
    """Floats in [-1, 1) to sign-extended 24-bit samples, saturating."""
    return np.clip(np.round(np.asarray(x, dtype=float) * S24_SCALE), -S24_SCALE, S24_SCALE - 1).astype(np.int32)


def from_s24(x):
    return np.asarray(x, dtype=float) / S24_SCALE


def process(x, *, block=BLOCK_FRAMES, init=True):
    """Run x through AudioProcess() in blocks of `block` frames.

    x       frames x channels, or one dimension for a single channel; floats in [-1, 1)
    init    clear the filter state first; False continues the previous stream

    Returns floats of the same shape as x.
    """
    x = np.asarray(x, dtype=float)
    frames = x.reshape(len(x), -1)
    channels = frames.shape[1]
    src = np.ascontiguousarray(to_s24(frames))
    dst = np.empty_like(src)

    lib = library()
    if init:
        lib.AudioProcessInit()
    for start in range(0, len(src), block):
        n = min(block, len(src) - start)
        out = np.empty((n, channels), dtype=np.int32)
        lib.AudioProcess(np.ascontiguousarray(src[start:start + n]), out, n, channels)
        dst[start:start + n] = out
    return from_s24(dst).reshape(x.shape)


def impulse(n, *, channels=2):
    """First n samples of each channel's response to a unit impulse at half scale.

    Half scale keeps the IIR's overshoot clear of saturation; the result is
    scaled back, so it compares directly with a designed h[n]. Returns
    n x channels.
    """
    x = np.zeros((n, channels))
    x[0] = 0.5
    return process(x) / 0.5
