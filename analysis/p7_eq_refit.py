#!/usr/bin/env python3
"""P7 — refit the drive-keyed EQ instruments as ONE set (FR_THD_AUDIT.md P7).

`shelfMaxDb`/`shelfSlopeDb` (450 Hz treble lift), `bassCut*` (185 Hz bell) and `bassBoost*`
(105 Hz low-shelf) all act on the same low-drive tilt over the same knob range, and were all fit
2026-06-29/07-04 — before the warp recalibration, `hfTrim` and P6 landed in overlapping territory.
This harness re-reads all three TOGETHER, because measuring any one of them alone attributes the
whole error to it (which is how P4 concluded the treble lift was 100 % spurious when the bell was
supplying half the same correction).

Reads analysis/reports/comprehensive_data.json only (no rendering, <1 s).

--------------------------------------------------------------------------------------------
INSTRUMENT VALIDITY — read this before trusting any row (this is what P7 added to the rulebook).

P4's rule was "fit on the least-nonlinear cell: Boost + clean sweep". That rule is necessary but
NOT sufficient: the clean sweep stops being linear part-way up the DRIVE knob. Measured THD of the
clean sweep, 250 Hz-2 kHz, plugin / pedal:

    G2 0.08/0.87   G3 0.09/0.58   G4 0.10/0.64   G5 0.11/0.79   G6 0.43/1.11
    G7 4.64/4.36   G8 7.63/7.71   G10 10.45/14.76        <-- NOT a linear FR measurement

So the fit window is **G2-G6**. G7 up is reported as a consequence and never scored. A defect
"found" at G8/G10 on the clean sweep is a distortion-spectrum difference read through an H1
estimator, not linear EQ — which is what P10's "+4.9 dB G10 Boost discontinuity" actually is.

LEVEL IS NOT SHAPE. Every error curve is mean-removed over the scored band: overall level is a
separately-calibrated axis (VOL taper / best-fit gain) and a shape fit must not chase it.

BAND. 63 Hz-5120 Hz. Below 63 Hz is P8's phase-limited deficit (different problem, different
instrument); above 5120 Hz the captures have +-18 dB spread and the warp shelf lives at 6.5 kHz.

THIS SCRIPT DOES NOT DECIDE ANYTHING. It fits a shape on the linear instrument. The driven
time-domain null across all 44 captures is the arbiter, and it needs a rebuild.
--------------------------------------------------------------------------------------------

Usage:
  p7_eq_refit.py raw        # the defect with the drive-keyed set REMOVED — read this first
  p7_eq_refit.py seesaw     # the same defect as one number per drive: it is a see-saw about 508 Hz
  p7_eq_refit.py fit        # fit the replacement two-instrument model on G2-G6
  p7_eq_refit.py score --set shelfMaxDb=0 ...   # residual for any candidate constant set
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fr_thd_audit as A  # noqa: E402

BAND_LO, BAND_HI = 63.0, 5120.0
FIT_DRIVES = (0.2, 0.3, 0.4, 0.5, 0.6)      # G2-G6: clean sweep still linear (see docstring)
LOWMID_HZ, PIVOT_HZ, HF_HZ = 202.0, 508.0, 3225.0
FS_BASE = 48000.0

# The drive-keyed set as shipped BEFORE P7 (fit 2026-06-29 / 07-04). Kept so the finding stays
# reproducible against a pre-P7 comprehensive_data.json once the header has moved on:
#     p7_eq_refit.py raw --base pre-p7
PRE_P7 = dict(shelfPivotHz=450.0, shelfMaxDb=5.6, shelfSlopeDb=11.8,
              bassCutPivotHz=185.0, bassCutQ=0.45, bassCutOffDrive=0.5,
              bassCutSlopeDb=13.0, bassCutMaxDb=4.6)


def instrument(d, bands, caps, mode="Boost", sweep="sweep_clean"):
    b = np.asarray(bands, float)
    return b, [(c["settings"]["drive"], c["id"],
                np.array(c["fr"][sweep]["plugin_db"], float) - np.array(c["fr"][sweep]["pedal_db"], float))
               for c in sorted(caps, key=A.sort_key) if c["rev"] == mode]


def _sel(bands, lo, hi):
    return (bands >= lo) & (bands <= hi)


# ------------------------------------------------------------------------- the two shelf "sets"
def old_set_db(K, drive01, freqs, fs):
    """The three drive-keyed instruments as currently shipped. Excludes warp + hfTrim, which are
    drive-INDEPENDENT and cancel exactly in the new-minus-old delta scored here."""
    treble = max(0.0, K["shelfMaxDb"] - K["shelfSlopeDb"] * drive01)
    boost = min(K["bassBoostMaxDb"], max(0.0, K["bassBoostSlopeDb"] * (drive01 - K["bassOnsetDrive"])))
    cut = -min(K["bassCutMaxDb"], max(0.0, K["bassCutSlopeDb"] * (K["bassCutOffDrive"] - drive01)))
    m = A._mag1(*A._shelf(1.0, 10 ** (treble / 20), K["shelfPivotHz"], fs), freqs, fs)
    m = m * A._mag1(*A._shelf(10 ** (boost / 20), 1.0, K["bassPivotHz"], fs), freqs, fs)
    m = m * A._mag2(*A._peak(K["bassCutPivotHz"], cut, K["bassCutQ"], fs), freqs, fs)
    return 20.0 * np.log10(m)


def new_set_db(P, drive01, freqs, fs):
    """Candidate = the SAME three instruments, same laws, different constants.

    The see-saw view shows the shipped law SHAPE is already right — `max(0, max - slope*drive)`
    saturating at a zero near G5-G6 reproduces the measured tilt trajectory (+3.95/+2.73/+1.25/
    +0.20/+0.02 at G2-G6) almost exactly. What is wrong is the MAGNITUDE: the treble lift and the
    bass-cut bell each supply about half of one see-saw and were fit independently, so together
    they deliver ~6.6 dB of tilt at G2 where 3.95 dB is needed. Refitting in the shipped
    parameterisation keeps the change reviewable and keeps `raw`/`drive_shelf_db` honest.
    """
    return old_set_db(P, drive01, freqs, fs)


def residual(rows, bands, K, fs, sel, new=None, drives=None):
    """Per-row predicted residual after replacing the old set with `new` (None = leave as shipped)."""
    out, meta = [], []
    for drive, cid, e in rows:
        if drives is not None and not any(abs(drive - d) < 1e-9 for d in drives):
            continue
        delta = -old_set_db(K, drive, bands, fs)
        if new is not None:
            delta = delta + new_set_db(new, drive, bands, fs)
        else:
            delta = np.zeros_like(delta)
        r = (e + delta)[sel]
        out.append(r - np.mean(r))
        meta.append((drive, cid))
    return np.array(out, float), meta


# ------------------------------------------------------------------------------------------ views
def view_raw(rows, bands, K, fs, out=sys.stdout):
    sel = _sel(bands, 20.0, BAND_HI)
    fit = _sel(bands, BAND_LO, BAND_HI)
    print("\n=== RAW plugin defect — the drive-keyed EQ set REMOVED (dB, mean-removed 63-5120) ===",
          file=out)
    print("  + = plugin hot. This is what the three instruments are actually there to correct.\n"
          "  G7+ rows are marked: the clean sweep carries 4.6-10 % THD there, so they are NOT a\n"
          "  linear FR measurement and are never scored.", file=out)
    print(f"\n{'drive':<9}" + "".join(f"{b:>7.0f}" for b in bands[sel]), file=out)
    for dv in sorted({r[0] for r in rows}):
        same = np.array([r[2] for r in rows if abs(r[0] - dv) < 1e-9])
        r = np.median(same, 0) - old_set_db(K, dv, bands, fs)
        r = r - np.mean(r[fit])
        tag = "G%-4.0f%s" % (dv * 10, " " if dv in FIT_DRIVES else "*")
        print(f"{tag:<9}" + "".join(f"{v:+7.2f}" for v in r[sel]), file=out)
    print("\n  * = excluded from the fit (clean sweep no longer linear)", file=out)


def view_seesaw(rows, bands, K, fs, out=sys.stdout):
    """Collapse the defect to the one number that describes it: the tilt about ~508 Hz."""
    fit = _sel(bands, BAND_LO, BAND_HI)
    ix = {h: int(np.argmin(np.abs(bands - h))) for h in (LOWMID_HZ, PIVOT_HZ, HF_HZ)}
    print(f"\n=== the defect is ONE see-saw about {bands[ix[PIVOT_HZ]]:.0f} Hz ===", file=out)
    print(f"  correction the plugin NEEDS (= -defect), dB, at three anchors.\n"
          f"  Note the {bands[ix[PIVOT_HZ]]:.0f} Hz column: near-constant at every drive. That is "
          f"the pivot.\n", file=out)
    print(f"{'drive':<8}{f'{bands[ix[LOWMID_HZ]]:.0f} Hz':>10}{f'{bands[ix[PIVOT_HZ]]:.0f} Hz':>10}"
          f"{f'{bands[ix[HF_HZ]]:.0f} Hz':>10}{'HF-LF tilt':>13}", file=out)
    for dv in sorted({r[0] for r in rows}):
        same = np.array([r[2] for r in rows if abs(r[0] - dv) < 1e-9])
        r = np.median(same, 0) - old_set_db(K, dv, bands, fs)
        r = -(r - np.mean(r[fit]))
        lo, mid, hi = r[ix[LOWMID_HZ]], r[ix[PIVOT_HZ]], r[ix[HF_HZ]]
        tag = "G%-3.0f%s" % (dv * 10, " " if dv in FIT_DRIVES else "*")
        print(f"{tag:<8}{lo:>+10.2f}{mid:>+10.2f}{hi:>+10.2f}{hi-lo:>+13.2f}", file=out)
    print("\n  * = clean sweep not linear at this drive; not scored.", file=out)


def view_fit(rows, bands, K, fs, out=sys.stdout):
    sel = _sel(bands, BAND_LO, BAND_HI)
    base, _ = residual(rows, bands, K, fs, sel, None, FIT_DRIVES)
    print(f"\n=== FIT on G2-G6 Boost/clean, scored {BAND_LO:.0f}-{BAND_HI:.0f} Hz ===", file=out)
    print(f"  as shipped:            rms {np.sqrt(np.mean(base**2)):.3f} dB", file=out)
    strip = dict(K, shelfMaxDb=0.0, shelfSlopeDb=0.0, bassCutMaxDb=0.0, bassCutSlopeDb=0.0,
                 bassBoostMaxDb=0.0, bassBoostSlopeDb=0.0)
    r0, _ = residual(rows, bands, K, fs, sel, None, FIT_DRIVES)
    raw = np.array([(e - old_set_db(K, dv, bands, fs))[sel] for dv, _, e in rows
                    if any(abs(dv - f) < 1e-9 for f in FIT_DRIVES)])
    raw = raw - raw.mean(1, keepdims=True)
    print(f"  all three REMOVED:     rms {np.sqrt(np.mean(raw**2)):.3f} dB", file=out)

    fits = [(dv, cid, e) for dv, cid, e in rows if any(abs(dv - f) < 1e-9 for f in FIT_DRIVES)]
    # pre-strip the shipped set once per fit row; the search then only ADDS a candidate set
    stripped = [(dv, (e - old_set_db(K, dv, bands, fs))) for dv, cid, e in fits]

    def rms_of(P):
        tot = 0.0
        for dv, base in stripped:
            r = (base + old_set_db(P, dv, bands, fs))[sel]
            tot += np.mean((r - r.mean()) ** 2)
        return float(np.sqrt(tot / len(stripped)))

    print(f"\n  --- coarse: keep every shape, scale the two low-drive instruments together ---",
          file=out)
    print(f"  {'scale':>7}{'rms':>8}", file=out)
    for s in np.arange(0.3, 1.21, 0.1):
        P = dict(K, shelfMaxDb=K["shelfMaxDb"] * s, shelfSlopeDb=K["shelfSlopeDb"] * s,
                 bassCutMaxDb=K["bassCutMaxDb"] * s, bassCutSlopeDb=K["bassCutSlopeDb"] * s)
        print(f"  {s:>7.2f}{rms_of(P):>8.3f}", file=out)

    print(f"\n  --- free: refit the drive-keyed constants on G2-G6 ---", file=out)
    best = []
    for spiv in (350.0, 450.0, 600.0, 800.0):
        for smax in np.arange(0.0, 5.01, 0.25):
            for szero in (0.40, 0.45, 0.475, 0.50, 0.55, 0.60):
                ssl = smax / szero if szero else 0.0
                for cpiv in (160.0, 185.0, 210.0, 240.0):
                    for cq in (0.45, 0.6, 0.8):
                        for cmax in np.arange(0.0, 5.01, 0.25):
                            for czero in (0.45, 0.5, 0.55, 0.6):
                                P = dict(K, shelfPivotHz=spiv, shelfMaxDb=float(smax),
                                         shelfSlopeDb=float(ssl), bassCutPivotHz=cpiv, bassCutQ=cq,
                                         bassCutMaxDb=float(cmax), bassCutOffDrive=czero,
                                         bassCutSlopeDb=float(cmax / czero) if czero else 0.0)
                                best.append((rms_of(P), spiv, float(smax), float(szero),
                                             cpiv, cq, float(cmax), czero))
    best.sort()
    print(f"  {'rms':>7}{'sPivot':>8}{'sMax':>7}{'sZero':>7}{'cPivot':>8}{'cQ':>6}{'cMax':>7}"
          f"{'cZero':>7}   (best 15)", file=out)
    for b in best[:15]:
        print(f"  {b[0]:>7.3f}{b[1]:>8.0f}{b[2]:>7.2f}{b[3]:>7.3f}{b[4]:>8.0f}{b[5]:>6.2f}"
              f"{b[6]:>7.2f}{b[7]:>7.2f}", file=out)
    b = best[0]
    return dict(K, shelfPivotHz=b[1], shelfMaxDb=b[2], shelfSlopeDb=b[2] / b[3],
                bassCutPivotHz=b[4], bassCutQ=b[5], bassCutMaxDb=b[6], bassCutOffDrive=b[7],
                bassCutSlopeDb=b[6] / b[7])


def view_score(rows, bands, K, fs, P, out=sys.stdout):
    sel = _sel(bands, BAND_LO, BAND_HI)
    low = _sel(bands, 20.0, 50.4)
    R0, meta = residual(rows, bands, K, fs, sel, None)
    R1, _ = residual(rows, bands, K, fs, sel, P)
    L0, _ = residual(rows, bands, K, fs, low, None)
    L1, _ = residual(rows, bands, K, fs, low, P)
    print(f"\n=== candidate residual, Boost/clean, {BAND_LO:.0f}-{BAND_HI:.0f} Hz ===", file=out)
    for k in sorted(set(K) & set(P)):
        if abs(K[k] - P[k]) > 1e-9:
            print(f"  {k:<20} {K[k]:>8.2f} -> {P[k]:>8.2f}", file=out)
    print(f"\n{'capture':<15}{'before':>9}{'after':>9}{'delta':>9}   {'20-50Hz before':>15}{'after':>9}",
          file=out)
    ins = []
    for i, (dv, cid) in enumerate(meta):
        r0, r1 = np.sqrt(np.mean(R0[i] ** 2)), np.sqrt(np.mean(R1[i] ** 2))
        tag = "" if dv in FIT_DRIVES else " *"
        print(f"{cid+tag:<15}{r0:>9.3f}{r1:>9.3f}{r1-r0:>+9.3f}   "
              f"{np.sqrt(np.mean(L0[i]**2)):>15.3f}{np.sqrt(np.mean(L1[i]**2)):>9.3f}", file=out)
        if dv in FIT_DRIVES:
            ins.append(i)
    print(f"\n{'FIT ROWS (G2-G6)':<15}{np.sqrt(np.mean(R0[ins]**2)):>9.3f}"
          f"{np.sqrt(np.mean(R1[ins]**2)):>9.3f}"
          f"{np.sqrt(np.mean(R1[ins]**2))-np.sqrt(np.mean(R0[ins]**2)):>+9.3f}", file=out)
    print(f"{'ALL ROWS':<15}{np.sqrt(np.mean(R0**2)):>9.3f}{np.sqrt(np.mean(R1**2)):>9.3f}"
          f"{np.sqrt(np.mean(R1**2))-np.sqrt(np.mean(R0**2)):>+9.3f}", file=out)
    print("\n  * = not in the fit window (clean sweep not linear); consequence only.", file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("view", nargs="?", default="raw",
                    choices=["raw", "seesaw", "fit", "score", "all"])
    ap.add_argument("--set", action="append", default=[],
                    help="candidate constant, e.g. --set bassCutQ=0.5 (repeatable)")
    ap.add_argument("--base", action="append", default=[],
                    help="constant the JSON's renders were made WITH, when that is no longer what "
                         "the header says (repeatable). Default: the header, which is only correct "
                         "while comprehensive_data.json is in sync with the working tree. "
                         "Shorthand `--base pre-p7` loads the pre-P7 shipped set.")
    ap.add_argument("--mode", default="Boost")
    ap.add_argument("--sweep", default="sweep_clean")
    a = ap.parse_args()

    d, bands, caps = A.load()
    K = A.channel_consts()
    fs = FS_BASE * float(d["meta"]["os_factor"])
    bands, rows = instrument(d, bands, caps, a.mode, a.sweep)

    # The candidate defaults to the WORKING TREE's constants; K must describe the build that
    # produced the JSON, or every view lies. They are the same thing only while the two are in sync.
    P = dict(K)
    for s in a.base:
        if s == "pre-p7":
            K.update(PRE_P7)
        else:
            k, v = s.split("=")
            K[k] = float(v)
    for s in a.set:
        k, v = s.split("=")
        P[k] = float(v)

    if a.view in ("raw", "all"):
        view_raw(rows, bands, K, fs)
    if a.view in ("seesaw", "all"):
        view_seesaw(rows, bands, K, fs)
    if a.view in ("fit", "all"):
        P = view_fit(rows, bands, K, fs)
    if a.view in ("score", "all"):
        view_score(rows, bands, K, fs, P)


if __name__ == "__main__":
    main()
