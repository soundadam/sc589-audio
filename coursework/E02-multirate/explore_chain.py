# %% [markdown]
# E02 exploration: the C chain as the firmware runs it, next to scipy references.
#
# Run cell by cell in an editor's interactive window, from the repository's uv
# environment. After an edit to firmware/audio_loopback/dsp/*.c, rerun a cell:
# dsplab.cdsp rebuilds the library from the changed sources. dsplab.plot is not
# imported here because it switches matplotlib to the pdf backend for the
# report figures.

# %%
import matplotlib.pyplot as plt
import numpy as np
from scipy import signal

from dsplab import cdsp, response

FS = 48000      # the codecs' rate (SAMPLE_RATE in adc_dac_playback.h)

# %% Impulse response of each channel of the current AudioProcess()
ir = cdsp.impulse(512, channels=2)
fig, ax = plt.subplots()
for ch in range(ir.shape[1]):
    r = response.from_impulse(ir[:, ch], fs=FS)
    ax.plot(r.f / 1000, r.mag_db, label="channel %d" % ch)
ax.set(xlabel="kHz", ylabel="dB", ylim=(-120, 5))
ax.legend()

# %% Block invariance: the same stream in 64-frame and in odd-sized blocks
x = np.random.default_rng(1).uniform(-0.4, 0.4, (8192, 2))
print("max difference:", np.abs(cdsp.process(x) - cdsp.process(x, block=7)).max())

# %% scipy reference: decimate by M, then interpolate back, one tone per run.
# A tone above FS / (2 M) should vanish rather than fold back into the band.
M = 4
t = np.arange(FS) / FS
for f0 in (1000.0, 0.75 * FS / M):
    tone = 0.5 * np.sin(2 * np.pi * f0 * t)
    y = signal.resample_poly(signal.resample_poly(tone, 1, M), M, 1)[:len(tone)]
    spec = 20 * np.log10(np.abs(np.fft.rfft(y[FS // 2:] * np.hanning(FS // 2))) + 1e-12)
    plt.figure()
    plt.plot(np.fft.rfftfreq(FS // 2, 1 / FS) / 1000, spec)
    plt.title("tone %.0f Hz through M = %d" % (f0, M))
    plt.xlabel("kHz")
