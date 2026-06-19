#!/usr/bin/env python3
"""A/B compare a plugin render vs the real-pedal (NAM) capture, using the calibrated test signal.

Segments are at known times (see gen_test_signal.py). We skip cross-correlation alignment and
just analyse the steady middle of each segment (processing latency << segment length), which is
robust to the small plugin/NAM latency.

Usage:
  analyze.py REAL.wav PLUGIN.wav [LABEL]

Reports, per (real, plugin):
  - level offset (plugin re-gained to match real in the near-linear clean-sweep tail)
  - harmonic content of the −14 dBFS freq tones (THD%, H2/H3 dB below fundamental)
  - compression curve from the 1 kHz level steps (−24..−6 dBFS in → out)
  - clean-sweep EQ at octave centres (validates the Tone knob)
"""
import sys
import numpy as np
from scipy.io import wavfile

FS = 48000

# (name, start_sec, end_sec)
LEVEL_STEPS = [("-24", 23.0, 24.0), ("-18", 24.3, 25.3), ("-12", 25.6, 26.6), ("-6", 26.9, 27.9)]
FREQ_TONES = [(82.41, 28.2, 29.2), (110, 29.5, 30.5), (220, 30.8, 31.8), (440, 32.1, 33.1),
              (1000, 33.4, 34.4), (2000, 34.7, 35.7), (5000, 36.0, 37.0)]
CLEAN_SWEEP = (2.0, 12.0)


def load_mono(path):
    sr, x = wavfile.read(path)
    if x.dtype.kind == 'i':
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    else:
        x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)  # stereo capture is dual-mono
    assert sr == FS, f"{path}: expected {FS} Hz, got {sr}"
    return x


def seg(x, t0, t1, guard=0.2):
    a = int((t0 + guard) * FS)
    b = int((t1 - guard) * FS)
    return x[a:min(b, len(x))]


def rms(x):
    return np.sqrt(np.mean(x ** 2)) + 1e-20


def harmonics(x, f0, n=6):
    w = np.hanning(len(x))
    X = np.abs(np.fft.rfft(x * w))
    f = np.fft.rfftfreq(len(x), 1 / FS)
    def mag_at(fc):
        k = int(round(fc / (FS / len(x))))
        lo, hi = max(0, k - 3), min(len(X), k + 4)
        return np.sqrt(np.sum(X[lo:hi] ** 2))
    h = np.array([mag_at((i + 1) * f0) for i in range(n)])
    thd = np.sqrt(np.sum(h[1:] ** 2)) / (h[0] + 1e-20)
    return h, thd


def db(x):
    return 20 * np.log10(np.abs(x) + 1e-20)


def main():
    real_path, plug_path = sys.argv[1], sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else ""
    real, plug = load_mono(real_path), load_mono(plug_path)

    # Level-match plugin to real using the quiet clean-sweep tail (near-linear in all modes).
    ref = seg_window = (3.0, 6.0)
    g = rms(seg(real, *ref)) / rms(seg(plug, *ref))
    plug = plug * g

    print(f"\n=== {label or '(render)'} ===")
    print(f"plugin re-gained {db(g):+.2f} dB to match real (clean-sweep ref); "
          f"real peak {db(np.max(np.abs(real))):.1f} dBFS, plugin {db(np.max(np.abs(plug))):.1f} dBFS")

    print("\n  Harmonic content (−14 dBFS tones):  THD%% | H2,H3 dB below fundamental")
    print("  %-7s  %-18s  %-18s" % ("freq", "REAL", "PLUGIN"))
    for f0, t0, t1 in FREQ_TONES:
        hr, thr = harmonics(seg(real, t0, t1), f0)
        hp, thp = harmonics(seg(plug, t0, t1), f0)
        print("  %-7.0f  THD %5.1f%%  H2 %4.0f H3 %4.0f   THD %5.1f%%  H2 %4.0f H3 %4.0f"
              % (f0, 100 * thr, db(hr[1] / hr[0]), db(hr[2] / hr[0]),
                 100 * thp, db(hp[1] / hp[0]), db(hp[2] / hp[0])))

    print("\n  Compression curve (1 kHz level steps, output dBFS):")
    print("  %-8s  %8s  %8s  %6s" % ("in dBFS", "REAL", "PLUGIN", "Δ"))
    base_r = base_p = None
    for nm, t0, t1 in LEVEL_STEPS:
        lr, lp = db(rms(seg(real, t0, t1))), db(rms(seg(plug, t0, t1)))
        print("  %-8s  %8.2f  %8.2f  %+5.2f" % (nm, lr, lp, lp - lr))

    print("\n  Clean-sweep EQ (relative dB at octave centres):")
    sr_real = seg(real, *CLEAN_SWEEP, guard=0.3)
    sr_plug = seg(plug, *CLEAN_SWEEP, guard=0.3)
    from scipy.signal import welch
    fr, Pr = welch(sr_real, FS, nperseg=8192)
    fp, Pp = welch(sr_plug, FS, nperseg=8192)
    print("  %-7s  %8s  %8s" % ("freq", "REAL", "PLUGIN"))
    for fc in (100, 250, 500, 1000, 2000, 4000, 8000):
        ir = np.argmin(np.abs(fr - fc))
        rr = 10 * np.log10(Pr[ir] / Pr[np.argmin(np.abs(fr - 1000))])
        rp = 10 * np.log10(Pp[ir] / Pp[np.argmin(np.abs(fp - 1000))])
        print("  %-7d  %8.2f  %8.2f" % (fc, rr, rp))


if __name__ == "__main__":
    main()
