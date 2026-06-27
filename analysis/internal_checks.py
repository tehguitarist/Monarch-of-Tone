#!/usr/bin/env python3
"""Internal (no-hardware-reference) behavioural checks for axes the captures don't cover.

The 25 NAM captures are all at fixed volume=noon & presence=min, Yellow only — so volume,
presence, sample-rate behaviour and aliasing have NO hardware reference. These checks instead
assert the plugin BEHAVES correctly (the way a real analog pedal must), driving tools/PedalRender.
Each check prints PASS/FAIL with the measured margin; exit code is nonzero if any fails.

  volume_invariance : volume only scales level — normalised FR & THD are identical across vols.
  volume_taper      : the audio taper follows pow(10, 1.8*(x-1)) (noon = -18 dB).
  knob_monotonic    : drive->THD up, tone->brightness up, presence->treble up (monotonic).
  aliasing          : a hot HF tone produces little inharmonic/alias energy at the 8x render OS.
  sample_rate       : voicing (FR + THD) is consistent at 44.1 / 48 / 96 kHz.

Usage:  internal_checks.py            # runs all; needs PedalRender built + the v2 signal present
"""
import os
import subprocess
import sys
import numpy as np

import gen_test_signal as gts
import analyze

SIGNAL = "analysis/test_signal_48k.wav"
TMP = "/tmp/monarch_internal"
FS = gts.FS


def pr_bin():
    for p in ("build/PedalRender_artefacts/Release/PedalRender",
              "build/PedalRender_artefacts/PedalRender", "build/PedalRender"):
        if os.path.exists(p):
            return p
    sys.exit("PedalRender not found — build it: cmake --build build --target PedalRender")


def render(pr, drive, tone, vol, pres, clip, tag, infile=SIGNAL):
    out = os.path.join(TMP, f"{tag}.wav")
    subprocess.run([pr, infile, out, str(drive), str(tone), str(vol), str(pres), str(clip)],
                   check=True, capture_output=True)
    return analyze.load_mono(out)


def tone_rms(x, name):
    return analyze.rms(analyze.seg(x, name))


def thd_at(x, name, f):
    s = analyze.seg(x, name)
    w = np.hanning(len(s))
    X = np.abs(np.fft.rfft(s * w))
    res = FS / len(s)

    def mag(fc):
        k = int(round(fc / res))
        return np.sqrt(np.sum(X[max(0, k - 3):k + 4] ** 2))
    h = np.array([mag((i + 1) * f) for i in range(8) if (i + 1) * f < FS / 2])
    return np.sqrt(np.sum(h[1:] ** 2)) / (h[0] + 1e-20)


def brightness(x):
    """HF/LF ratio from the clean sweep linear TF (dB(4k) - dB(500))."""
    F, H = analyze.linear_tf(analyze.seg(x, "sweep_clean", guard=0.3), gts.SWEEP_CLEAN_SEC)
    m = analyze.db(H)
    return m[np.argmin(np.abs(F - 4000))] - m[np.argmin(np.abs(F - 500))]


# --------------------------------------------------------------------------------------- checks
def check_volume_invariance(pr):
    """Volume must only scale level — after level-matching, FR & THD must be identical."""
    refs = {v: render(pr, 0.6, 0.5, v, 0.0, 1, f"vol_{v}") for v in (0.3, 0.5, 0.7)}
    base = refs[0.5]
    fr_dev, thd_dev = 0.0, 0.0
    Fb, Hb = analyze.linear_tf(analyze.seg(base, "sweep_clean", guard=0.3), gts.SWEEP_CLEAN_SEC)
    mb = analyze.db(Hb); mb -= mb[np.argmin(np.abs(Fb - 1000))]
    thd_b = thd_at(base, "tone_1000", 1000)
    for v, x in refs.items():
        if v == 0.5:
            continue
        F, H = analyze.linear_tf(analyze.seg(x, "sweep_clean", guard=0.3), gts.SWEEP_CLEAN_SEC)
        m = analyze.db(H); m -= m[np.argmin(np.abs(F - 1000))]
        band = (F > 50) & (F < 12000)
        fr_dev = max(fr_dev, float(np.max(np.abs(m[band] - mb[band]))))
        thd_dev = max(thd_dev, abs(thd_at(x, "tone_1000", 1000) - thd_b) * 100)
    ok = fr_dev < 0.3 and thd_dev < 0.5
    print(f"[{'PASS' if ok else 'FAIL'}] volume_invariance: max FR dev {fr_dev:.3f} dB (<0.3), "
          f" THD dev {thd_dev:.3f}% (<0.5)")
    return ok


def check_volume_taper(pr):
    """Audio taper should track pow(10, 1.8*(x-1)), normalised at noon (x=0.5). Probe with a
    quiet tone (lvl_-30, drive 0.1, Boost) so the chain stays linear and isolates the taper —
    a hot tone would rail the op-amp and mask the top of the curve."""
    vols = [0.2, 0.35, 0.5, 0.65, 0.8, 1.0]
    meas = np.array([analyze.db(analyze.rms(analyze.seg(
        render(pr, 0.1, 0.5, v, 0.0, 0, f"tap_{v}"), "lvl_-30"))) for v in vols])
    meas -= meas[vols.index(0.5)]
    want = np.array([36.0 * (v - 1) for v in vols])   # 20*log10(pow(10,1.8(v-1))) = 36(v-1)
    want -= want[vols.index(0.5)]
    dev = float(np.max(np.abs(meas - want)))
    ok = dev < 0.5
    print(f"[{'PASS' if ok else 'FAIL'}] volume_taper: max deviation from pow(10,1.8(x-1)) "
          f"{dev:.3f} dB (<0.5)")
    return ok


def check_knob_monotonic(pr):
    # Drive: probe with the driven SWEEP's mid-band THD, not a single hot tone — a -14 dBFS tone
    # is already fully diode-clamped at every drive setting (THD flat/noisy), whereas the swept
    # broadband distortion rises monotonically with drive (the audible "more gain" behaviour).
    drives = [0.1, 0.3, 0.5, 0.7, 0.9]
    thd = [analyze.thd_vs_freq(analyze.seg(render(pr, d, 0.5, 0.5, 0.0, 1, f"drv_{d}"),
           "sweep_drv_-18", guard=0.3), gts.SWEEP_DRIVEN_SEC)["250-1k"] / 100.0 for d in drives]
    drive_ok = all(b >= a - 1e-3 for a, b in zip(thd, thd[1:]))

    tones = [0.1, 0.3, 0.5, 0.7, 0.9]
    br = [brightness(render(pr, 0.5, t, 0.5, 0.0, 0, f"tn_{t}")) for t in tones]
    tone_ok = all(b >= a - 0.05 for a, b in zip(br, br[1:]))

    pres = [0.0, 0.33, 0.66, 1.0]
    pr_br = [brightness(render(pr, 0.5, 0.5, 0.5, p, 0, f"pr_{p}")) for p in pres]
    pres_ok = all(b >= a - 0.05 for a, b in zip(pr_br, pr_br[1:]))

    ok = drive_ok and tone_ok and pres_ok
    print(f"[{'PASS' if ok else 'FAIL'}] knob_monotonic: "
          f"drive->THD {'up' if drive_ok else 'NON-MONO'} ({thd[0]*100:.1f}->{thd[-1]*100:.1f}%), "
          f"tone->bright {'up' if tone_ok else 'NON-MONO'} ({br[0]:.1f}->{br[-1]:.1f}dB), "
          f"presence->bright {'up' if pres_ok else 'NON-MONO'} ({pr_br[0]:.1f}->{pr_br[-1]:.1f}dB)")
    return ok


def check_aliasing(pr):
    """Hot 6 kHz tone in Distortion: inharmonic/alias energy should be well below the fundamental."""
    x = render(pr, 0.9, 0.5, 0.5, 0.0, 2, "alias")
    s = analyze.seg(x, "tone_6000")
    w = np.hanning(len(s))
    X = np.abs(np.fft.rfft(s * w))
    F = np.fft.rfftfreq(len(s), 1 / FS)
    f0 = 6000.0
    harm = np.zeros(len(F), bool)
    for k in range(1, int((FS / 2) / f0) + 1):           # mask true harmonics (+-30 Hz)
        harm |= np.abs(F - k * f0) < 30
    harm |= F < 100
    fund = X[np.argmin(np.abs(F - f0))]
    alias = np.sqrt(np.sum(X[~harm] ** 2))
    ratio = analyze.db(alias / (fund + 1e-20))
    ok = ratio < -40
    print(f"[{'PASS' if ok else 'FAIL'}] aliasing: inharmonic energy {ratio:.1f} dB below "
          f"fundamental at 8x render OS (<-40)")
    return ok


def check_sample_rate(pr):
    """Render the same tones at 44.1/48/96 kHz; voicing (FR ratio + 1k THD) must be consistent."""
    from scipy.io import wavfile

    def probe(sr):
        t = np.arange(int(1.0 * sr)) / sr
        seg1k = gts.fade(0.2 * np.sin(2 * np.pi * 1000 * t))
        seg4k = gts.fade(0.2 * np.sin(2 * np.pi * 4000 * t))
        sil = np.zeros(int(0.3 * sr))
        sig = np.concatenate([sil, seg1k, sil, seg4k, sil]).astype(np.float32)
        pin = os.path.join(TMP, f"sr_{sr}_in.wav")
        wavfile.write(pin, sr, sig)
        pout = os.path.join(TMP, f"sr_{sr}_out.wav")
        subprocess.run([pr, pin, pout, "0.6", "0.5", "0.5", "0.0", "1"], check=True,
                       capture_output=True)
        srr, y = wavfile.read(pout)
        y = y.astype(np.float64) / (np.iinfo(y.dtype).max if y.dtype.kind == 'i' else 1)
        if y.ndim > 1:
            y = y.mean(axis=1)

        def band(a, b, f):
            w = y[int(a * sr):int(b * sr)]
            ww = np.hanning(len(w))
            Y = np.abs(np.fft.rfft(w * ww))
            res = sr / len(w)
            mag = lambda fc: np.sqrt(np.sum(Y[max(0, int(fc / res) - 3):int(fc / res) + 4] ** 2))
            h = [mag(k * f) for k in range(1, 6) if k * f < sr / 2]
            return mag(f), np.sqrt(sum(v ** 2 for v in h[1:])) / (h[0] + 1e-20)
        g1, thd1 = band(0.3, 1.3, 1000)
        g4, _ = band(1.6, 2.6, 4000)
        return analyze.db(g4 / g1), thd1 * 100

    base = probe(48000)
    devs = []
    for sr in (44100, 96000):
        fr, thd = probe(sr)
        devs.append((sr, abs(fr - base[0]), abs(thd - base[1])))
    fr_ok = all(d[1] < 1.0 for d in devs)
    thd_ok = all(d[2] < 1.5 for d in devs)
    ok = fr_ok and thd_ok
    detail = ", ".join(f"{sr//1000}k ΔFR{a:.2f} ΔTHD{b:.2f}" for sr, a, b in devs)
    print(f"[{'PASS' if ok else 'FAIL'}] sample_rate: vs 48k -> {detail} (FR<1.0dB, THD<1.5%)")
    return ok


def main():
    if not os.path.exists(SIGNAL):
        sys.exit(f"{SIGNAL} missing — run: python analysis/gen_test_signal.py")
    os.makedirs(TMP, exist_ok=True)
    pr = pr_bin()
    checks = [check_volume_invariance, check_volume_taper, check_knob_monotonic,
              check_aliasing, check_sample_rate]
    results = [c(pr) for c in checks]
    print(f"\n{sum(results)}/{len(results)} checks passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
