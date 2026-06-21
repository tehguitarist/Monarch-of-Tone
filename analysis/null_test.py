#!/usr/bin/env python3
"""Null test: plugin render vs real-pedal (NAM) capture.

Null depth is the strongest "does it match" metric, but it is VERY sensitive to alignment and
level — and the captures are LEVEL-NORMALISED per clipping mode, so a plain subtract won't cancel.
This harness handles both: integer + sub-sample (fractional) time alignment via FFT phase, and a
least-squares per-file gain. Reports null depth overall and per octave-ish band.

Expected pattern vs NAM captures (2026-06-21): deep null in the low-mids (−20 to −35 dB), less at
2–6 kHz, little above 6 kHz (NAM HF limit). Clean/Boost nulls deepest; driven modes shallower
(exact clip waveform + the empirical even-harmonic layer won't phase-match perfectly).

Usage:
  null_test.py CAPTURE.wav PLUGIN.wav [t0 t1]     # default segment = driven sweep 13.5–19.5 s
Render the PLUGIN at the capture's settings first, e.g.:
  PedalRender test_signal_48k.wav plug.wav 0.6 0.5 0.5 0.0 1      # drive .6 tone .5 vol .5 OD
"""
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfiltfilt

FS = 48000
BANDS = [("100-300Hz", 100, 300), ("300Hz-1k", 300, 1000), ("1-2k", 1000, 2000),
         ("2-6k", 2000, 6000), ("6k+", 6000, 20000)]


def load(p):
    sr, x = wavfile.read(p)
    x = x.astype(np.float64) / (np.iinfo(x.dtype).max if x.dtype.kind == 'i' else 1)
    if x.ndim > 1:
        x = x.mean(axis=1)
    assert sr == FS, f"{p}: expected {FS}, got {sr}"
    return x


def best_null(c, p):
    """Align p to c (integer + fractional delay) and least-squares gain; return residual, gain."""
    n = min(len(c), len(p))
    c, p = c[:n].copy(), p[:n].copy()
    lag = np.argmax(np.abs(np.correlate(c - c.mean(), p - p.mean(), 'full'))) - (n - 1)
    p = np.roll(p, lag)
    P = np.fft.rfft(p)
    f = np.fft.rfftfreq(n)
    best = None
    for frac in np.arange(-1.0, 1.01, 0.1):          # sub-sample delay search
        pf = np.fft.irfft(P * np.exp(-2j * np.pi * f * frac), n)
        g = np.dot(c, pf) / (np.dot(pf, pf) + 1e-20)  # least-squares per-file gain
        r = c - g * pf
        d = np.dot(r, r)
        if best is None or d < best[0]:
            best = (d, r, g)
    return best[1], best[2]


def null_db(c, r, band=None):
    if band is not None:
        sos = butter(4, [band[0] / (FS / 2), min(band[1], FS / 2 - 1) / (FS / 2)], btype='band', output='sos')
        c, r = sosfiltfilt(sos, c), sosfiltfilt(sos, r)
    return 20 * np.log10((np.sqrt(np.mean(r ** 2)) + 1e-20) / (np.sqrt(np.mean(c ** 2)) + 1e-20))


def main():
    cap, plug = sys.argv[1], sys.argv[2]
    t0, t1 = (float(sys.argv[3]), float(sys.argv[4])) if len(sys.argv) > 4 else (13.5, 19.5)
    c = load(cap)[int(t0 * FS):int(t1 * FS)]
    p = load(plug)[int(t0 * FS):int(t1 * FS)]
    r, g = best_null(c, p)
    print(f"gain {20 * np.log10(abs(g)):+.1f} dB | OVERALL null {null_db(c, r):+.1f} dB "
          f"(more negative = better match)")
    for name, lo, hi in BANDS:
        print(f"  {name:10} {null_db(c, r, (lo, hi)):+6.1f} dB")


if __name__ == "__main__":
    main()
