"""Run a wav file through the firmware's AudioProcess() and write the result.

    uv run python -m dsplab.wavrun in.wav out.wav --play

The processing is the C in firmware/audio_loopback/dsp/, built for the Mac by
dsplab.cdsp, so what plays here is what the board computes, minus the analog
path. A mono file is copied onto a stereo pair, as on the board when one input
feeds two adjacent channels: the left output then carries the even-channel
processing and the right the odd one. The coefficients are designed for the
codecs' rate, so feed files at that rate.
"""

import argparse
import subprocess
from pathlib import Path

import numpy as np
from scipy.io import wavfile

from dsplab import cdsp


def read(path):
    """Samples as frames x channels floats in [-1, 1), and the rate."""
    fs, x = wavfile.read(path)
    if x.dtype == np.uint8:
        x = (x.astype(float) - 128.0) / 128.0
    elif np.issubdtype(x.dtype, np.integer):
        x = x / float(-np.iinfo(x.dtype).min)
    x = np.asarray(x, dtype=float)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    return x, fs


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, help="written as 32-bit float")
    parser.add_argument("--block", type=int, default=cdsp.BLOCK_FRAMES, help="frames per AudioProcess() call")
    parser.add_argument("--play", action="store_true", help="play the output with afplay")
    args = parser.parse_args()

    x, fs = read(args.input)
    y = cdsp.process(x, block=args.block)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    wavfile.write(args.output, fs, y.astype(np.float32))
    print("wrote %s: %d frames, %d channels at %d Hz" % (args.output, len(y), y.shape[1], fs))
    if args.play:
        subprocess.run(["afplay", str(args.output)], check=True)


if __name__ == "__main__":
    main()
