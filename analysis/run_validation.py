#!/usr/bin/env python3
"""Reference-validation orchestrator: render the plugin at each capture's settings, A/B + null
against the real NAM captures, and emit analysis/VALIDATION_REPORT.md with headline figures.

For each capture in Pedal_export/ it parses the settings from the filename (e.g. "G6 T5 OD" ->
drive 0.6, tone 0.5, mode Overdrive), renders the plugin with tools/PedalRender on the SAME v2
test signal, then:
  - nulls (null_test.best_null: integer+fractional align + LS gain) on each broadband segment,
    per band, since the captures are level-normalised per mode;
  - runs the analyze.py frequency-response and THD-by-band metrics.

The captures MUST have been recorded against the current analysis/test_signal_48k.wav (v2). If
they were made against an older signal the segment timings won't line up — recapture first.

Usage:
  run_validation.py [--captures DIR] [--render-dir DIR] [--limit N]
Writes analysis/VALIDATION_REPORT.md and prints the headline null range.
"""
import argparse
import os
import re
import subprocess
import sys
import numpy as np

import gen_test_signal as gts
import analyze
from null_test import best_null, null_db, BANDS

FS = gts.FS
TIMES = gts.segment_times()
MODE_MAP = {"clean": 0, "boost": 0, "od": 1, "overdrive": 1, "dist": 2, "distortion": 2}
NULL_SEGMENTS = ["sweep_clean", "sweep_drv_-18", "sweep_drv_-12", "sweep_drv_-6"]


def find_pedalrender():
    for p in ("build/PedalRender_artefacts/Release/PedalRender",
              "build/PedalRender_artefacts/PedalRender", "build/PedalRender"):
        if os.path.exists(p):
            return p
    sys.exit("PedalRender not found — build it: cmake --build build --target PedalRender")


def parse_settings(fname):
    """'G6 T5 OD...' -> (drive, tone, clip_int, label) ; None if unparseable."""
    m = re.match(r"G([\d.]+)\s*T([\d.]+)\s*(Clean|OD|Overdrive|Dist|Distortion|Boost)", fname,
                 re.IGNORECASE)
    if not m:
        return None
    drive = float(m.group(1)) / 10.0
    tone = float(m.group(2)) / 10.0
    clip = MODE_MAP[m.group(3).lower()]
    return drive, tone, clip, f"G{m.group(1)} T{m.group(2)} {m.group(3)}"


def render(pr, drive, tone, clip, out):
    # Yellow-only render at fixed volume=0.5, presence=0.0 (matches the capture knob positions).
    subprocess.run([pr, "analysis/test_signal_48k.wav", out, str(drive), str(tone), "0.5", "0.0",
                    str(clip)], check=True, capture_output=True)


def seg_samples(x, name, guard=0.15):
    t0, t1 = TIMES[name]
    return x[int((t0 + guard) * FS):int((t1 - guard) * FS)]


def null_segment(real, plug, name):
    c, p = seg_samples(real, name), seg_samples(plug, name)
    n = min(len(c), len(p))
    r, g = best_null(c[:n], p[:n])
    overall = null_db(c[:n], r)
    bands = {nm: null_db(c[:n], r, (lo, hi)) for nm, lo, hi in BANDS}
    return overall, bands, 20 * np.log10(abs(g) + 1e-20)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--captures", default="analysis/Pedal_export")
    ap.add_argument("--render-dir", default="/tmp/monarch_renders")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--out", default="analysis/VALIDATION_REPORT.md")
    args = ap.parse_args()

    pr = find_pedalrender()
    os.makedirs(args.render_dir, exist_ok=True)
    caps = sorted(f for f in os.listdir(args.captures) if f.lower().endswith(".wav"))
    if args.limit:
        caps = caps[:args.limit]

    results = []
    for fn in caps:
        s = parse_settings(fn)
        if not s:
            print(f"  skip (unparseable): {fn}")
            continue
        drive, tone, clip, label = s
        real = analyze.load_mono(os.path.join(args.captures, fn))
        out = os.path.join(args.render_dir, label.replace(" ", "_") + ".wav")
        render(pr, drive, tone, clip, out)
        plug = analyze.load_mono(out)
        plug_m, _ = analyze.level_match(real, plug)

        seg_nulls = {}
        for name in NULL_SEGMENTS:
            try:
                seg_nulls[name] = null_segment(real, plug, name)
            except Exception as e:                       # noqa: BLE001 - segment may be absent
                seg_nulls[name] = (float("nan"), {}, float("nan"))
        _, fr_rms, fr_max = analyze.frequency_response(real, plug_m)
        best = min((v[0] for v in seg_nulls.values() if not np.isnan(v[0])), default=float("nan"))
        results.append(dict(label=label, clip=clip, seg=seg_nulls, fr_rms=fr_rms,
                            fr_max=fr_max, best=best))
        print(f"  {label:18} best null {best:+6.1f} dB | FR err {fr_rms:.2f} dB")

    write_report(args.out, results)
    valid = [r["best"] for r in results if not np.isnan(r["best"])]
    if valid:
        print(f"\nHEADLINE: null depth {min(valid):.1f} to {max(valid):.1f} dB "
              f"(median {np.median(valid):.1f}) across {len(valid)} captures")
        print(f"Report written: {args.out}")


def write_report(path, results):
    lines = ["# Monarch of Tone — Reference Validation Report", "",
             "Auto-generated by `analysis/run_validation.py`. Plugin rendered at each capture's",
             "settings via `tools/PedalRender` on the v2 test signal, then nulled (sub-sample",
             "aligned + per-mode LS gain) and A/B'd against the real King of Tone NAM captures.",
             "Null depth is the headline match metric (more negative = closer to the hardware).",
             ""]
    valid = [r["best"] for r in results if not np.isnan(r["best"])]
    if valid:
        lines += [f"**Best-segment null depth: {min(valid):.1f} to {max(valid):.1f} dB "
                  f"(median {np.median(valid):.1f} dB) across {len(valid)} captures.**", ""]
    lines += ["## Per-capture summary", "",
              "| Capture | Best null (dB) | FR RMS err (dB) | "
              + " | ".join(s.replace("sweep_", "") for s in NULL_SEGMENTS) + " |",
              "|" + "---|" * (3 + len(NULL_SEGMENTS))]
    for r in results:
        segcols = " | ".join(
            f"{r['seg'][s][0]:+.1f}" if not np.isnan(r['seg'][s][0]) else "—"
            for s in NULL_SEGMENTS)
        lines.append(f"| {r['label']} | {r['best']:+.1f} | {r['fr_rms']:.2f} | {segcols} |")
    lines += ["", "## Per-band null (driven −12 dB sweep)", "",
              "| Capture | " + " | ".join(b[0] for b in BANDS) + " |",
              "|" + "---|" * (1 + len(BANDS))]
    for r in results:
        b = r["seg"].get("sweep_drv_-12", (None, {}, None))[1]
        cols = " | ".join(f"{b[nm]:+.1f}" if nm in b else "—" for nm, _, _ in BANDS)
        lines.append(f"| {r['label']} | {cols} |")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
