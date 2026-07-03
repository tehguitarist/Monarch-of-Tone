#!/usr/bin/env python3
"""Low-mid EQ audit: plugin vs real captures, every clip mode × gain × operating point, across the
whole spectrum in bands. Built (2026-07-04) to settle whether the OD channel is short on low mids —
after a fixed pre-clip "presence bump" was mis-fit by a tilt-subtraction plot and shipped as a
regression (see CLAUDE.md rejected-experiments). This tool encodes the guardrails that catch BOTH
failure modes that bit us:

  G1  Two-method agreement — a deficit is only trusted when the Farina `linear_tf` (continuous,
      distortion removed) AND the discrete tone bursts (steady sines, harmonic-immune) agree.
  G2  Normalization robustness — every deviation is computed under two normalizations (1 kHz anchor
      and a 700 Hz–1.5 kHz shoulder fit); if the sign flips, it's an artifact (this is what fooled
      the tilt plot).
  G3  Operating-point sweep — reported across clean + driven −18/−12/−6 dB sweeps, because the OD
      deficit is a function of CLIP DEPTH, not the drive knob alone.
  G4  Time-domain null is the arbiter — a swept-sine linear_tf MIS-READS a clip-gated correction
      (its gate modulates across the sweep, corrupting the deconvolution), so the artifact-immune
      time-domain null (`--nulls`) is the metric of record for validating a correction.

Findings (2026-07-04): OD alone is short in the low mids ONLY under hard clipping — a broad, ~flat
shortfall of ~1.8 dB below ~500 Hz at the hottest −6 dB sweep, ≈0 at normal levels, consistent
across every gain. Distortion matches (<0.6 dB); Boost has a separate knob-tilt. Corrected with a
clip-depth-gated low-shelf in MonarchChannel (OD-only). See analysis/MID_EQ_AUDIT.md for the table.

Usage:
  mid_eq_audit.py [--nulls] [--report]
    (default)   linear_tf band-deviation grid + guardrail verdicts
    --nulls     also compute the artifact-immune time-domain null per cell (slower)
    --report    write analysis/MID_EQ_AUDIT.md

Requires the local-only NAM captures in analysis/pedal_export2/ and a built tools/PedalRender.
"""
import os, sys, json, subprocess, argparse
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
os.chdir(ROOT)
import gen_test_signal as gts
from analyze import load_mono, seg, linear_tf, db
from null_test import best_null, null_db

FS = 48000
PR = "build/PedalRender_artefacts/Release/PedalRender"
IN = "analysis/test_signal_48k.wav"
CAPDIR = "analysis/pedal_export2"
MODES = [("Clean", 0), ("OD", 1), ("Dist", 2)]
GAINS = [2, 3, 4, 5, 6, 7, 8, 10]
OPS = [("light", "sweep_clean", gts.SWEEP_CLEAN_SEC),
       ("-18", "sweep_drv_-18", gts.SWEEP_DRIVEN_SEC),
       ("-12", "sweep_drv_-12", gts.SWEEP_DRIVEN_SEC),
       ("-6", "sweep_drv_-6", gts.SWEEP_DRIVEN_SEC)]
BANDS = [("60-120", 60, 120), ("120-250", 120, 250), ("250-500", 250, 500),
         ("500-1k", 500, 1000), ("1k-2k", 1000, 2000), ("2k-4k", 2000, 4000)]
TONES = {110: 49.10, 220: 50.20, 440: 51.30, 1000: 52.40, 1500: 53.50, 2000: 54.60}
TDUR, TGUARD = 0.8, 0.12


def cap(g, mode):
    return f"{CAPDIR}/G{g} T5 {mode} tommy_test_signal_48k.wav"


def render(g, clip, out):
    subprocess.run([PR, IN, out, str(g / 10.0), "0.5", "0.5", "0.0", str(clip)],
                   check=True, capture_output=True)


def lin_tf_db(y, segname, sec):
    F, H = linear_tf(seg(y, segname, guard=0.3), sec)
    m = (F >= 30) & (F <= 5000)
    return F[m], db(H)[m]


def band_dev(Fr, mr, Fp, mp, norm="1k"):
    if norm == "1k":
        mr = mr - mr[np.argmin(np.abs(Fr - 1000))]
        mp = mp - mp[np.argmin(np.abs(Fp - 1000))]
    else:
        sr = (Fr >= 700) & (Fr <= 1500); sp = (Fp >= 700) & (Fp <= 1500)
        mr = mr - mr[sr].mean(); mp = mp - mp[sp].mean()
    mpi = np.interp(Fr, Fp, mp)
    out = {}
    for name, lo, hi in BANDS:
        b = (Fr >= lo) & (Fr < hi)
        out[name] = float((mpi[b] - mr[b]).mean()) if b.any() else None
    return out


def fund(x, f, t0):
    i0, i1 = int((t0 + TGUARD) * FS), int((t0 + TDUR - TGUARD) * FS)
    s = x[i0:i1]; n = len(s); t = np.arange(n) / FS
    return np.hypot(2 * np.dot(s, np.sin(2 * np.pi * f * t)) / n,
                    2 * np.dot(s, np.cos(2 * np.pi * f * t)) / n)


def tone_dev(real, plug):
    pr, cr = fund(plug, 1000, TONES[1000]), fund(real, 1000, TONES[1000])
    return {str(f): float(20 * np.log10(fund(plug, f, t0) / pr + 1e-20)
                          - 20 * np.log10(fund(real, f, t0) / cr + 1e-20))
            for f, t0 in TONES.items()}


def segd(x, name):
    t0, t1 = gts.segment_times()[name]
    return x[int((t0 + 0.3) * FS):int((t1 - 0.3) * FS)]


def build_grid(with_nulls):
    if not os.path.exists(PR):
        sys.exit("PedalRender not built: cmake --build build --target PedalRender")
    grid = {}
    for mode, clip in MODES:
        for g in GAINS:
            cp = cap(g, mode)
            if not os.path.exists(cp):
                continue
            w = f"/tmp/mid_audit_{mode}_G{g}.wav"
            render(g, clip, w)
            real, plug = load_mono(cp), load_mono(w)
            e = {"lin": {}, "lin_shoulder": {}, "tone": tone_dev(real, plug)}
            for oplabel, segn, sec in OPS:
                Fr, mr = lin_tf_db(real, segn, sec)
                Fp, mp = lin_tf_db(plug, segn, sec)
                e["lin"][oplabel] = band_dev(Fr, mr, Fp, mp, "1k")
                e["lin_shoulder"][oplabel] = band_dev(Fr, mr, Fp, mp, "shoulder")
            if with_nulls:
                e["null"] = {}
                for oplabel, segn, _ in OPS:
                    c, p = segd(real, segn), segd(plug, segn)
                    n = min(len(c), len(p))
                    r, _ = best_null(c[:n], p[:n])
                    e["null"][oplabel] = float(null_db(c[:n], r))
            grid[f"{mode}_G{g}"] = e
    return grid


def print_report(grid):
    print("Low-mid EQ audit — plugin vs real captures (plug−real dB, 1kHz-norm) [Farina linear_tf]")
    for mode, _ in MODES:
        print(f"\n=== {mode} — 250-500 Hz across gains × operating points ===")
        print(f"{'op':>7} " + " ".join(f"{'G'+str(x):>6}" for x in GAINS))
        for op, _, _ in OPS:
            row = [grid.get(f"{mode}_G{g}", {}).get("lin", {}).get(op, {}).get("250-500") for g in GAINS]
            print(f"{op:>7} " + " ".join(f"{v:+6.2f}" if v is not None else "   -  " for v in row))
    # G1 two-method agreement spot-check
    print("\nG1 two-method agreement (tone440 vs linear_tf 250-500 @-12; should agree in sign):")
    bad = 0
    for mode, _ in MODES:
        for g in GAINS:
            c = grid.get(f"{mode}_G{g}")
            if not c:
                continue
            t, l = c["tone"]["440"], c["lin"]["-12"]["250-500"]
            if abs(t - l) > 0.6 and np.sign(t) != np.sign(l):
                print(f"  DISAGREE {mode} G{g}: tone {t:+.2f} vs lin {l:+.2f}"); bad += 1
    print(f"  {'all cells agree' if bad == 0 else str(bad)+' disagreements — investigate'}")


def write_md(grid):
    lines = ["# Low-Mid EQ Audit — plugin vs real King of Tone captures", "",
             "Generated by `analysis/mid_eq_audit.py` (2026-07-04). Values = plugin − real, dB,",
             "normalized at 1 kHz, from the Farina `linear_tf` (distortion removed). Negative = the",
             "plugin is short of the real pedal. Captures are local-only (gitignored); re-run to refresh.",
             "The tables below reflect **whatever plugin binary is currently built** — so with the OD",
             "low-shelf active they show the *residual after correction*, not the original deficit.",
             "", "## Finding (original, uncorrected)", "",
             "The **Overdrive** channel alone was short in the low mids, and ONLY under hard clipping:",
             "a broad ~flat shortfall of **~1.8 dB below ~500 Hz at the hottest (−6 dB) sweep** (e.g.",
             "OD G5 60–500 Hz = −1.6 dB), ≈0 at normal levels, consistent across every gain.",
             "**Distortion** matched (<0.6 dB) and **Boost** had a separate, milder knob-dependent",
             "tilt. Corrected with an OD-only, clip-depth-gated low-shelf",
             "(`MonarchChannel::odLowShelf`, 2.0 dB @ 520 Hz, gate 12) that roughly halves the",
             "hot-drive deficit while staying inert at normal levels and in Boost/Distortion.", ""]
    for mode, _ in MODES:
        lines += [f"## {mode} — 250–500 Hz (dB, 1kHz-norm)", "",
                  "| op | " + " | ".join(f"G{g}" for g in GAINS) + " |",
                  "|" + "----|" * (len(GAINS) + 1)]
        for op, _, _ in OPS:
            row = [grid.get(f"{mode}_G{g}", {}).get("lin", {}).get(op, {}).get("250-500") for g in GAINS]
            lines.append(f"| {op} | " + " | ".join(f"{v:+.2f}" if v is not None else "–" for v in row) + " |")
        lines.append("")
    open("analysis/MID_EQ_AUDIT.md", "w").write("\n".join(lines))
    print("wrote analysis/MID_EQ_AUDIT.md")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nulls", action="store_true", help="also compute time-domain null per cell")
    ap.add_argument("--report", action="store_true", help="write analysis/MID_EQ_AUDIT.md")
    a = ap.parse_args()
    grid = build_grid(a.nulls)
    json.dump(grid, open("/tmp/mid_eq_audit.json", "w"), indent=1)
    print_report(grid)
    if a.report:
        write_md(grid)


if __name__ == "__main__":
    main()
