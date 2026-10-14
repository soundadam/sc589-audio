"""Read the filter measurements the firmware prints on the UART console.

The line format is defined in firmware/audio_loopback/platform/filter_report.h;
a console log saved by `make board` (build/board.log) holds them among the
other console lines, which are skipped.
"""

from dataclasses import dataclass, field
from pathlib import Path

import numpy as np


@dataclass
class Cycles:
    frames: int     # samples in the measured block
    cycles: int     # core cycles to filter it

    @property
    def per_sample(self):
        return self.cycles / self.frames


@dataclass
class BoardRun:
    cclk_hz: int | None = None
    impulse: dict[str, np.ndarray] = field(default_factory=dict)
    cycles: dict[str, Cycles] = field(default_factory=dict)


def parse(lines):
    run = BoardRun()
    samples = {}
    for line in lines:
        fields = line.strip().split(",")
        if fields[0] == "clock" and len(fields) == 3 and fields[1] == "cclk":
            run.cclk_hz = int(fields[2])
        elif fields[0] == "ir" and len(fields) == 4:
            samples.setdefault(fields[1], {})[int(fields[2])] = float(fields[3])
        elif fields[0] == "cycles" and len(fields) == 4:
            run.cycles[fields[1]] = Cycles(int(fields[2]), int(fields[3]))
    for name, by_index in samples.items():
        n = max(by_index) + 1
        if sorted(by_index) != list(range(n)):
            raise ValueError("impulse response %r is missing samples; the console dropped lines?" % name)
        run.impulse[name] = np.array([by_index[i] for i in range(n)])
    return run


def read(path):
    return parse(Path(path).read_text(errors="replace").splitlines())
