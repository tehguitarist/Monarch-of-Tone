#!/usr/bin/env python3
"""P9 step 3 — the SW-1 output ceiling: admissibility test first, then the fit.

WHY THIS EXISTS. P9 steps 1-2 established the defect beyond doubt (FR_THD_AUDIT.md P9): in
Overdrive the pedal's output saturates with input level and the plugin's does not. The agreed step 3
is "fit the ceiling empirically as a soft limiter on the SW-1 path". This harness's FIRST job is not
to fit it — it is to ask whether a soft limiter on pin7 is an ADMISSIBLE instrument at all, because
this project has repeatedly fitted the wrong instrument to a correctly-measured defect (P1's
min-phase shelf, P4's fixed tilt, P6's mode-differentiated mechanism, P8's fixed low-shelf).

THE ADMISSIBILITY TEST (`static`). A memoryless waveshaper sitting on pin7 imposes ONE map from
pin7 level to output level, shared by every drive and every input level. The `comp` metric is
self-anchored per curve (the captures are per-mode level-normalised), so the map is only pinned up
to a per-drive constant. So: is there a single monotone f and per-drive offsets c_d with

    L_pedal(d, l)  ==  f( L_plugin(d, l) )  +  c_d          for every drive d, level l ?

If two cells from different drives land on nearly the SAME plugin level but demand very different
pedal levels (after removing the best per-drive offset), no static map on pin7 can fit them, and
step 3's proposed instrument is dead on arrival however it is parameterised.

`curves` prints the absolute rises the test is built from (plugin and pedal, dB re each curve's own
-30 dB step) so the arithmetic is auditable rather than buried.

Levels come from the renders/captures (the same segments `p9_od_compression.py comp` reads), and
the plugin's pin7 voltages come from the standalone probe (`probe`), which runs the real
MonarchChannel front end -- no plugin render needed, so a candidate is a ~1 s compile away.

THE FIT (`fit`). Once admissible, the two constants are swept by PATCHING MonarchChannel.h and
recompiling the standalone probe -- ~1 s per candidate, because the probe pulls in the DSP headers
only (no JUCE). The header stays the single source of truth: nothing is reimplemented in numpy, so
what is scored is the real `processClip` path. The objective is the comp-curve rms error over the
G2-G7 fit window; the ARBITER is still the 44-capture null, which only the winner is spent on.

Usage:
  p9_ceiling_fit.py curves [--render-dir DIR]      # absolute plugin/pedal comp curves
  p9_ceiling_fit.py static [--render-dir DIR]      # THE admissibility test — read this first
  p9_ceiling_fit.py probe --ceil V --knee V        # one candidate: patch, compile, score
  p9_ceiling_fit.py fit                            # sweep the 2-D grid
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import analyze as A            # noqa: E402
import gen_test_signal as gts   # noqa: E402
import offline_null_probe as ONP  # noqa: E402

RENDER_DIR = "/tmp/monarch_renders"


def _od_items(render_dir):
    orig = A.load(A.ORIG)
    items = [it for it in ONP.load_pairs(render_dir, orig) if it["mode"] == "OD"]
    if not items:
        sys.exit(f"no OD render/capture pairs in {render_dir} — run\n"
                 f"  python3 analysis/comprehensive_report.py --keep-renders {render_dir}")
    return items


def _curves(render_dir):
    """{drive: (plugin_rise_db, pedal_rise_db)} — median over tones, re each curve's -30 step."""
    steps = list(gts.LEVEL_STEPS_DB)
    items = _od_items(render_dir)
    out = {}
    for dv in sorted({it["drive"] for it in items}):
        rows = [it for it in items if abs(it["drive"] - dv) < 1e-9]
        pl, pe = [], []
        for it in rows:
            a = np.array([A.db(A.rms(A.seg_of(it["ren"], f"lvl_{s}"))) for s in steps])
            b = np.array([A.db(A.rms(A.seg_of(it["cap"], f"lvl_{s}"))) for s in steps])
            pl.append(a - a[0])
            pe.append(b - b[0])
        out[dv] = (np.median(np.array(pl), 0), np.median(np.array(pe), 0))
    return steps, out


def view_curves(render_dir, out=sys.stdout):
    steps, cur = _curves(render_dir)
    print("\n=== Overdrive — absolute comp curves, dB re each curve's own -30 dBFS step ===", file=out)
    print("  median over tone settings. `d` = plugin - pedal (what `p9 comp` prints).", file=out)
    for dv, (pl, pe) in cur.items():
        print(f"\n-- G{dv * 10:.0f}", file=out)
        print(f"{'':<8}" + "".join(f"{s:>8}" for s in steps), file=out)
        for name, v in (("pedal", pe), ("plugin", pl), ("d", pl - pe)):
            print(f"{name:<8}" + "".join(f"{x:>+8.2f}" for x in v), file=out)


def view_static(render_dir, out=sys.stdout):
    """Can ONE memoryless map on pin7 explain every (drive, level) cell? See the module docstring."""
    steps, cur = _curves(render_dir)
    print("\n=== ADMISSIBILITY — can a static ceiling on pin7 fit this at all? ===", file=out)
    print("  A memoryless waveshaper on pin7 imposes ONE level->level map shared by every drive,\n"
          "  pinned only up to a per-drive offset (the captures are level-normalised). Below, each\n"
          "  cell is placed by its measured plugin pin7 level and the pedal level it demands, with\n"
          "  the best per-drive offset removed. Cells at the SAME pin7 level must agree.", file=out)

    # Plugin pin7 rms (dB re the drive's own -30 step) comes from the probe; the render-measured
    # plugin rise is the check that the probe is a valid stand-in (OD's post-pin7 chain is linear).
    probe = _probe_table()
    if probe is None:
        print("\n  [probe unavailable — build it first: see `probe` in this file]", file=out)
        return

    print(f"\n{'drive':<7}{'lvlFS':>7}{'pin7 dBV':>10}{'plug dB':>9}{'pedal dB':>10}{'probe-render':>14}",
          file=out)
    rows = []
    for dv, (pl, pe) in cur.items():
        if dv not in probe:
            continue
        p7 = probe[dv]
        for i, s in enumerate(steps):
            if s not in p7:
                continue
            absdb, reldb = p7[s]
            rows.append((dv, s, absdb, pl[i], pe[i]))
            print(f"G{dv * 10:<6.0f}{s:>7}{absdb:>+10.2f}{pl[i]:>+9.2f}{pe[i]:>+10.2f}"
                  f"{reldb - pl[i]:>+14.2f}", file=out)

    print("\n  'probe-render' is the instrument check: the probe measures pin7 and the render\n"
          "  measures the output, and OD's chain between them is linear at 1 kHz, so these rises\n"
          "  must agree. A drifting column means the probe is not a valid stand-in.", file=out)
    _pairwise(rows, out)


def _pairwise(rows, out):
    """The test itself, done the only way that is offset-free.

    A static map f on pin7 gives  L_pedal(d,l) = f(x(d,l)) + c_d  with x the ABSOLUTE pin7 level and
    c_d an unknown per-drive constant (the captures are level-normalised). So every drive's
    (x, L_pedal) curve must be the SAME curve up to a vertical shift. Take two drives, restrict to
    the x-range they share, remove the best single offset between them, and any residual spread is
    an inconsistency no choice of f or c_d can absorb.
    """
    print("\n-- pairwise overlap test: every drive's (pin7, pedal) curve must be one shared", file=out)
    print("   curve plus a per-drive offset. Residual = what no static map can absorb.", file=out)
    drives = sorted({r[0] for r in rows})
    worst = 0.0
    print(f"\n{'pair':<12}{'overlap dBV':>16}{'n':>4}{'offset':>9}{'residual rms':>14}{'max':>8}", file=out)
    for i, d1 in enumerate(drives):
        for d2 in drives[i + 1:]:
            a = sorted([(r[2], r[4]) for r in rows if r[0] == d1])
            b = sorted([(r[2], r[4]) for r in rows if r[0] == d2])
            ax = np.array([p[0] for p in a]); ay = np.array([p[1] for p in a])
            bx = np.array([p[0] for p in b]); by = np.array([p[1] for p in b])
            lo, hi = max(ax[0], bx[0]), min(ax[-1], bx[-1])
            if hi - lo < 1.5:
                continue
            grid = np.linspace(lo, hi, 12)
            fa = np.interp(grid, ax, ay)
            fb = np.interp(grid, bx, by)
            diff = fa - fb
            off = float(np.mean(diff))
            res = diff - off
            rms, mx = float(np.sqrt(np.mean(res ** 2))), float(np.max(np.abs(res)))
            worst = max(worst, mx)
            print(f"G{d1 * 10:.0f}/G{d2 * 10:<8.0f}{lo:>+7.1f}..{hi:<+7.1f}{len(grid):>4}"
                  f"{off:>+9.2f}{rms:>14.2f}{mx:>8.2f}", file=out)
    print(f"\n  worst irreconcilable residual: {worst:.2f} dB", file=out)
    print("  A static ceiling on pin7 can only be the answer if this is small (<~0.5 dB). If it is\n"
          "  large, the pedal's extra compression does NOT depend on pin7 level alone, and step 3\n"
          "  must not be fitted as a memoryless limiter however it is parameterised.", file=out)

    # The residual is not noise — it is indexed by how far apart the two drives are, which is the
    # tell that a SECOND, drive-keyed error is mixed in (P6's driveMakeup caps at 6.0 dB against a
    # measured 6.8 dB need at G10). So report the fit window separately from the tail.
    print("\n-- residual vs drive gap (is the inconsistency structured or random?)", file=out)
    by_gap = {}
    for i, d1 in enumerate(drives):
        for d2 in drives[i + 1:]:
            a = sorted([(r[2], r[4]) for r in rows if r[0] == d1])
            b = sorted([(r[2], r[4]) for r in rows if r[0] == d2])
            ax = np.array([p[0] for p in a]); ay = np.array([p[1] for p in a])
            bx = np.array([p[0] for p in b]); by = np.array([p[1] for p in b])
            lo, hi = max(ax[0], bx[0]), min(ax[-1], bx[-1])
            if hi - lo < 1.5:
                continue
            grid = np.linspace(lo, hi, 12)
            diff = np.interp(grid, ax, ay) - np.interp(grid, bx, by)
            r = float(np.sqrt(np.mean((diff - np.mean(diff)) ** 2)))
            by_gap.setdefault(round(abs(d2 - d1), 1), []).append(r)
    for g in sorted(by_gap):
        v = by_gap[g]
        print(f"  drive gap {g:>4.1f} : residual rms {np.mean(v):>5.2f} dB  (n={len(v)})", file=out)

    for name, keep in (("G2-G7 only", lambda d: d <= 0.7), ("all drives", lambda d: True)):
        sub = [r for r in rows if keep(r[0])]
        ds = sorted({r[0] for r in sub})
        acc = []
        for i, d1 in enumerate(ds):
            for d2 in ds[i + 1:]:
                a = sorted([(r[2], r[4]) for r in sub if r[0] == d1])
                b = sorted([(r[2], r[4]) for r in sub if r[0] == d2])
                ax = np.array([p[0] for p in a]); ay = np.array([p[1] for p in a])
                bx = np.array([p[0] for p in b]); by = np.array([p[1] for p in b])
                lo, hi = max(ax[0], bx[0]), min(ax[-1], bx[-1])
                if hi - lo < 1.5:
                    continue
                grid = np.linspace(lo, hi, 12)
                diff = np.interp(grid, ax, ay) - np.interp(grid, bx, by)
                acc.append(float(np.sqrt(np.mean((diff - np.mean(diff)) ** 2))))
        print(f"  {name:<12}: mean residual rms {np.mean(acc):.2f} dB, worst {np.max(acc):.2f}", file=out)


def _probe_table():
    """pin7 levels with the ceiling DISABLED — the admissibility test asks whether a ceiling COULD
    explain the gap, so it must be measured against the plugin that still has the gap. The header is
    restored afterwards, so this view never disturbs what is currently baked in.
    """
    snap = _snapshot()
    try:
        _patch_header(0.0, 0.0)
        tab = _run_probe()
    finally:
        _restore(snap)
    # (absolute dBV at pin7, dB re this drive's own -30 step) — the map is over the ABSOLUTE level;
    # the relative figure is only for the probe-vs-render instrument check.
    return {d: {k: (A.db(v), A.db(v) - A.db(v0[-30])) for k, v in v0.items()}
            for d, v0 in tab.items() if -30 in v0} or None


# --------------------------------------------------------------------------- the fit loop
HEADER = "src/dsp/MonarchChannel.h"
SW1_HEADER_PATH = "src/dsp/SW1SoftClip.h"


def _snapshot():
    """Exact contents of every header this harness patches, so restoring cannot reformat a literal
    (a naive write-back turned `6.8e3` into `6800.0` — a spurious diff in a validated file)."""
    return {p: open(p).read() for p in (HEADER, SW1_HEADER_PATH)}


def _restore(snap):
    for p, s in snap.items():
        if open(p).read() != s:
            open(p, "w").write(s)
PROBE_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "p9_pin7_probe.cpp")
PROBE_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".cache", "p9_pin7_probe")
FIT_DRIVES = (0.2, 0.3, 0.4, 0.5, 0.6, 0.7)  # G2-G7: where `static` says the instrument is valid


def _patch_header(ceil, knee):
    """Set the two sw1Ceil* constants in the header. The header is the single source of truth."""
    import re
    s = open(HEADER).read()
    s2 = re.sub(r"(static constexpr double sw1CeilV = )[-0-9.eE]+(;)", rf"\g<1>{ceil!r}\g<2>", s)
    s2 = re.sub(r"(static constexpr double sw1CeilKneeV = )[-0-9.eE]+(;)", rf"\g<1>{knee!r}\g<2>", s2)
    if s2 == s and (ceil, knee) != _read_header():
        sys.exit(f"could not patch sw1Ceil* in {HEADER}")
    open(HEADER, "w").write(s2)


def _read_header():
    import re
    s = open(HEADER).read()
    c = re.search(r"static constexpr double sw1CeilV = ([-0-9.eE]+);", s)
    k = re.search(r"static constexpr double sw1CeilKneeV = ([-0-9.eE]+);", s)
    return (float(c.group(1)), float(k.group(1))) if c and k else (None, None)


def _run_probe():
    """Compile + run the probe; returns {drive: {levelDb: pin7_rms}}. ~1 s."""
    import subprocess
    os.makedirs(os.path.dirname(PROBE_BIN), exist_ok=True)
    r = subprocess.run(["clang++", "-std=c++17", "-O2", "-I.", "-isystem", "libs/chowdsp_wdf/include",
                        PROBE_SRC, "-o", PROBE_BIN], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("probe compile failed:\n" + r.stderr[-3000:])
    out = subprocess.run([PROBE_BIN], capture_output=True, text=True, check=True).stdout
    tab = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) != 5 or p[0] == "drive":
            continue
        tab.setdefault(float(p[0]), {})[int(float(p[1]))] = float(p[4])
    return tab


def _score(tab, ped, steps, drives=FIT_DRIVES):
    """rms (and per-drive) error between the probe's rise and the pedal's, over the fit window."""
    errs, per = [], {}
    for d in drives:
        if d not in tab or d not in ped:
            continue
        base = A.db(tab[d][-30])
        e = [(A.db(tab[d][s]) - base) - ped[d][i] for i, s in enumerate(steps) if s in tab[d]]
        per[d] = float(np.sqrt(np.mean(np.array(e) ** 2)))
        errs += e
    return float(np.sqrt(np.mean(np.array(errs) ** 2))), per


def view_probe(render_dir, ceil, knee, out=sys.stdout):
    steps, cur = _curves(render_dir)
    ped = {d: pe for d, (pl, pe) in cur.items()}
    _patch_header(ceil, knee)
    tab = _run_probe()
    rms, per = _score(tab, ped, steps)
    print(f"\nceil={ceil} knee={knee}  ->  comp rms error {rms:.3f} dB over G2-G7", file=out)
    print("  per-drive rms: " + "  ".join(f"G{d * 10:.0f} {v:.2f}" for d, v in sorted(per.items())),
          file=out)
    print(f"\n{'drive':<7}" + "".join(f"{s:>8}" for s in steps), file=out)
    for d in sorted(tab):
        base = A.db(tab[d][-30])
        row = [(A.db(tab[d][s]) - base) - (ped[d][i] if d in ped else 0.0)
               for i, s in enumerate(steps)]
        tag = "" if d in FIT_DRIVES else "  (outside fit window)"
        print(f"G{d * 10:<6.0f}" + "".join(f"{v:>+8.2f}" for v in row) + tag, file=out)
    print("\n  values are plugin-minus-pedal, i.e. what `p9 comp` prints. Target: 0.", file=out)


def _quiet_gain_db(tab, base):
    """Change in ABSOLUTE OD output level at a quiet, essentially-unclipped level (-30 dBFS), dB.

    The comp objective is self-anchored per curve, so it is BLIND to an overall gain change: a
    candidate can score well while quietly re-levelling Overdrive, which the null would punish and
    this table would not show. Report it alongside the score. Median over the fit-window drives.
    """
    v = [A.db(tab[d][-30]) - A.db(base[d][-30]) for d in FIT_DRIVES if d in tab and d in base]
    return float(np.median(v)) if v else float("nan")


def view_fit(render_dir, ceils, knees, out=sys.stdout):
    steps, cur = _curves(render_dir)
    ped = {d: pe for d, (pl, pe) in cur.items()}
    keep, snap = _read_header(), _snapshot()
    try:
        _patch_header(0.0, 0.0)
        base = _run_probe()
        base_rms, _ = _score(base, ped, steps)
        print(f"\n=== SW-1 ceiling grid — comp rms error (dB) over G2-G7 ===", file=out)
        print(f"  baseline (ceiling disabled): {base_rms:.3f} dB rms", file=out)
        print("  'dG' = change in absolute OD level at -30 dBFS, i.e. how much this candidate\n"
              "  re-levels the mode. The comp score cannot see it; the null can. Watch it.", file=out)
        best = None
        print("\nknee/ceil ".ljust(10) + "".join(f"{c:>14}" for c in ceils), file=out)
        for k in knees:
            cells = []
            for c in ceils:
                if k >= c:
                    cells.append("             -")
                    continue
                _patch_header(c, k)
                tab = _run_probe()
                rms, _ = _score(tab, ped, steps)
                dg = _quiet_gain_db(tab, base)
                cells.append(f"{rms:>8.3f}{dg:>+6.2f}")
                if best is None or rms < best[0]:
                    best = (rms, c, k, dg)
            print(f"{k:<10}" + "".join(cells), file=out)
    finally:
        _restore(snap)
    print(f"\n  best: ceil={best[1]} knee={best[2]} -> {best[0]:.3f} dB rms (dG {best[3]:+.2f} dB)",
          file=out)
    print(f"  (header restored to ceil={keep[0]} knee={keep[1]})", file=out)
    print("\n  Each cell is 'rms  dG'. Do NOT pick on rms alone — take the top few to the null.", file=out)


def _patch_r11(ohms):
    import re
    s = open(SW1_HEADER_PATH).read()
    s2 = re.sub(r"(static constexpr double R11 = )[-0-9.eE]+(;)", rf"\g<1>{ohms!r}\g<2>", s)
    open(SW1_HEADER_PATH, "w").write(s2)


def _read_r11():
    import re
    return float(re.search(r"static constexpr double R11 = ([-0-9.eE]+);",
                           open(SW1_HEADER_PATH).read()).group(1))


def view_r11(render_dir, values, out=sys.stdout):
    """Is the ceiling really a SERIES-RESISTANCE error? A physics fix would beat an empirical one.

    Above the diode clamp the model's pin7 is Vf + i_in*R11, so the whole reason it keeps climbing
    is the R11/R9 = 0.68 residual slope. The best empirical ceiling landed at ~1.6 V, i.e. right at
    the clamp itself -- which is what a MUCH SMALLER series resistance would produce on its own,
    since the bare diode voltage grows only logarithmically with current. If some smaller R11 scores
    like the fitted ceiling, the defect is a component value and not a missing mechanism.

    NOTE this is a real topology claim and the schematic says 6.8k (P9's schematic-checker re-trace
    confirmed one shared 6.8k in series with the whole network). So a good score here does NOT
    license changing R11 -- it would license going back to the schematic with a specific question.
    """
    steps, cur = _curves(render_dir)
    ped = {d: pe for d, (pl, pe) in cur.items()}
    keep_c, keep_r, snap = _read_header(), _read_r11(), _snapshot()
    print("\n=== is it R11? — comp rms error with the ceiling OFF and R11 varied ===", file=out)
    print(f"  shipped R11 = {keep_r:g} ohms (schematic value). Ceiling disabled throughout.", file=out)
    print(f"\n{'R11':>10}{'comp rms':>12}{'dG':>8}", file=out)
    try:
        _patch_header(0.0, 0.0)
        _patch_r11(keep_r)
        base = _run_probe()
        for r in values:
            _patch_r11(float(r))
            tab = _run_probe()
            rms, _ = _score(tab, ped, steps)
            print(f"{r:>10.0f}{rms:>12.3f}{_quiet_gain_db(tab, base):>+8.2f}", file=out)
    finally:
        _restore(snap)
    print(f"\n  (restored R11={keep_r:g}, ceil={keep_c[0]}, knee={keep_c[1]})", file=out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("view", choices=["curves", "static", "probe", "fit", "r11"])
    ap.add_argument("--render-dir", default=RENDER_DIR)
    ap.add_argument("--ceil", type=float, default=1.8)
    ap.add_argument("--knee", type=float, default=1.0)
    ap.add_argument("--ceils", type=float, nargs="+",
                    default=[1.5, 1.6, 1.7, 1.8, 1.9, 2.0, 2.2, 2.5])
    ap.add_argument("--knees", type=float, nargs="+", default=[0.4, 0.6, 0.8, 1.0, 1.2, 1.4])
    ap.add_argument("--r11", type=float, nargs="+",
                    default=[6800, 4700, 3400, 2200, 1000, 470, 100])
    a = ap.parse_args()
    if a.view == "probe":
        view_probe(a.render_dir, a.ceil, a.knee)
    elif a.view == "fit":
        view_fit(a.render_dir, a.ceils, a.knees)
    elif a.view == "r11":
        view_r11(a.render_dir, a.r11)
    else:
        {"curves": view_curves, "static": view_static}[a.view](a.render_dir)


if __name__ == "__main__":
    main()
