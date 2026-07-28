#!/usr/bin/env python3
"""FR error SHAPE audit — curves and cross-tabs, not point-to-point deltas.

Reads analysis/reports/comprehensive_data.json only (no rendering, <1 s).

WHY THIS EXISTS. The project has now walked into the same aggregation trap three times
(Finding 3's "+-0.2 oct", P6's per-capture sign reading, and P4's 1 kHz-anchored tilt). Every one
was a real error hiding inside a summary that averaged over the axis it was indexed by. The FR
error here is indexed by DRIVE and SWEEP LEVEL *jointly*, so:

  - a median over all captures at one drive averages the levels away,
  - a median over all drives at one level averages the drives away,
  - and NEITHER shows the cell where both are extreme.

`cross` is therefore the load-bearing view; `tilt` and `curves` are context. Read `cross` first.

THE INSTRUMENT TO TRUST. `clean` restricts to the sweep_clean column and the Boost row -- the most
nearly-linear measurement in the set. On a driven sweep the H1 transfer estimator still lets some
harmonic energy through, so a mode that distorts more reads HOTTER at HF for reasons that are not
linear EQ. Do not fit a linear shelf to a driven-sweep tilt without checking it survives here.

Usage:
  shape_audit.py                     # all views
  shape_audit.py cross|tilt|curves|clean
  shape_audit.py cross --lo 80 --hi 5120
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fr_thd_audit as A  # noqa: E402

# 80 Hz because the sub-64 Hz band is a separate, phase-limited problem (P1); 5120 Hz because the
# captures are not trustworthy above ~8 kHz and the warp shelf lives at 6.5 kHz.
FIT_LO, FIT_HI = 80.0, 5120.0

# MonarchChannel::updateDriveShelf -- trebleDb = max(0, shelfMaxDb - shelfSlopeDb * drive01).
# Parsed live out of the header rather than hardcoded: v1.4 P7 RETIRED this instrument (both
# constants are now 0), and a stale copy here would have kept printing a lift that no longer
# exists -- exactly the drift fr_thd_audit.channel_consts() was written to prevent.
_K = A.channel_consts()
SHELF_MAX_DB, SHELF_SLOPE_DB = _K["shelfMaxDb"], _K["shelfSlopeDb"]


def treble_lift(drive01):
    return max(0.0, SHELF_MAX_DB - SHELF_SLOPE_DB * drive01)


def rows(d, bands, caps, lo, hi):
    """(err_curve, meta) per capture x sweep, plus the band selection."""
    sel = np.array([i for i, b in enumerate(bands) if lo <= b <= hi])
    E, meta = [], []
    for c in sorted(caps, key=A.sort_key):
        for sw in d["meta"]["all_sweep_levels"]:
            f = c["fr"][sw]
            E.append(np.array(f["plugin_db"], float) - np.array(f["pedal_db"], float))
            meta.append(dict(mode=c["rev"], drive=c["settings"]["drive"],
                             tone=c["settings"]["tone"], sw=sw, id=c["id"]))
    return np.array(E, float), meta, sel


def tilt_of(e, bands, sel):
    """LS slope in dB/octave over the selected bands (+ = plugin bright / bass-light)."""
    x = np.log2(bands[sel] / 1000.0)
    y = e[sel]
    ok = np.isfinite(y)
    if ok.sum() < 4:
        return np.nan
    M = np.vstack([np.ones(ok.sum()), x[ok]]).T
    coef, *_ = np.linalg.lstsq(M, y[ok], rcond=None)
    return float(coef[1])


def view_cross(d, bands, caps, lo, hi, out=sys.stdout):
    E, meta, sel = rows(d, bands, caps, lo, hi)
    lv = d["meta"]["all_sweep_levels"]
    oct_span = np.log2(hi / lo)
    print(f"\n=== FR error TILT by DRIVE x SWEEP LEVEL, {lo:.0f}-{hi:.0f} Hz "
          f"({oct_span:.1f} oct) ===", file=out)
    print("  total dB across the band; + = plugin bright. `lift` = the drive shelf's own treble\n"
          "  lift at that knob position, for comparison.", file=out)
    print(f"\n{'drive':<7}{'lift':>7}  " + "".join(f"{l.replace('sweep_', ''):>12}" for l in lv),
          file=out)
    for dv in sorted({m["drive"] for m in meta}):
        row = f"G{dv * 10:<6.0f}{treble_lift(dv):>7.2f}  "
        for l in lv:
            T = [tilt_of(E[j], bands, sel) for j, m in enumerate(meta)
                 if abs(m["drive"] - dv) < 1e-9 and m["sw"] == l]
            row += f"{np.median(T) * oct_span:>+12.2f}"
        print(row, file=out)


def view_clean(d, bands, caps, lo, hi, out=sys.stdout):
    E, meta, sel = rows(d, bands, caps, lo, hi)
    oct_span = np.log2(hi / lo)
    print(f"\n=== CLEAN sweep only, by DRIVE x MODE — the most linear measurement available ===",
          file=out)
    print(f"  total dB across {lo:.0f}-{hi:.0f} Hz. Boost is the trustworthy row.\n", file=out)
    print(f"{'drive':<7}{'lift':>7}   " + "".join(f"{m:>16}" for m in A.MODES), file=out)
    for dv in sorted({m["drive"] for m in meta}):
        row = f"G{dv * 10:<6.0f}{treble_lift(dv):>7.2f}   "
        for mo in A.MODES:
            T = [tilt_of(E[j], bands, sel) for j, m in enumerate(meta)
                 if abs(m["drive"] - dv) < 1e-9 and m["mode"] == mo and m["sw"] == "sweep_clean"]
            row += f"{np.median(T) * oct_span:>+16.2f}" if T else f"{'-':>16}"
        print(row, file=out)


def view_tilt(d, bands, caps, lo, hi, out=sys.stdout):
    E, meta, sel = rows(d, bands, caps, lo, hi)
    T = np.array([tilt_of(e, bands, sel) for e in E])
    keep = np.array([m["drive"] < 0.95 for m in meta])   # G10 is a separate residual
    print(f"\n=== tilt distribution, {lo:.0f}-{hi:.0f} Hz (dB/oct) ===", file=out)

    def line(name, m):
        v = T[m]
        print(f"  {name:<16} n={m.sum():3d}  median {np.median(v):+.3f}  "
              f"IQR [{np.percentile(v, 25):+.3f}, {np.percentile(v, 75):+.3f}]  "
              f"frac>0 {np.mean(v > 0):.2f}", file=out)

    line("ALL", np.ones(len(T), bool))
    line("excl G10", keep)
    for mo in A.MODES:
        line(mo, np.array([m["mode"] == mo for m in meta]) & keep)
    for sw in d["meta"]["all_sweep_levels"]:
        line(sw.replace("sweep_", ""), np.array([m["sw"] == sw for m in meta]) & keep)
    print("\n  NOTE: a tight, sign-consistent median here does NOT mean a fixed shelf is the right\n"
          "  instrument. Check `cross` before believing it — see the module docstring.", file=out)


def view_curves(d, bands, caps, lo, hi, out=sys.stdout):
    E, meta, _ = rows(d, bands, caps, lo, hi)
    print(f"\n=== median error curve (dB, + = plugin hot), full band ===", file=out)
    print(f"{'group':<22}{'n':>4}  " + " ".join(f"{b:>6.0f}" for b in bands), file=out)

    def line(name, m):
        print(f"{name:<22}{m.sum():>4}  "
              + " ".join(f"{v:+6.2f}" for v in np.nanmedian(E[m], 0)), file=out)

    line("ALL", np.ones(len(E), bool))
    for mo in A.MODES:
        line(mo, np.array([m["mode"] == mo for m in meta]))
    for sw in d["meta"]["all_sweep_levels"]:
        line(sw.replace("sweep_", ""), np.array([m["sw"] == sw for m in meta]))
    for dv in sorted({m["drive"] for m in meta}):
        line(f"drive G{dv * 10:.0f}", np.array([abs(m["drive"] - dv) < 1e-9 for m in meta]))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("view", nargs="?", default="all",
                    choices=["all", "cross", "tilt", "curves", "clean"])
    ap.add_argument("--lo", type=float, default=FIT_LO)
    ap.add_argument("--hi", type=float, default=FIT_HI)
    a = ap.parse_args()

    d, bands, caps = A.load()
    bands = np.asarray(bands, float)
    if a.view in ("all", "cross"):
        view_cross(d, bands, caps, a.lo, a.hi)
    if a.view in ("all", "clean"):
        view_clean(d, bands, caps, a.lo, a.hi)
    if a.view in ("all", "tilt"):
        view_tilt(d, bands, caps, a.lo, a.hi)
    if a.view in ("all", "curves"):
        view_curves(d, bands, caps, a.lo, a.hi)


if __name__ == "__main__":
    main()
