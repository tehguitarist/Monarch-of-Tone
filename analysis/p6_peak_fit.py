#!/usr/bin/env python3
"""P6 fit/probe harness — mid-gain FR peak displacement (FR_THD_AUDIT.md P6).

comprehensive_report.py renders 44 captures x 4 sweeps; this renders a chosen subset once and
prints exactly the two numbers P6 is read on:

  peak    overall-FR peak frequency on `sweep_clean`, plugin vs pedal, as an octave error
          (same `_peak_hz` broadband argmax as fr_thd_audit.py `peaks`, so numbers are comparable)
  tilt    the compression-profile error: mean(plugin-minus-pedal THD, dB) over 400-2000 Hz minus
          the same over 100-250 Hz, on `sweep_drv_-6`. Negative = the plugin under-compresses at
          mid/HF relative to LF, which leaves its FR peak too HIGH. This is the quantity the
          mode-split tracks (r = -0.72 over G3-G7, all modes pooled).

`--in-gain DB` pre-scales the plugin's input signal (and deconvolves against the same scaled
reference, so the measurement stays a transfer function and a pure gain cannot move the peak).
That is the "boosting the plugin input reproduces the bloom" probe from CLAUDE.md, run as a
measurement rather than by ear: any peak movement it produces is nonlinear, i.e. clip-depth.

Usage:
  p6_peak_fit.py                          # default subset, no input gain
  p6_peak_fit.py --in-gain 3 6            # sweep input gains, one table per gain
  p6_peak_fit.py --modes OD Dist
  p6_peak_fit.py --json OUT.json          # machine-readable
  p6_peak_fit.py --compare BASE.json      # diff against a saved run

Needs the local-only captures in analysis/pedal_export2/ and a built tools/PedalRender.
"""
import argparse
import concurrent.futures as cf
import json
import math
import os
import sys
import tempfile

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import analyze as A               # noqa: E402
import captures as C              # noqa: E402
import comprehensive_report as R  # noqa: E402

# Mid-gain rows the P6 table is stated on, plus the drive extremes that show the migration deficit.
DEFAULT_SUBSET = (
    "G2 T5 OD", "G4 T5 OD", "G5 T5 OD", "G6 T5 OD", "G7 T5 OD", "G8 T5 OD", "G10 T5 OD",
    "G2 T5 Dist", "G4 T5 Dist", "G5 T5 Dist", "G6 T5 Dist", "G7 T5 Dist", "G8 T5 Dist", "G10 T5 Dist",
    "G2 T5 Clean", "G5 T5 Clean", "G6 T5 Clean", "G8 T5 Clean", "G10 T5 Clean",
)

PEAK_SWEEP = "sweep_clean"
TILT_SWEEP = "sweep_drv_-6"
TILT_LO = (100.0, 250.0)    # reference band: both modes' clipping already matches here
TILT_HI = (400.0, 2000.0)   # below 2.5 kHz, where capture-side NAM aliasing starts inflating THD


def peak_hz(y, bands, lo=25.0, hi=12000.0):
    """Broadband argmax with parabolic interpolation in log-f — identical to fr_thd_audit._peak_hz."""
    m = (bands >= lo) & (bands <= hi)
    f, v = bands[m], np.asarray(y, float)[m]
    i = int(np.argmax(v))
    if 0 < i < len(f) - 1:
        x = np.log2(f[i - 1:i + 2])
        a, b, c = v[i - 1:i + 2]
        den = a - 2 * b + c
        if den != 0:
            return float(2 ** (x[1] - 0.5 * (c - a) / den * (x[1] - x[0])))
    return float(f[i])


def band_mean(vals, bands, lo, hi):
    m = (bands >= lo) & (bands <= hi)
    return float(np.mean(np.asarray(vals, float)[m]))


def analyse(path, parsed, orig, ref_path, binpath, os_factor, bands):
    """Render one capture's settings against `ref_path` and return its peak + tilt numbers."""
    cached = R.get_pedal_features(path, orig, R.CACHE_DIR, True)
    if cached is None:
        return None
    cap_al, pedal_features = cached

    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "r.wav")
        args = C.render_args(parsed)
        os_index = C.OS_FACTOR_TO_INDEX.get(os_factor, 3)
        import subprocess
        cmd = [binpath, ref_path, out, *args, "render", str(os_index)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(f"  ! render failed: {r.stderr.strip() or r.stdout.strip()}\n")
            return None
        ren = A.load_mono(out)

    # The render is aligned to the reference it was rendered through; the input gain is divided
    # back out by the deconvolution, so `ref` here must be the SCALED signal, not the pristine one.
    ref = A.load_mono(ref_path)
    ren_al, _ = A.align(ren, ref)

    # A.transfer already returns dB — do not log it again.
    f_ren, H_ren = A.transfer(A.seg_of(ren_al, PEAK_SWEEP), A.seg_of(ref, PEAK_SWEEP))
    f_cap, H_cap = pedal_features["fr_transfer"][PEAK_SWEEP]
    plug_db = [float(np.interp(b, f_ren, H_ren)) for b in bands]
    ped_db = [float(np.interp(b, f_cap, H_cap)) for b in bands]

    p_pk = peak_hz(plug_db, bands)
    q_pk = peak_hz(ped_db, bands)

    # THD-vs-frequency on the driven sweep, same Farina extraction both sides.
    f_t, thd_ren = A.harmonic_thd_curve(A.seg_of(ren_al, TILT_SWEEP), A.seg_of(ref, TILT_SWEEP),
                                        max_order=7)[:2]
    f_tc, thd_cap = pedal_features["farina"][TILT_SWEEP][:2]
    tp = np.interp(bands, f_t, thd_ren)
    tq = np.interp(bands, f_tc, thd_cap)
    err = 20 * np.log10(np.maximum(tp, 1e-3) / np.maximum(tq, 1e-3))
    tilt = band_mean(err, bands, *TILT_HI) - band_mean(err, bands, *TILT_LO)

    return {
        "plug_hz": p_pk, "ped_hz": q_pk, "oct": math.log2(p_pk / q_pk),
        "tilt_db": tilt,
        "thd_lo_db": band_mean(err, bands, *TILT_LO),
        "thd_hi_db": band_mean(err, bands, *TILT_HI),
    }


def scaled_reference(gain_db, td):
    """Write a gain-scaled copy of the reference signal; return its path (or ORIG at 0 dB)."""
    if abs(gain_db) < 1e-9:
        return A.ORIG
    x = A.load_mono(A.ORIG)
    path = os.path.join(td, f"ref{gain_db:+g}.wav")
    wavfile.write(path, A.FS, (x * (10.0 ** (gain_db / 20.0))).astype(np.float32))
    return path


def run(subset, gains, binpath, os_factor, bands):
    files = {}
    for p, parsed in C.find_captures():
        if parsed["label"] in subset:
            files[parsed["label"]] = (p, parsed)
    missing = [s for s in subset if s not in files]
    if missing:
        sys.stderr.write(f"  ! missing captures: {', '.join(missing)}\n")

    orig = A.load_mono(A.ORIG)
    out = {}
    with tempfile.TemporaryDirectory() as td:
        for g in gains:
            ref_path = scaled_reference(g, td)
            rows = {}
            with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
                futs = {ex.submit(analyse, p, parsed, orig, ref_path, binpath, os_factor, bands): cid
                        for cid, (p, parsed) in files.items()}
                for f in cf.as_completed(futs):
                    r = f.result()
                    if r:
                        rows[futs[f]] = r
            out[f"{g:+g}"] = rows
    return out


def print_table(rows, title, base=None):
    print(f"\n=== {title} ===")
    print(f"{'capture':<15}{'plug Hz':>9}{'ped Hz':>8}{'oct':>8}{'tilt dB':>9}{'lo':>7}{'hi':>7}"
          + ("     d(oct)  d(tilt)" if base else ""))
    order = [c for c in DEFAULT_SUBSET if c in rows]
    for c in order:
        r = rows[c]
        line = (f"{c:<15}{r['plug_hz']:9.0f}{r['ped_hz']:8.0f}{r['oct']:+8.2f}"
                f"{r['tilt_db']:+9.2f}{r['thd_lo_db']:+7.2f}{r['thd_hi_db']:+7.2f}")
        if base and c in base:
            line += f"   {r['oct'] - base[c]['oct']:+8.2f}{r['tilt_db'] - base[c]['tilt_db']:+9.2f}"
        print(line)
    for m in ("OD", "Dist", "Clean"):
        sub = [rows[c] for c in order if c.endswith(m)]
        if sub:
            o = np.array([r["oct"] for r in sub])
            t = np.array([r["tilt_db"] for r in sub])
            print(f"  {m:<6} mean oct {o.mean():+.2f}  rms {np.sqrt((o ** 2).mean()):.2f}"
                  f" | mean tilt {t.mean():+.2f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--modes", nargs="*", default=None, help="filter subset by mode name")
    ap.add_argument("--in-gain", nargs="*", type=float, default=[0.0],
                    help="input gain(s) in dB to probe (default 0)")
    ap.add_argument("--os", type=int, default=8, choices=(1, 2, 4, 8))
    ap.add_argument("--bin", default="build/PedalRender_artefacts/Release/PedalRender")
    ap.add_argument("--json", default=None)
    ap.add_argument("--compare", default=None)
    a = ap.parse_args()

    subset = DEFAULT_SUBSET
    if a.modes:
        subset = tuple(c for c in subset if any(c.endswith(m) for m in a.modes))

    bands = np.array(A.fractional_octave_freqs(), float)
    res = run(subset, a.in_gain, a.bin, a.os, bands)

    base = None
    if a.compare:
        with open(a.compare) as fh:
            base = json.load(fh)
    for g, rows in res.items():
        b = (base or {}).get(g)
        print_table(rows, f"P6 peak + compression tilt — input gain {g} dB", b)

    if a.json:
        with open(a.json, "w") as fh:
            json.dump(res, fh, indent=1)
        print(f"\nwrote {a.json}")


if __name__ == "__main__":
    main()
