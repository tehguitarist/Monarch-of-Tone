#!/usr/bin/env python3
"""P9 — Overdrive's "mode-specific tilt" and its THD roll-off (FR_THD_AUDIT.md P9).

THE PREMISE THIS HARNESS WAS BUILT TO TEST. P9 was filed as "Overdrive carries +1.4 to +2.5 dB of
tilt at every drive, the only mode that does" plus "its THD falls off with frequency far faster
than the pedal's". Two symptoms, filed as one open mode-specific defect.

WHAT THE VIEWS ARE FOR.

  `tilt`   the FR error as a drive x SWEEP-LEVEL cross-tab, per mode (P4's rule: never marginalise
           one axis away). This is the view that decides whether the "tilt" is linear EQ at all --
           a linear tilt is the SAME at every sweep level, a compression difference GROWS with
           level. Read this before anything else.

  `thd`    the THD deficit as a drive x BAND table, per mode. The companion symptom, on the axis
           that matters: the deficit's onset frequency and its drive-dependence.

  `orders` the discriminator between the two mechanisms that both produce "less THD at HF":
             - every order down by the same amount  -> the clipper is not being driven as hard
             - high orders down, H2/H3 intact       -> the knee shape or the post-clip filtering
           Needs renders (see --render-dir).

  `valid`  INSTRUMENT VALIDITY, per P7's rule. The clean sweep stops being a linear FR measurement
           part-way up the DRIVE knob, and P7 only ever checked that for Boost. If OD's clean sweep
           is itself clipping, then the "tilt at every drive" measured on it is a distortion
           reading, not an EQ reading. Needs renders.

  `comp`   the DYNAMICS axis (lvl_-30..lvl_-3, 1 kHz), which the audit lists as never audited at
           all and flags as "likely the same underlying thing as P9". Output level vs input level,
           plugin vs pedal, per mode. Curves are normalised to their own -30 dB point because the
           captures are per-mode level-normalised (CLAUDE.md) -- only the SHAPE is comparable.

  `gain`   the unit conversion `orders` lacks: re-renders at flat pre-clip gains and measures
           d(odd Hn/H1)/d(input dB) directly, so a harmonic deficit can be read as dB of drive.
           Needs renders + PedalRender, ~6 min.

  `tones`  P9 step 1's first new axis: discrete steady tones (-14 dBFS, gts.TONE_FREQS), self-
           anchored per capture on 82 Hz. `comp` found the ceiling at ONE frequency (1 kHz); this
           checks whether it is broadband (flat delta vs frequency -- more ceiling evidence) or
           frequency-shaped (Stage 1's shelf, a different mechanism). Needs renders.

  `decay`  P9 step 1's second new axis: plucked-note decay (decay_220, decay_1k), self-anchored on
           each curve's own quietest window. A CONTINUOUS trajectory through the drive range inside
           one segment, unlike `comp`'s synthetic level steps -- the natural-dynamics version of
           the same test, at two frequencies. Needs renders.

WHAT THIS SCRIPT FOUND (2026-07-29) -- read before re-running any of it:

  * P9's premise is WITHDRAWN. `valid` shows OD's clean sweep carries 1.4-6 % THD from G2 to G5, so
    the "tilt at every drive" was never a linear FR reading; `tilt` shows it GROWS with sweep level
    (+0.70 clean -> +2.63 at -6 dB, at G2), which is this table's own definition of a compression
    difference. It is the dynamics axis, not EQ.
  * `comp` is where the answer is: the pedal's OVERDRIVE output turns over with input level and the
    plugin's never does (G5, -6 -> -3 dBFS: pedal +3.96 -> +3.93, plugin +5.94 -> +7.08). Boost and
    Distortion match, because their ceilings are modelled. OD has no ceiling.
  * `orders` MUST NOT be read alone. `gain` measured its sensitivity at |d(odd Hn/H1)/d(input dB)|
    = 0.0-0.5, sign-unstable, in OD above G5 -- harmonic dB does not convert to drive dB there at
    all, and `orders`' median hid a per-capture spread of -0.4 to -6.3 dB at 800 Hz. Use `gain`'s
    `sens` row as the finding; ignore its `req` row wherever |sens| is small.
  * STEP 1 DONE (2026-07-29): `decay` reproduces `comp`'s finding on a totally different signal
    (plucked-note decay, not synthetic level steps) -- OD is hot at the attack, converges to 0 at
    the tail, growing with drive, at BOTH 220 Hz and 1 kHz. Second independent confirmation of the
    missing ceiling. `tones` is NOT flat vs frequency (rises through 1-5 kHz) but that's Stage 1's
    known shelf feeding more pre-clip level in at HF, not a competing mechanism -- read it as a
    caveat, not evidence against the ceiling. `decay` ALSO found something nobody was looking for:
    Boost at G10 diverges +5 dB at the attack (both frequencies), decaying to 0 -- nothing like it
    at G2-G8. That's the discrete/level-stepped instrument P10 was waiting for; see FR_THD_AUDIT.md
    P10. Distortion's decay delta is negative at low-mid drive (opposite sign to OD) and OD's own
    G10 row doesn't converge cleanly on decay_1k -- both unexplained, noted for later.

LEVEL IS NOT SHAPE, and the arbiter is still the null. This script measures; it decides nothing.

Usage:
  p9_od_compression.py tilt|thd                             # JSON only, <1 s
  p9_od_compression.py orders|valid|comp|tones|decay [--render-dir DIR]
  p9_od_compression.py gain [--in-gain 0 2 4]               # renders; slow
  p9_od_compression.py all
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import analyze as A            # noqa: E402
import fr_thd_audit as F       # noqa: E402
import gen_test_signal as gts  # noqa: E402
import offline_null_probe as ONP  # noqa: E402

MODES = ("Boost", "Overdrive", "Distortion")
DEFAULT_RENDERS = "/tmp/monarch_renders_p9"

# Bands. 80 Hz up because sub-64 Hz is P1/P8's phase-limited problem; 5120 Hz because the captures
# are untrustworthy above ~8 kHz and Farina's THD is H2-only past ~6.3 kHz (P0).
LO, HI = 80.0, 5120.0
BANDS = ((80, 254, "80-254"), (320, 640, "320-640"), (806, 1613, "806-1613"),
         (2032, 3225, "2k-3.2k"), (4064, 5120, "4-5.1k"))


def _sel(b, lo, hi):
    return (b >= lo) & (b <= hi)


def err_raw(cap, sweep):
    """plugin-minus-pedal FR error, dB, NOT anchor-normalised (every view here mean-removes over
    its own scored band instead — level is a separately calibrated axis, per P7)."""
    f = cap["fr"][sweep]
    return np.array(f["plugin_db"], float) - np.array(f["pedal_db"], float)


def _med(rows):
    return np.nanmedian(np.array(rows, float), 0)


# --------------------------------------------------------------------------------- JSON views
def view_tilt(d, bands, caps, out=sys.stdout):
    b = np.asarray(bands, float)
    lo, hi = _sel(b, 80, 254), _sel(b, 2032, 5120)
    sweeps = d["meta"]["all_sweep_levels"]

    print("\n=== FR TILT (HF 2-5.1k minus LF 80-254 Hz), plugin-pedal, dB — drive x SWEEP LEVEL ===",
          file=out)
    print("  + = plugin hot at HF relative to LF. A LINEAR tilt is the same at every sweep level;\n"
          "  a COMPRESSION difference grows with level. That is the whole point of this table.",
          file=out)
    for mode in MODES:
        print(f"\n-- {mode}", file=out)
        print(f"{'drive':<8}" + "".join(f"{s.replace('sweep_', ''):>12}" for s in sweeps), file=out)
        rows = {}
        for c in caps:
            if c["rev"] != mode:
                continue
            for s in sweeps:
                e = err_raw(c, s)
                rows.setdefault(c["settings"]["drive"], {}).setdefault(s, []).append(
                    e[hi].mean() - e[lo].mean())
        for dv in sorted(rows):
            print(f"G{dv * 10:<7.0f}" + "".join(f"{np.median(rows[dv][s]):>+12.2f}" for s in sweeps),
                  file=out)

    print("\n=== FR error CURVE (mean-removed 80-5120), median over G2-G8, per mode/sweep ===", file=out)
    sel = _sel(b, LO, HI)
    print(f"{'mode/sweep':<20}" + "".join(f"{x:>7.0f}" for x in b[sel]), file=out)
    for mode in MODES:
        for s in sweeps:
            E = [(lambda e: e - e.mean())(err_raw(c, s)[sel])
                 for c in caps if c["rev"] == mode and c["settings"]["drive"] <= 0.85]
            print(f"{mode[:4] + ' ' + s.replace('sweep_', ''):<20}"
                  + "".join(f"{v:+7.2f}" for v in _med(E)), file=out)


def view_thd(d, bands, caps, out=sys.stdout):
    b = np.asarray(bands, float)
    mid = _sel(b, 403, 2560)
    print("\n=== THD deficit 20log10(plugin/pedal), dB — drive x band, sweep_drv_-6 (hot) ===", file=out)
    print("  - = plugin under-distorts. Last two columns are the ABSOLUTE mid-band THD (403-2560 Hz),\n"
          "  which is what shows whether THD responds to the DRIVE knob at all.", file=out)
    for mode in MODES:
        print(f"\n-- {mode}", file=out)
        print(f"{'drive':<8}" + "".join(f"{c[2]:>10}" for c in BANDS)
              + f"{'pedal%':>9}{'plugin%':>9}", file=out)
        drives = sorted({c["settings"]["drive"] for c in caps if c["rev"] == mode})
        for dv in drives:
            R, PE, PL = [], [], []
            for c in caps:
                if c["rev"] != mode or abs(c["settings"]["drive"] - dv) > 1e-9:
                    continue
                t = c["thd"]["sweep_drv_-6"]
                pl = np.array([x if x else np.nan for x in t["plugin_pct"]], float)
                pe = np.array([x if x else np.nan for x in t["pedal_pct"]], float)
                R.append(20 * np.log10(pl / pe))
                PE.append(pe)
                PL.append(pl)
            R, PE, PL = _med(R), _med(PE), _med(PL)
            print(f"G{dv * 10:<7.0f}"
                  + "".join(f"{np.nanmean(R[_sel(b, *c[:2])]):>+10.2f}" for c in BANDS)
                  + f"{np.nanmean(PE[mid]):>9.1f}{np.nanmean(PL[mid]):>9.1f}", file=out)


# ------------------------------------------------------------------------------ render views
def _pairs(render_dir):
    orig = A.load(A.ORIG)
    items = ONP.load_pairs(render_dir, orig)
    if not items:
        sys.exit(f"no render/capture pairs in {render_dir} — run\n"
                 f"  python3 analysis/comprehensive_report.py --keep-renders {render_dir}")
    for it in items:
        it["mode"] = {"Clean": "Boost", "OD": "Overdrive", "Dist": "Distortion"}.get(it["mode"],
                                                                                     it["mode"])
    return orig, items


def _curves(it, orig, sweep):
    """(freqs, Hn) for pedal and plugin on one sweep segment."""
    ref = A.seg_of(orig, sweep)
    fc, _, Hc = A.harmonic_thd_curve(A.seg_of(it["cap"], sweep), ref, max_order=7)
    fr, _, Hr = A.harmonic_thd_curve(A.seg_of(it["ren"], sweep), ref, max_order=7)
    return (fc, Hc), (fr, Hr)


def view_orders(render_dir, out=sys.stdout, sweep="sweep_drv_-6"):
    orig, items = _pairs(render_dir)
    anchors = np.array([100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0])
    print(f"\n=== PER-ORDER deficit 20log10(plugin/pedal) of Hn/H1, dB — {sweep} ===", file=out)
    print("  THE DISCRIMINATOR. Every order down by a similar amount => the clipper is not being\n"
          "  driven as hard (a level/drive-shaping error). High orders down while H2/H3 hold =>\n"
          "  the knee shape or the post-clip filtering. Median over drives G5-G10.", file=out)
    for mode in MODES:
        sel = [it for it in items if it["mode"] == mode and it["drive"] >= 0.5]
        if not sel:
            continue
        print(f"\n-- {mode}  (n={len(sel)})", file=out)
        print(f"{'order':<8}" + "".join(f"{a:>9.0f}" for a in anchors), file=out)
        acc = {n: [] for n in range(2, 8)}
        for it in sel:
            (fc, Hc), (fr, Hr) = _curves(it, orig, sweep)
            for n in range(2, 8):
                rc = np.interp(anchors, fc, Hc[n] / (Hc[1] + 1e-20))
                rr = np.interp(anchors, fr, Hr[n] / (Hr[1] + 1e-20))
                acc[n].append(20 * np.log10((rr + 1e-20) / (rc + 1e-20)))
        for n in range(2, 8):
            print(f"H{n:<7}" + "".join(f"{v:>+9.2f}" for v in _med(acc[n])), file=out)


def view_valid(render_dir, out=sys.stdout):
    orig, items = _pairs(render_dir)
    print("\n=== INSTRUMENT VALIDITY — THD of the CLEAN sweep, 250 Hz-2 kHz, % (plugin/pedal) ===",
          file=out)
    print("  P7 established the fit window on Boost alone. If OD's clean sweep is itself clipping,\n"
          "  a 'tilt' measured on it is a distortion reading, not linear EQ.", file=out)
    ref = A.seg_of(orig, "sweep_clean")
    grid = {}
    for it in items:
        fc, tc, _ = A.harmonic_thd_curve(A.seg_of(it["cap"], "sweep_clean"), ref, max_order=7)
        fr, tr, _ = A.harmonic_thd_curve(A.seg_of(it["ren"], "sweep_clean"), ref, max_order=7)
        m = (fc >= 250) & (fc <= 2000)
        grid.setdefault(it["drive"], {}).setdefault(it["mode"], []).append(
            (float(np.nanmean(tr[(fr >= 250) & (fr <= 2000)])), float(np.nanmean(tc[m]))))
    print(f"\n{'drive':<8}" + "".join(f"{m:>20}" for m in MODES), file=out)
    for dv in sorted(grid):
        row = f"G{dv * 10:<7.0f}"
        for m in MODES:
            v = grid[dv].get(m)
            row += f"{'-':>20}" if not v else "%20s" % (
                "%.2f / %.2f" % (float(np.median([x[0] for x in v])),
                                 float(np.median([x[1] for x in v]))))
        print(row, file=out)


def view_comp(render_dir, out=sys.stdout):
    orig, items = _pairs(render_dir)
    steps = list(gts.LEVEL_STEPS_DB)
    print("\n=== DYNAMICS — 1 kHz level steps, output dB re each curve's own -30 dB point ===", file=out)
    print("  Never audited before (FR_THD_AUDIT.md 'Axes never audited at all'). Captures are\n"
          "  per-mode level-NORMALISED, so only the SHAPE is comparable — hence the self-anchor.\n"
          "  'd' = plugin minus pedal: negative = the plugin compresses LESS than the pedal.", file=out)
    for mode in MODES:
        sel = [it for it in items if it["mode"] == mode]
        print(f"\n-- {mode}", file=out)
        print(f"{'drive':<8}" + "".join(f"{s:>8}" for s in steps), file=out)
        for dv in sorted({it["drive"] for it in sel}):
            rows = [it for it in sel if abs(it["drive"] - dv) < 1e-9]
            D = []
            for it in rows:
                pe = np.array([A.db(A.rms(A.seg_of(it["cap"], f"lvl_{s}"))) for s in steps])
                pl = np.array([A.db(A.rms(A.seg_of(it["ren"], f"lvl_{s}"))) for s in steps])
                D.append((pl - pl[0]) - (pe - pe[0]))
            print(f"G{dv * 10:<7.0f}" + "".join(f"{v:>+8.2f}" for v in _med(D)), file=out)


# ------------------------------------------------------- pre-clip LEVEL calibration (`gain`)
# P6's method, moved onto the harmonic axis. `orders` says WHAT is wrong (every odd order down by
# a similar amount => the clipper is under-driven); it cannot say by HOW MUCH, because harmonic dB
# and drive dB are different units. This view supplies the conversion by MEASURING it: render the
# same capture through flat pre-clip gains and read the slope d(Hn/H1)/d(gain) per band. Then
#     required pre-clip gain = -deficit / sensitivity
# is in dB of drive, per band — which is the shape a mechanism has to have.
#
# Odd orders only. The evens are an empirical injection (P2/P3/P3.2), so they measure that fit, not
# the clipper. Flat gain is the right probe precisely BECAUSE it is flat: if the answer comes back
# frequency-dependent, the defect is a tilt, and if it comes back flat it is P6's residual.
ODD_ORDERS = (3, 5, 7)
GAIN_ANCHORS = np.array([100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0])
GAIN_SUBSET = ("G5 T5 OD", "G6 T5 OD", "G7 T5 OD", "G8 T5 OD", "G10 T5 OD",
               "G5 T2 OD", "G6 T8 OD", "G8 T2 OD",
               "G6 T5 Dist", "G8 T5 Dist", "G6 T5 Clean", "G8 T5 Clean")


def _odd_ratio_db(sig, ref, sweep):
    """Median over the odd orders of 20log10(Hn/H1) at the anchors, dB — one row per anchor."""
    f, _, H = A.harmonic_thd_curve(A.seg_of(sig, sweep), A.seg_of(ref, sweep), max_order=7)
    rows = [20 * np.log10(np.interp(GAIN_ANCHORS, f, H[n] / (H[1] + 1e-20)) + 1e-20)
            for n in ODD_ORDERS]
    return np.median(np.array(rows, float), 0)


def _render(parsed, ref_path, binpath, os_factor):
    import subprocess
    import tempfile
    import captures as C  # noqa: E402  (local: only this view needs the render path)
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "r.wav")
        cmd = [binpath, ref_path, out, *C.render_args(parsed), "render",
               str(C.OS_FACTOR_TO_INDEX.get(os_factor, 3))]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(f"  ! render failed: {r.stderr.strip() or r.stdout.strip()}\n")
            return None
        return A.load_mono(out)


def view_gain(binpath, os_factor, gains, sweep="sweep_drv_-6", out=sys.stdout):
    import concurrent.futures as cf
    import tempfile
    import captures as C            # noqa: E402
    import p6_peak_fit as P6        # noqa: E402  (reuse its scaled-reference writer)

    files = {p_["label"]: (p, p_) for p, p_ in C.find_captures() if p_["label"] in GAIN_SUBSET}
    orig = A.load_mono(A.ORIG)

    # pedal side (gain-independent)
    ped = {}
    for lab, (p, _) in files.items():
        cap, _ = A.align(A.load_mono(p), orig)
        ped[lab] = _odd_ratio_db(cap, orig, sweep)

    plug = {}
    with tempfile.TemporaryDirectory() as td:
        for g in gains:
            ref_path = P6.scaled_reference(g, td)
            ref = A.load_mono(ref_path)
            with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
                futs = {ex.submit(_render, pr, ref_path, binpath, os_factor): lab
                        for lab, (_, pr) in files.items()}
                for f in cf.as_completed(futs):
                    y = f.result()
                    if y is None:
                        continue
                    y, _ = A.align(y, ref)
                    plug[(futs[f], g)] = _odd_ratio_db(y, ref, sweep)

    g0 = gains[0]
    print(f"\n=== PRE-CLIP LEVEL REQUIREMENT — odd orders as a drive meter, {sweep} ===", file=out)
    print("  sens = d(odd Hn/H1)/d(input dB), MEASURED by re-rendering at +%s dB. It is the unit\n"
          "  conversion `orders` lacks. def = plugin-pedal odd-order deficit at %+g dB.\n"
          "  req  = -def/sens = dB of pre-clip level the plugin is SHORT of, per band.\n"
          "  A flat req = P6's residual (more drive). A req that RISES with frequency = a tilt."
          % ("/".join(f"{g:+g}" for g in gains[1:]), g0), file=out)
    print(f"\n{'capture':<12}{'row':<6}" + "".join(f"{a:>8.0f}" for a in GAIN_ANCHORS), file=out)
    for lab in [c for c in GAIN_SUBSET if c in ped and (c, g0) in plug]:
        d = plug[(lab, g0)] - ped[lab]
        S = []
        for g in gains[1:]:
            if (lab, g) in plug:
                S.append((plug[(lab, g)] - plug[(lab, g0)]) / (g - g0))
        s = np.mean(np.array(S, float), 0) if S else np.full(len(GAIN_ANCHORS), np.nan)
        with np.errstate(divide="ignore", invalid="ignore"):
            req = np.where(np.abs(s) > 0.05, -d / s, np.nan)
        print(f"{lab:<12}{'def':<6}" + "".join(f"{v:>+8.2f}" for v in d), file=out)
        print(f"{'':<12}{'sens':<6}" + "".join(f"{v:>8.2f}" for v in s), file=out)
        print(f"{'':<12}{'req':<6}" + "".join(
            ("     n/a" if not np.isfinite(v) else f"{v:>+8.2f}") for v in req), file=out)


def view_tones(render_dir, out=sys.stdout):
    """Discrete steady-state tones (-14 dBFS, gts.TONE_FREQS) — a cleaner ceiling probe than the
    sweeps because each is a fixed, known level (no continuous level change to fight the estimator
    with). Self-anchored per capture on the LOWEST tone (82.41 Hz, least shelved by Stage 1), so
    only the delta's shape vs frequency is read — the discriminator this view exists for: a missing
    OUTPUT ceiling is broadband (flat delta vs frequency); a missing/misfit EQ shelf is not."""
    orig, items = _pairs(render_dir)
    freqs = np.array(gts.TONE_FREQS, float)
    print("\n=== DISCRETE TONES, -14 dBFS — output level, plugin-minus-pedal dB, "
          "self-anchored on 82 Hz ===", file=out)
    print("  Flat across frequency => broadband (the P9 ceiling, showing at a second, steady-state\n"
          "  operating point). Rising/falling with frequency => Stage 1's shelf, not the ceiling.",
          file=out)
    for mode in MODES:
        sel = [it for it in items if it["mode"] == mode]
        if not sel:
            continue
        print(f"\n-- {mode}", file=out)
        print(f"{'drive':<8}" + "".join(f"{f:>8.0f}" for f in freqs), file=out)
        rows = {}
        for it in sel:
            pe = np.array([A.db(A.rms(A.seg(it["cap"], f"tone_{f:g}"))) for f in gts.TONE_FREQS])
            pl = np.array([A.db(A.rms(A.seg(it["ren"], f"tone_{f:g}"))) for f in gts.TONE_FREQS])
            d = (pl - pl[0]) - (pe - pe[0])
            rows.setdefault(it["drive"], []).append(d)
        for dv in sorted(rows):
            print(f"G{dv * 10:<7.0f}" + "".join(f"{v:>+8.2f}" for v in _med(rows[dv])), file=out)


def view_decay(render_dir, out=sys.stdout, n_windows=8):
    """Plucked-note decay (decay_220, decay_1k) — a CONTINUOUS trajectory through the drive range
    within one segment, unlike the synthetic `comp` level steps. Self-anchored on the quietest
    (last) window, same logic as `comp`: if OD has no ceiling, the plugin should read hotter than
    the pedal in the early (loud) windows and converge as the note decays into the linear region."""
    orig, items = _pairs(render_dir)
    for name, f0 in (("decay_220", 220.0), ("decay_1k", 1000.0)):
        print(f"\n=== DECAY ENVELOPE — {name}, output level re each curve's own QUIETEST window, dB ===",
              file=out)
        print("  Window 0 = loudest (attack), last = quietest (tail). 'd' = plugin minus pedal:\n"
              "  positive & shrinking toward 0 = the plugin stays hot while the note is loud, same\n"
              "  shape as the `comp` level-step finding, seen here in a natural continuous decay.",
              file=out)
        for mode in MODES:
            sel = [it for it in items if it["mode"] == mode]
            if not sel:
                continue
            print(f"\n-- {mode}", file=out)
            print(f"{'drive':<8}" + "".join(f"{'w' + str(i):>8}" for i in range(n_windows)), file=out)
            rows = {}
            for it in sel:
                pe_rows = A.dynamics(it["cap"], name, f0, n_windows=n_windows)
                pl_rows = A.dynamics(it["ren"], name, f0, n_windows=n_windows)
                n = min(len(pe_rows), len(pl_rows))
                if n < 2:
                    continue
                pe = np.array([r[0] for r in pe_rows[:n]])
                pl = np.array([r[0] for r in pl_rows[:n]])
                d = (pl - pl[-1]) - (pe - pe[-1])
                rows.setdefault(it["drive"], []).append(d)
            for dv in sorted(rows):
                vals = _med(rows[dv])
                print(f"G{dv * 10:<7.0f}" + "".join(f"{v:>+8.2f}" for v in vals), file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("view", nargs="?", default="all",
                    choices=["tilt", "thd", "orders", "valid", "comp", "tones", "decay", "gain", "all"])
    ap.add_argument("--render-dir", default=DEFAULT_RENDERS)
    ap.add_argument("--sweep", default="sweep_drv_-6")
    ap.add_argument("--bin", default="build/PedalRender_artefacts/Release/PedalRender")
    ap.add_argument("--os", type=int, default=8, choices=(1, 2, 4, 8))
    ap.add_argument("--in-gain", nargs="*", type=float, default=[0.0, 2.0, 4.0])
    a = ap.parse_args()

    if a.view == "gain":
        view_gain(a.bin, a.os, a.in_gain, sweep=a.sweep)
        return

    if a.view in ("tilt", "thd", "all"):
        d, bands, caps = F.load()
        if a.view in ("tilt", "all"):
            view_tilt(d, bands, caps)
        if a.view in ("thd", "all"):
            view_thd(d, bands, caps)
    if a.view in ("valid", "all"):
        view_valid(a.render_dir)
    if a.view in ("orders", "all"):
        view_orders(a.render_dir, sweep=a.sweep)
    if a.view in ("comp", "all"):
        view_comp(a.render_dir)
    if a.view in ("tones", "all"):
        view_tones(a.render_dir)
    if a.view in ("decay", "all"):
        view_decay(a.render_dir)


if __name__ == "__main__":
    main()
