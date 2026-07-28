#!/usr/bin/env python3
"""Offline null probe — score a candidate EQ change on the ARBITER without rebuilding.

Applies a candidate filter to the EXISTING plugin renders and re-nulls them against the captures.
This is P1's own methodology, and it is the cheap way to test an EQ hypothesis that FR generated:
FR generates, the null decides. A full rebuild + comprehensive_report.py is minutes; this is
seconds, so it belongs BEFORE you touch MonarchChannel.h, not after.

Needs a directory of plugin renders named after the captures (e.g. `G5_T5_OD.wav` against
`analysis/pedal_export2/G5 T5 OD tommy_test_signal_48k.wav`). Produce them with:

    python3 analysis/comprehensive_report.py --keep-renders /tmp/monarch_renders

Views:
  transfer  complex pedal/plugin transfer D(f) = magnitude AND PHASE at low frequency, per
            capture, referenced to the 200 Hz-2 kHz plateau. This is what P1 needed and did not
            have per-band: the sign of the phase decides whether a minimum-phase filter is even
            the right instrument. Positive = the pedal LEADS = no causal min-phase EQ can match it.
  shelf     grid-search a min-phase low-shelf scored on the COMPLEX residual |D/S - 1|, weighted
            equal-energy-per-octave (what an ESS actually delivers). Scoring magnitude alone is
            what produced P1's two rejected shelves — both were fit to zero the FR error and were
            roughly twice as deep as the complex optimum.
  null      apply candidate filters to every render and re-null on the FULL sweep segments, so the
            20-40 Hz region is actually inside the scored window. Breaks the result out by DRIVE
            and MODE, because a knob-indexed result means a fixed filter is the wrong instrument.

TWO STANDING CAVEATS, both load-bearing:
  1. PLACEMENT. This filters the render's OUTPUT. The shelves it emulates live PRE-CLIP in
     MonarchChannel::processPre. For Boost/clean the two are near-equivalent; for hard-clipped
     modes they are not. Direction and rough magnitude transfer; exact constants do not.
  2. DRIVEN-SWEEP NULLS REWARD DULLING. On a driven sweep the null is partly matching distortion
     products, so an HF cut can deepen the null while the LINEAR response gets worse. When the
     `null` and `transfer`/FR views disagree about HF, believe the clean sweep.

Usage:
  offline_null_probe.py transfer
  offline_null_probe.py shelf --band 30 120
  offline_null_probe.py null --low-shelf 100:1.0 --low-shelf 100:1.65
  offline_null_probe.py null --high-shelf 2000:-1.95 --renders /tmp/monarch_renders
"""
import argparse
import os
import sys

import numpy as np
import scipy.signal as sps
from scipy.signal import lfilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A          # noqa: E402
import fr_thd_audit as F     # noqa: E402
import null_test as N        # noqa: E402

FS = 48000
DEFAULT_RENDERS = "/tmp/monarch_renders"
CAP_DIR = "analysis/pedal_export2"
SWEEPS = ["sweep_clean", "sweep_drv_-18", "sweep_drv_-12", "sweep_drv_-6"]
LF_PROBE = [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200]


def _ba(glo, ghi, pivot):
    b0, b1, a1 = F._shelf(glo, ghi, pivot, FS)
    return np.array([b0, b1]), np.array([1.0, a1])


def low_shelf(fc, db):
    """Boost of `db` below `fc`, unity above — the P1-class LF correction."""
    return _ba(10 ** (db / 20.0), 1.0, fc)


def high_shelf(fc, db):
    """`db` above `fc`, unity below — the hfTrim-class correction."""
    return _ba(1.0, 10 ** (db / 20.0), fc)


def shelf_H(ba, f):
    b, a = ba
    z = np.exp(-2j * np.pi * f / FS)
    return (b[0] + b[1] * z) / (a[0] + a[1] * z)


def load_pairs(render_dir, orig):
    items = []
    for fn in sorted(os.listdir(render_dir)):
        if not fn.endswith(".wav"):
            continue
        lab = os.path.splitext(fn)[0]
        cp = os.path.join(CAP_DIR, f"{lab.replace('_', ' ')} tommy_test_signal_48k.wav")
        if not os.path.exists(cp):
            sys.stderr.write(f"  ! no capture for {lab}\n")
            continue
        cap, _ = A.align(A.load(cp), orig)
        ren, _ = A.align(A.load(os.path.join(render_dir, fn)), orig)
        items.append(dict(label=lab, drive=float(lab.split("_")[0][1:]) / 10.0,
                          mode=lab.split("_")[2], cap=cap, ren=ren))
    return items


def ctf(out, inp):
    f, Pxy = sps.csd(inp, out, FS, nperseg=32768)
    f, Pxx = sps.welch(inp, FS, nperseg=32768)
    return f, Pxy / (Pxx + 1e-30)


def measure_D(items, orig, sweep="sweep_clean"):
    """Complex pedal/plugin transfer per capture, referenced to the 200 Hz-2 kHz plateau.

    The reference removes (a) the plateau's mean magnitude and (b) a LINEAR phase fit over the
    plateau — i.e. any residual sub-sample misalignment plus a constant rotation. It deliberately
    does NOT divide by the complex mean of D: where the plateau's own phase is incoherent (G10,
    whose midband is itself badly matched) that mean is small and rotates the whole curve by an
    arbitrary angle, which reads as a fake +-100 deg of LF phase. Captures whose midband does not
    match are not usable for this measurement at all — read the ones that do.
    """
    i = A.seg_of(orig, sweep)
    out = []
    for it in items:
        f, Hc = ctf(A.seg_of(it["cap"], sweep), i)
        _, Hr = ctf(A.seg_of(it["ren"], sweep), i)
        D = Hc / (Hr + 1e-30)
        m = (f >= 200) & (f <= 2000)
        mag = np.exp(np.mean(np.log(np.abs(D[m]) + 1e-30)))          # geometric mean magnitude
        ph = np.unwrap(np.angle(D))
        coef = np.polyfit(f[m], ph[m], 1)                            # delay + constant
        out.append((np.abs(D) / mag) * np.exp(1j * (ph - np.polyval(coef, f))))
    return f, np.array(out)


def view_transfer(items, orig, out=sys.stdout):
    f, Ds = measure_D(items, orig)
    print("\n=== D(f) = pedal / plugin, clean sweep. mag dB / phase deg (+ = pedal LEADS) ===",
          file=out)
    print("  A minimum-phase boost supplies LAG. Where the pedal LAGS (negative), a min-phase\n"
          "  low-shelf is the right instrument; where it LEADS, no causal min-phase EQ matches it.",
          file=out)
    print(f"\n{'capture':<15}" + "".join(f"{h:>13.0f}" for h in LF_PROBE), file=out)
    for it, D in zip(items, Ds):
        v = [np.interp(x, f, D.real) + 1j * np.interp(x, f, D.imag) for x in LF_PROBE]
        print(f"{it['label']:<15}"
              + "".join(f"{20 * np.log10(abs(z)):>+7.2f}/{np.degrees(np.angle(z)):>+5.0f}"
                        for z in v), file=out)


def view_shelf(items, orig, band, out=sys.stdout):
    f, Ds = measure_D(items, orig)

    def resid(bd, S=None):
        m = (f >= bd[0]) & (f <= bd[1])
        w = 1.0 / f[m]                       # equal energy per octave, as an ESS delivers
        DD = Ds[:, m] / (S[m] if S is not None else 1.0)
        return float(np.sqrt(np.sum(np.abs(DD - 1) ** 2 * w, axis=1).mean() / np.sum(w)))

    print(f"\n=== min-phase low-shelf fit on the COMPLEX residual, {band[0]:.0f}-{band[1]:.0f} Hz, "
          f"n={len(items)} ===", file=out)
    print(f"  baseline (no shelf): {resid(band):.4f}", file=out)
    cands = []
    for fc in np.arange(30, 301, 5.0):
        for db in np.arange(0, 4.01, 0.05):
            cands.append((resid(band, shelf_H(low_shelf(fc, db), f)), fc, db))
    cands.sort()
    r, fc, db = cands[0]
    print(f"  best: fc {fc:.0f} Hz {db:+.2f} dB -> {r:.4f}  "
          f"({100 * (1 - r / resid(band)):+.0f}% residual)", file=out)
    S = shelf_H(low_shelf(fc, db), f)
    for chk in [(20, 32), (32, 64), (64, 128), (128, 256)]:
        print(f"    sub-band {chk[0]:>3}-{chk[1]:<3} Hz: {resid(chk):.4f} -> {resid(chk, S):.4f}",
              file=out)


def view_null(items, cands, out=sys.stdout):
    sl = {s: slice(int((A.TIMES[s][0] + 0.3) * FS), int((A.TIMES[s][1] - 0.3) * FS))
          for s in SWEEPS}
    base = {}
    for it in items:
        for s in SWEEPS:
            r, _ = N.best_null(it["cap"][sl[s]], it["ren"][sl[s]])
            base[(it["label"], s)] = N.null_db(it["cap"][sl[s]], r)
    print(f"\n=== null on FULL sweep segments, {len(items)} captures x {len(SWEEPS)} levels ===",
          file=out)
    print(f"  baseline mean {np.mean(list(base.values())):.3f} dB\n", file=out)
    drives = sorted({it["drive"] for it in items})
    modes = sorted({it["mode"] for it in items})
    print(f"{'candidate':<22}{'meanD':>8}{'median':>8}{'deeper':>10}{'worst':>8}   "
          + "".join(f"{'G%g' % (d * 10):>7}" for d in drives)
          + "  |" + "".join(f"{m:>7}" for m in modes), file=out)
    for name, ba in cands:
        dl = {}
        for it in items:
            y = lfilter(ba[0], ba[1], it["ren"])
            for s in SWEEPS:
                r, _ = N.best_null(it["cap"][sl[s]], y[sl[s]])
                dl[(it["label"], s)] = N.null_db(it["cap"][sl[s]], r) - base[(it["label"], s)]
        v = np.array(list(dl.values()))
        per_d = [np.mean([x for k, x in dl.items()
                          if abs(next(i["drive"] for i in items if i["label"] == k[0]) - d) < 1e-9])
                 for d in drives]
        per_m = [np.mean([x for k, x in dl.items()
                          if next(i["mode"] for i in items if i["label"] == k[0]) == m])
                 for m in modes]
        print(f"{name:<22}{v.mean():>+8.3f}{np.median(v):>+8.3f}"
              f"{int((v < -0.005).sum()):>7d}/{len(v):<3d}{v.max():>+8.2f}   "
              + "".join(f"{x:>+7.2f}" for x in per_d)
              + "  |" + "".join(f"{x:>+7.2f}" for x in per_m), file=out, flush=True)


def parse_shelf(spec, kind):
    fc, db = spec.split(":")
    fc, db = float(fc), float(db)
    ba = low_shelf(fc, db) if kind == "low" else high_shelf(fc, db)
    return (f"{kind} {fc:.0f}Hz {db:+.2f}dB", ba)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("view", choices=["transfer", "shelf", "null"])
    ap.add_argument("--renders", default=DEFAULT_RENDERS)
    ap.add_argument("--band", nargs=2, type=float, default=[30.0, 120.0])
    ap.add_argument("--low-shelf", action="append", default=[], metavar="FC:DB")
    ap.add_argument("--high-shelf", action="append", default=[], metavar="FC:DB")
    ap.add_argument("--limit", type=int, default=0, help="only the first N captures (transfer/shelf)")
    a = ap.parse_args()

    if not os.path.isdir(a.renders):
        sys.exit(f"{a.renders} not found — run:\n"
                 f"  python3 analysis/comprehensive_report.py --keep-renders {a.renders}")
    orig = A.load(A.ORIG)
    items = load_pairs(a.renders, orig)
    if a.limit:
        items = items[:a.limit]

    if a.view == "transfer":
        view_transfer(items, orig)
    elif a.view == "shelf":
        view_shelf(items, orig, a.band)
    else:
        cands = ([parse_shelf(s, "low") for s in a.low_shelf]
                 + [parse_shelf(s, "high") for s in a.high_shelf])
        if not cands:
            sys.exit("null: pass at least one --low-shelf FC:DB or --high-shelf FC:DB")
        view_null(items, cands)


if __name__ == "__main__":
    main()
