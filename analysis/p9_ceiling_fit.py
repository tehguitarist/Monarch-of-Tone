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

THREE GATES BEFORE A FIT, and they are different questions (P10 step 3 added two of them):
  `static`  ADMISSIBLE — can one memoryless map on pin7 explain every cell at all? (P9 step 3)
            `--gain-fix` removes the gain path's measured error first, because the test absorbs only
            a VERTICAL per-drive offset: a drive whose pre-clip level is wrong sits at the wrong place
            on the pin7 AXIS, which looks identical to "no map exists" without the switch.
  `floor`   RESOLVABLE — what is the target's own noise on this instrument? Uses TONE as the probe,
            because the tone stack is post-clip and linear, so a self-anchored comp curve must be
            tone-independent and any spread is capture-side. Run this before grinding any cell.
  `need`    RIGHT SHAPE — the extra compression REQUIRED per drive against what the instrument
            SUPPLIES there. This is what showed the requirement to be nearly drive-independent while
            a ceiling voltage must ramp, i.e. the mean was right and the distribution was not.

Usage:
  p9_ceiling_fit.py curves [--render-dir DIR]      # absolute plugin/pedal comp curves
  p9_ceiling_fit.py static [--gain-fix]            # gate 1 — admissibility; read this first
  p9_ceiling_fit.py floor                          # gate 2 — the target's own tone-spread
  p9_ceiling_fit.py need [--gain-fix]              # gate 3 — requirement vs supply, per drive
  p9_ceiling_fit.py probe --ceil V --knee V [--slope M]   # one candidate: patch, compile, score
  p9_ceiling_fit.py fit                            # sweep the ceiling/knee grid
  p9_ceiling_fit.py ratio --ceils .. --knees .. --exps ..  # sweep the residual SLOPE (sw1CeilSlope)
  p9_ceiling_fit.py r11                            # is it a component value instead?

⚠ `probe` PATCHES THE HEADER AND DOES NOT RESTORE IT (the fit loops snapshot/restore around
themselves; `probe` deliberately leaves its candidate in place so it can be built). Restore it with
`git diff`/`git checkout` ONLY if nothing else in that file is uncommitted — a plain checkout there
will silently discard in-progress DSP work. Copy the file aside instead.
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


def view_static(render_dir, gain_fix=False, out=sys.stdout):
    """Can ONE memoryless map on pin7 explain every (drive, level) cell? See the module docstring."""
    steps, cur = _curves(render_dir)
    print("\n=== ADMISSIBILITY — can a static ceiling on pin7 fit this at all? ===", file=out)
    print("  A memoryless waveshaper on pin7 imposes ONE level->level map shared by every drive,\n"
          "  pinned only up to a per-drive offset (the captures are level-normalised). Below, each\n"
          "  cell is placed by its measured plugin pin7 level and the pedal level it demands, with\n"
          "  the best per-drive offset removed. Cells at the SAME pin7 level must agree.", file=out)
    if gain_fix:
        print("\n  --gain-fix: the gain path's MEASURED error is removed first (see GAIN_FIX_DB).\n"
              "  The per-drive offset this test removes is VERTICAL, so a drive whose pre-clip level\n"
              "  is wrong sits at the wrong place on the pin7 axis and no map can absorb it. That is\n"
              "  a distinct failure from 'no static map exists', and the two look identical without\n"
              "  this switch.", file=out)
        for d, v in sorted(GAIN_FIX_DB.items()):
            print(f"    G{d * 10:.0f}: +{v:.2f} dB (measured)   {_hyper_extra_db(d):+.2f} dB "
                  f"(candidate 2's own shape, independent)", file=out)

    # Plugin pin7 rms (dB re the drive's own -30 step) comes from the probe; the render-measured
    # plugin rise is the check that the probe is a valid stand-in (OD's post-pin7 chain is linear).
    probe = _probe_table(GAIN_FIX_DB if gain_fix else None)
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
    if gain_fix:
        print("  (with --gain-fix that column is EXPECTED to disagree at the corrected drives — the\n"
              "   render on disk does not have the gain fix in it. Read it at the others only.)",
              file=out)
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


def view_floor(render_dir, out=sys.stdout):
    """P10 step 3 — the TARGET-SIDE NOISE FLOOR of the compression instrument. Run before fitting it.

    `p31_harm_floor.py` exists because no quiet harmonic should be fitted without knowing the capture
    chain's floor. The comp curves never had the equivalent, and they need one more than the harmonics
    do: the captures are 44 independently trained NAM models, so each carries its own fit error.

    The probe used here is TONE. The tone stack sits AFTER the clip node and is linear, so it cannot
    change a self-anchored compression curve at all -- the true tone-spread of the total rise is zero,
    and the plugin's measured spread confirms that (it comes out at 0.00-0.08 dB). Whatever the
    pedal's spread is, is therefore target-side, and it bounds how finely this instrument can be
    fitted at that cell.

    Only G2/G5/G10 have more than one tone capture; the other drives print `-` rather than a
    misleading 0.00, because a spread over one sample is structurally zero and means nothing.
    """
    items = ONP.load_pairs(render_dir, A.load(A.ORIG))
    steps = list(gts.LEVEL_STEPS_DB)

    def total_rise(w):
        b = [A.db(A.rms(A.seg_of(w, f"lvl_{s}"))) for s in steps]
        return b[-1] - b[0]

    print("\n=== TARGET-SIDE FLOOR — tone-spread of the self-anchored comp curve ===", file=out)
    print("  TONE is post-clip and linear, so the true spread is 0. The pedal's is the floor.", file=out)
    modes = ("Clean", "OD", "Dist")
    print(f"\n{'drive':<7}" + "".join(f"{m:>20}" for m in modes), file=out)
    print(f"{'':<7}" + "".join(f"{'pedal':>10}{'plugin':>10}" for _ in modes), file=out)
    worst = {}
    for dv in sorted({it["drive"] for it in items}):
        row = f"G{dv * 10:<6.0f}"
        for m in modes:
            r = [it for it in items if it["mode"] == m and abs(it["drive"] - dv) < 1e-9]
            if len(r) < 2:
                row += f"{'-':>10}{'-':>10}"
                continue
            pe = [total_rise(it["cap"]) for it in r]
            pl = [total_rise(it["ren"]) for it in r]
            row += f"{max(pe) - min(pe):>10.2f}{max(pl) - min(pl):>10.2f}"
            worst[(m, dv)] = max(pe) - min(pe)
        print(row, file=out)
    print(f"\n  tone settings per drive: "
          + ", ".join(f"G{d * 10:.0f}={len([i for i in items if i['mode'] == 'OD' and abs(i['drive'] - d) < 1e-9])}"
                      for d in sorted({it["drive"] for it in items})), file=out)
    if worst:
        k = max(worst, key=worst.get)
        print(f"  worst floor: {k[0]} G{k[1] * 10:.0f} at {worst[k]:.2f} dB — do not grind that cell "
              f"finer than this.", file=out)


def view_need(render_dir, gain_fix=None, out=sys.stdout):
    """P10 step 3 — how much extra compression is REQUIRED at each drive, against what the shipped
    instrument SUPPLIES there. The one table that says whether the instrument's shape is right.

    `static` asks whether *a* memoryless map exists. This asks the next question: the total output
    rise over the 27 dB level span is a single number per drive, so

        need(d)   = plugin rise with the ceiling OFF  -  pedal rise      (what is missing)
        supply(d) = plugin rise with the ceiling OFF  -  with it ON      (what it delivers)

    Both are differences of self-anchored rises, so the per-capture level caveat cannot touch them.
    A level-keyed ceiling MUST supply more at higher drive, because pin7 is higher there. If `need`
    is flat while `supply` ramps, the defect is the instrument's drive-dependence -- and no choice of
    ceiling voltage or knee fixes that, which is exactly what P9 step 3 observed when it found the
    G10 penalty identical at every knee.
    """
    steps, cur = _curves(render_dir)
    keep, snap = _read_header(), _snapshot()
    try:
        _patch_header(0.0, 0.0)
        off = _run_probe(gain_fix)
        _patch_header(*keep)
        on = _run_probe(gain_fix)
    finally:
        _restore(snap)

    def rise(tab, d):
        return A.db(tab[d][steps[-1]]) - A.db(tab[d][steps[0]])

    print("\n=== REQUIREMENT vs SUPPLY — total OD output rise over "
          f"{steps[0]} to {steps[-1]} dBFS ===", file=out)
    print(f"  shipped instrument: ceil={keep[0]} V, knee={keep[1]} V"
          + ("   (gain-fixed)" if gain_fix else ""), file=out)
    print(f"\n{'drive':<7}{'pedal':>8}{'plug off':>10}{'need':>8}{'supply':>9}{'error':>8}", file=out)
    need, supply = [], []
    for d in sorted(off):
        if d not in cur:
            continue
        pe = cur[d][1][-1] - cur[d][1][0]
        n, s = rise(off, d) - pe, rise(off, d) - rise(on, d)
        need.append(n); supply.append(s)
        print(f"G{d * 10:<6.0f}{pe:>+8.2f}{rise(off, d):>+10.2f}{n:>+8.2f}{s:>+9.2f}"
              f"{s - n:>+8.2f}", file=out)
    n, s = np.array(need), np.array(supply)
    print(f"\n  need   : mean {n.mean():+.2f} dB, spread {n.max() - n.min():.2f} dB "
          f"(sd {n.std():.2f})", file=out)
    print(f"  supply : mean {s.mean():+.2f} dB, spread {s.max() - s.min():.2f} dB "
          f"(sd {s.std():.2f})", file=out)
    print(f"  error  : rms {np.sqrt(np.mean((s - n) ** 2)):.2f} dB, worst {np.abs(s - n).max():.2f}",
          file=out)
    print(f"  a FLAT {n.mean():.2f} dB would score: rms "
          f"{np.sqrt(np.mean((n.mean() - n) ** 2)):.2f} dB, worst {np.abs(n.mean() - n).max():.2f} "
          "— the bar any shape must clear.", file=out)


def _probe_table(gain_fix=None):
    """pin7 levels with the ceiling DISABLED — the admissibility test asks whether a ceiling COULD
    explain the gap, so it must be measured against the plugin that still has the gap. The header is
    restored afterwards, so this view never disturbs what is currently baked in.
    """
    snap = _snapshot()
    try:
        _patch_header(0.0, 0.0)
        tab = _run_probe(gain_fix)
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
# ...and the whole axis, which only becomes a legitimate fit window once the gain path's measured
# error is removed (P10 step 3 — until then G8/G10 sit at the wrong place on the pin7 axis).
ALL_DRIVES = (0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.0)


def _patch_header(ceil, knee, exp=None):
    """Set the sw1Ceil* constants in the header. The header is the single source of truth.

    `exp` is sw1CeilSlope, the ceiling's asymptotic slope (P10 step 3; 0 = P9's pure ceiling). None
    leaves it as it is, so every pre-existing caller keeps sweeping the shape that is baked in.
    """
    import re
    s = open(HEADER).read()
    s2 = re.sub(r"(static constexpr double sw1CeilV = )[-0-9.eE]+(;)", rf"\g<1>{ceil!r}\g<2>", s)
    s2 = re.sub(r"(static constexpr double sw1CeilKneeV = )[-0-9.eE]+(;)", rf"\g<1>{knee!r}\g<2>", s2)
    if exp is not None:
        s2 = re.sub(r"(static constexpr double sw1CeilSlope = )[-0-9.eE]+(;)", rf"\g<1>{exp!r}\g<2>", s2)
    if s2 == s and (ceil, knee, exp if exp is not None else _read_header()[2]) != _read_header():
        sys.exit(f"could not patch sw1Ceil* in {HEADER}")
    open(HEADER, "w").write(s2)


def _read_header():
    import re
    s = open(HEADER).read()
    g = [re.search(rf"static constexpr double {n} = ([-0-9.eE]+);", s)
         for n in ("sw1CeilV", "sw1CeilKneeV", "sw1CeilSlope")]
    return tuple(float(m.group(1)) if m else None for m in g)


# ---------------------------------------------------------------- P10 step 3: the gain-path fix
# P10 measured the plugin's pre-clip gain against the pedal's on the ONE instrument that separates
# gain from ceiling (`p9_od_compression.py knee`/`split`): right to +-0.4 dB from G2 to G8, and
# ~5.0-5.3 dB SHORT at G10 (dKnee +5.28 against dGlin -4.96, opposite ends of the same curve).
#
# It was measured in BOOST -- the only mode with no diode clipper -- and is applied here in
# OVERDRIVE, which is legitimate because `driveMakeup` and Stage 1 are both mode-independent: the
# pre-clip level error is a property of the gain path alone, and Boost is simply where it can be read
# without a clipper in the way.
GAIN_FIX_DB = {1.0: 5.28}  # every other drive is 0 within the measurement's +-0.4 dB


def _hyper_extra_db(d, k=0.187):
    """P10 step 1's candidate 2 MINUS the shipped ramp, dB -- the circuit's own gain shape.

    The DRIVE pot's discarded second action is a series resistance into Stage 2's virtual ground, so
    level goes as 1/(R6 + R(1-d)); normalised at the ramp's onset. Reported next to GAIN_FIX_DB as a
    cross-check, because the two were derived from completely different evidence and agree to 0.02 dB
    at G10 (5.30 vs the measured 5.28).
    """
    shipped = min(6.0, max(0.0, 14.0 * (d - 0.5)))
    hyper = 20.0 * np.log10((k + 0.5) / (k + 1.0 - d)) if d > 0.5 else 0.0
    return hyper - shipped


def _run_probe(gain_fix=None):
    """Compile + run the probe; returns {drive: {levelDb: pin7_rms}}. ~1 s.

    `gain_fix` is an optional {drive: dB} pre-clip gain handed to the probe (see GAIN_FIX_DB). It is
    applied between processPre and processClip, which is exactly equivalent to raising `driveMakeup`.
    """
    import subprocess
    os.makedirs(os.path.dirname(PROBE_BIN), exist_ok=True)
    r = subprocess.run(["clang++", "-std=c++17", "-O2", "-I.", "-isystem", "libs/chowdsp_wdf/include",
                        PROBE_SRC, "-o", PROBE_BIN], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("probe compile failed:\n" + r.stderr[-3000:])
    env = dict(os.environ)
    if gain_fix:
        env["P9_PRE_GAIN_DB"] = ",".join(f"{d}:{v}" for d, v in sorted(gain_fix.items()))
    else:
        env.pop("P9_PRE_GAIN_DB", None)
    out = subprocess.run([PROBE_BIN], capture_output=True, text=True, check=True, env=env).stdout
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


def view_probe(render_dir, ceil, knee, slope=None, gain_fix=None, out=sys.stdout):
    steps, cur = _curves(render_dir)
    ped = {d: pe for d, (pl, pe) in cur.items()}
    _patch_header(ceil, knee, slope)
    tab = _run_probe(gain_fix)
    drives = ALL_DRIVES
    rms, per = _score(tab, ped, steps, drives)
    win = "G2-G10"
    print(f"\nceil={ceil} knee={knee}  ->  comp rms error {rms:.3f} dB over {win}", file=out)
    print("  per-drive rms: " + "  ".join(f"G{d * 10:.0f} {v:.2f}" for d, v in sorted(per.items())),
          file=out)
    print(f"\n{'drive':<7}" + "".join(f"{s:>8}" for s in steps), file=out)
    for d in sorted(tab):
        base = A.db(tab[d][-30])
        row = [(A.db(tab[d][s]) - base) - (ped[d][i] if d in ped else 0.0)
               for i, s in enumerate(steps)]
        tag = "" if d in drives else "  (outside fit window)"
        print(f"G{d * 10:<6.0f}" + "".join(f"{v:>+8.2f}" for v in row) + tag, file=out)
    print("\n  values are plugin-minus-pedal, i.e. what `p9 comp` prints. Target: 0.", file=out)


def _quiet_gain_db(tab, base, drives=FIT_DRIVES):
    """Change in ABSOLUTE OD output level at a quiet, essentially-unclipped level (-30 dBFS), dB.

    The comp objective is self-anchored per curve, so it is BLIND to an overall gain change: a
    candidate can score well while quietly re-levelling Overdrive, which the null would punish and
    this table would not show. Report it alongside the score. Median over the fit-window drives.
    """
    v = [A.db(tab[d][-30]) - A.db(base[d][-30]) for d in drives if d in tab and d in base]
    return float(np.median(v)) if v else float("nan")


def view_fit(render_dir, ceils, knees, gain_fix=None, out=sys.stdout):
    steps, cur = _curves(render_dir)
    ped = {d: pe for d, (pl, pe) in cur.items()}
    keep, snap = _read_header(), _snapshot()
    drives = ALL_DRIVES if gain_fix else FIT_DRIVES
    win = "G2-G10 (gain-fixed)" if gain_fix else "G2-G7"
    try:
        _patch_header(0.0, 0.0)
        base = _run_probe(gain_fix)
        base_rms, _ = _score(base, ped, steps, drives)
        print(f"\n=== SW-1 ceiling grid — comp rms error (dB) over {win} ===", file=out)
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
                tab = _run_probe(gain_fix)
                rms, _ = _score(tab, ped, steps, drives)
                dg = _quiet_gain_db(tab, base, drives)
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


def view_ratio(render_dir, knees, exps, gain_fix=None, ceils=(1.6,), out=sys.stdout):
    """P10 step 3 — sweep the ceiling's RESIDUAL SLOPE, over the WHOLE drive axis and over G2-G7.

    Both windows are printed for every candidate, because the question is not only "does this fix
    G8-G10" but "does it give up any of what the shipped tanh ceiling already earned in G2-G7". The
    shipped shape is scored on the same two windows as the first row, so the comparison is like for
    like, and `dG` still guards against a candidate quietly re-levelling the mode.
    """
    steps, cur = _curves(render_dir)
    ped = {d: pe for d, (pl, pe) in cur.items()}
    keep, snap = _read_header(), _snapshot()
    try:
        _patch_header(0.0, 0.0, 0.0)
        base = _run_probe(gain_fix)
        print("\n=== fixed-RATIO shape vs the shipped tanh ceiling — comp rms error (dB) ===", file=out)
        print("  'all' = G2-G10, 'fit' = G2-G7 (what the tanh ceiling was fitted on).", file=out)
        print(f"\n{'shape':<26}{'all':>8}{'fit':>8}{'dG':>8}", file=out)

        def score(label):
            tab = _run_probe(gain_fix)
            a, _ = _score(tab, ped, steps, ALL_DRIVES)
            f, _ = _score(tab, ped, steps, FIT_DRIVES)
            print(f"{label:<26}{a:>8.3f}{f:>8.3f}"
                  f"{_quiet_gain_db(tab, base, ALL_DRIVES):>+8.2f}", file=out)
            return a, f

        print(f"{'(no ceiling at all)':<26}"
              f"{_score(base, ped, steps, ALL_DRIVES)[0]:>8.3f}"
              f"{_score(base, ped, steps, FIT_DRIVES)[0]:>8.3f}{0.0:>+8.2f}", file=out)
        _patch_header(keep[0], keep[1], 0.0)
        best_tanh = score(f"shipped ceil={keep[0]} knee={keep[1]} m=0")
        best = None
        for c in ceils:
            for k in knees:
                for e in exps:
                    if k >= c:
                        continue
                    _patch_header(c, k, e)
                    a, f = score(f"ceil={c} knee={k} m={e:.2f}")
                    if best is None or a < best[0]:
                        best = (a, f, k, e, c)
    finally:
        _restore(snap)
    print(f"\n  shipped tanh : all {best_tanh[0]:.3f}  fit {best_tanh[1]:.3f}", file=out)
    print(f"  best sloped  : all {best[0]:.3f}  fit {best[1]:.3f}   "
          f"(ceil {best[4]}, knee {best[2]}, m {best[3]})", file=out)
    print("\n  A slope wins only if it beats m=0 on 'all' WITHOUT giving up 'fit'. The null is still\n"
          "  the arbiter — take the top few, not the argmin.", file=out)


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
    ap.add_argument("view", choices=["curves", "static", "floor", "need", "probe", "fit", "ratio", "r11"])
    ap.add_argument("--render-dir", default=RENDER_DIR)
    ap.add_argument("--gain-fix", action="store_true",
                    help="remove the gain path's measured error first (P10 step 3); applies to "
                         "static/probe/fit")
    ap.add_argument("--ceil", type=float, default=1.8)
    ap.add_argument("--knee", type=float, default=1.0)
    ap.add_argument("--slope", type=float, default=None, help="sw1CeilSlope for `probe`")
    ap.add_argument("--ceils", type=float, nargs="+",
                    default=[1.5, 1.6, 1.7, 1.8, 1.9, 2.0, 2.2, 2.5])
    ap.add_argument("--knees", type=float, nargs="+", default=[0.4, 0.6, 0.8, 1.0, 1.2, 1.4])
    ap.add_argument("--exps", type=float, nargs="+", default=[0.05, 0.1, 0.15, 0.2, 0.3],
                    help="sw1CeilSlope values (the ceiling's asymptotic slope)")
    ap.add_argument("--r11", type=float, nargs="+",
                    default=[6800, 4700, 3400, 2200, 1000, 470, 100])
    a = ap.parse_args()
    gf = GAIN_FIX_DB if a.gain_fix else None
    if a.view == "probe":
        view_probe(a.render_dir, a.ceil, a.knee, slope=a.slope, gain_fix=gf)
    elif a.view == "fit":
        view_fit(a.render_dir, a.ceils, a.knees, gain_fix=gf)
    elif a.view == "r11":
        view_r11(a.render_dir, a.r11)
    elif a.view == "static":
        view_static(a.render_dir, gain_fix=a.gain_fix)
    elif a.view == "floor":
        view_floor(a.render_dir)
    elif a.view == "need":
        view_need(a.render_dir, gain_fix=gf)
    elif a.view == "ratio":
        view_ratio(a.render_dir, a.knees, a.exps, gain_fix=gf, ceils=a.ceils)
    else:
        view_curves(a.render_dir)


if __name__ == "__main__":
    main()
