#!/usr/bin/env python3
"""P3.1 prerequisite — is the pedal's H4/H6 target above the capture chain's noise floor?

FR_THD_AUDIT.md P3.1 asks for this before any fitting: several pedal-side H6 targets sit at
-60..-67 dBc, and P0 characterised the FR/THD trust bands but never the harmonic floor itself.
Fitting the plugin to a number that is really the NAM capture chain's noise would be worse than
leaving H6 alone.

METHOD — read the floor out of the SAME instrument that reads the harmonics. Farina's deconvolved
IR puts the N-th harmonic's impulse at a pre-delay dt_N = T*ln(N)/ln(f1/f0). Between two adjacent
harmonic impulses there is nothing but noise + deconvolution residue, and it is gated, windowed and
FFT'd by exactly the same code path. So gating at a FRACTIONAL "order" (2.5, 3.5, ... 6.5) with the
same window rule yields a per-band floor in the same units as the harmonic reading: dB re the
fundamental, at the same anchor frequencies. A harmonic is trustworthy when it stands clear of the
pseudo-order rows that bracket it.

Second, independent check: a real harmonic TRACKS DRIVE (it grows from the -18 sweep to the -6
sweep and with the drive knob); a floor reading does not move. Both views are printed.

Usage:
  p31_harm_floor.py                          # default subset (OD/Dist mid+high gain, all levels)
  p31_harm_floor.py --captures all
  p31_harm_floor.py --anchors 100 200 400 800
  p31_harm_floor.py --json OUT.json
"""
import argparse
import concurrent.futures as cf
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import analyze as A               # noqa: E402
import captures as C              # noqa: E402
import comprehensive_report as R  # noqa: E402
import gen_test_signal as gts     # noqa: E402

DEFAULT_SUBSET = (
    "G5 T5 OD", "G7 T5 OD", "G10 T5 OD", "G5 T5 Dist", "G7 T5 Dist", "G10 T5 Dist",
    "G5 T5 Clean", "G10 T5 Clean",
)
SWEEPS = R.ALL_SWEEP_LEVELS
ORDERS = (2, 3, 4, 5, 6, 7)
# Pseudo-orders: each sits between two real harmonic impulses, so it measures what contaminates
# them. 6.5 brackets H6 from above, 5.5 from below.
PSEUDO = (2.5, 3.5, 4.5, 5.5, 6.5)


def gated_orders(sweep, ref, orders, anchors):
    """dB re fundamental at each anchor for each (possibly fractional) order.

    Deconvolution + gating copied from analyze.harmonic_thd_curve so the numbers are directly
    comparable to `fr_thd_audit.py harm`; the only change is that `order` may be fractional.
    """
    n = min(len(sweep), len(ref))
    y = sweep[:n].astype(np.float64)
    x = ref[:n].astype(np.float64)
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    X = np.fft.rfft(x, nfft)
    Y = np.fft.rfft(y, nfft)
    eps = 1e-6 * np.mean(np.abs(X) ** 2)
    ir = np.fft.irfft(Y * np.conj(X) / (np.abs(X) ** 2 + eps), nfft)
    T_sweep = n / A.FS
    Rlog = np.log(gts.SWEEP_F1 / gts.SWEEP_F0)

    def gated(order):
        dt = T_sweep * np.log(order) / Rlog
        center = int(round((-dt) * A.FS)) % nfft
        if order == 1:
            half = int(0.04 * A.FS)
        else:
            gap = (T_sweep / Rlog) * np.log((order + 1) / order)
            half = int(0.35 * gap * A.FS)
        half = max(half, int(0.01 * A.FS))
        idx = (np.arange(center - half, center + half) % nfft)
        spec = np.fft.rfft(ir[idx] * np.hanning(len(idx)), nfft)
        return np.fft.rfftfreq(nfft, 1 / A.FS), np.abs(spec)

    fr, H1 = gated(1)
    out = {}
    for o in orders:
        frN, mag = gated(o)
        H = np.interp(fr, frN / o, mag, left=0.0, right=0.0)
        vals = []
        for ahz in anchors:
            i = int(np.argmin(np.abs(fr - ahz)))
            measurable = o * fr[i] <= gts.SWEEP_F1 * A.ORDER_LIMIT_MARGIN
            vals.append(float(20 * np.log10(H[i] / (H1[i] + 1e-20) + 1e-20)) if measurable
                        else None)
        out[o] = vals
    return out


def analyse(path, parsed, orig, anchors):
    cached = R.get_pedal_features(path, orig, R.CACHE_DIR, True)
    if cached is None:
        return None
    cap_al, _ = cached
    rows = {}
    for sw in SWEEPS:
        rows[sw] = gated_orders(A.seg_of(cap_al, sw), A.seg_of(orig, sw),
                                tuple(ORDERS) + PSEUDO, anchors)
    return {"id": parsed["label"], "rev": parsed["rev"], "sweeps": rows}


def fmt(v):
    return "   na " if v is None else f"{v:>6.1f}"


def print_levels(results, anchors, out=sys.stdout):
    """Per capture: real orders interleaved with the pseudo-order floor rows."""
    print("\n=== Pedal harmonics vs interleaved noise floor (dB re fundamental) ===", file=out)
    print("    rows marked * are PSEUDO-orders — gated between harmonic impulses, i.e. the "
          "floor.\n", file=out)
    for r in results:
        print(f"--- {r['id']}", file=out)
        hdr = f"{'order':>7}"
        for sw in SWEEPS:
            hdr += f"  {sw.replace('sweep_', ''):>{7 * len(anchors)}}"
        print(hdr, file=out)
        print(f"{'':>7}" + "".join("  " + "".join(f"{a:>7}" for a in anchors) for _ in SWEEPS),
              file=out)
        for o in sorted(tuple(ORDERS) + PSEUDO):
            label = (f"H{o:g}" if float(o).is_integer() else f"*{o:g}")
            row = f"{label:>7}"
            for sw in SWEEPS:
                row += "  " + "".join(fmt(v) for v in r["sweeps"][sw][o])
            print(row, file=out)
        print(file=out)


def print_margins(results, anchors, out=sys.stdout):
    """The decision table: how far each even order stands above its local floor."""
    print("=== Margin above local floor (dB) — even orders, driven sweeps ===", file=out)
    print("    floor = max of the two bracketing pseudo-orders (e.g. H6 vs *5.5,*6.5).", file=out)
    print("    margin < ~6 dB = the target is contaminated; < ~3 dB = it IS the floor.\n", file=out)
    brackets = {2: (2.5,), 4: (3.5, 4.5), 6: (5.5, 6.5)}
    print(f"{'capture':<14}{'sweep':>10}{'ord':>5}" + "".join(f"{a:>8}" for a in anchors)
          + "   (value / floor / margin)", file=out)
    stats = {o: [] for o in brackets}
    for r in results:
        for sw in R.DRIVEN_SWEEPS:
            for o, br in brackets.items():
                row = f"{r['id']:<14}{sw.replace('sweep_', ''):>10}{'H' + str(o):>5}"
                for ai in range(len(anchors)):
                    v = r["sweeps"][sw][o][ai]
                    fl = [r["sweeps"][sw][b][ai] for b in br]
                    fl = [f for f in fl if f is not None]
                    if v is None or not fl:
                        row += f"{'na':>8}"
                        continue
                    floor = max(fl)
                    row += f"{v - floor:>8.1f}"
                    stats[o].append((r["id"], sw, anchors[ai], v, floor, v - floor))
                print(row, file=out)
    print(file=out)
    print("--- summary: margin distribution per even order (driven sweeps, all anchors)", file=out)
    for o, s in stats.items():
        if not s:
            continue
        m = np.array([x[5] for x in s])
        print(f"  H{o}: n={len(m):>3}  median {np.median(m):>+6.1f}  min {m.min():>+6.1f}  "
              f"max {m.max():>+6.1f}   below 6 dB: {int((m < 6).sum())}/{len(m)}   "
              f"below 3 dB: {int((m < 3).sum())}/{len(m)}", file=out)


def print_tracking(results, anchors, out=sys.stdout):
    """Second, independent test: a real harmonic grows with sweep level; a floor does not."""
    print("\n=== Level tracking: does the reading follow drive? (dB, -6 sweep minus -18) ===",
          file=out)
    print("    a real harmonic rises with level; a noise floor is flat (and rides the "
          "fundamental down).\n", file=out)
    print(f"{'capture':<14}{'ord':>5}" + "".join(f"{a:>8}" for a in anchors), file=out)
    for r in results:
        for o in sorted(tuple(ORDERS) + PSEUDO):
            label = (f"H{o:g}" if float(o).is_integer() else f"*{o:g}")
            row = f"{r['id']:<14}{label:>5}"
            for ai in range(len(anchors)):
                a6 = r["sweeps"]["sweep_drv_-6"][o][ai]
                a18 = r["sweeps"]["sweep_drv_-18"][o][ai]
                row += f"{'na':>8}" if (a6 is None or a18 is None) else f"{a6 - a18:>+8.1f}"
            print(row, file=out)
        print(file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--captures", nargs="*", default=list(DEFAULT_SUBSET))
    ap.add_argument("--anchors", nargs="*", type=int, default=list(R.THD_ANCHORS) + [800])
    ap.add_argument("--json", default=None)
    ap.add_argument("--jobs", type=int, default=None)
    a = ap.parse_args()

    orig = A.load(A.ORIG)
    found = C.find_captures()
    wanted = None if a.captures == ["all"] else set(a.captures)
    todo = [(p, q) for p, q in found if wanted is None or q["label"] in wanted]
    if not todo:
        sys.exit("no captures matched")

    results = []
    with cf.ThreadPoolExecutor(max_workers=a.jobs or R.default_jobs()) as ex:
        futs = {ex.submit(analyse, p, q, orig, a.anchors): q["label"] for p, q in todo}
        for f in cf.as_completed(futs):
            r = f.result()
            if r:
                results.append(r)
    results.sort(key=lambda r: (r["rev"], r["id"]))

    print_levels(results, a.anchors)
    print_margins(results, a.anchors)
    print_tracking(results, a.anchors)
    if a.json:
        json.dump(results, open(a.json, "w"), indent=1)
        print(f"\nwrote {a.json}")


if __name__ == "__main__":
    main()
