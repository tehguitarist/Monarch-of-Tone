#!/usr/bin/env python3
"""P2 fit/guard harness — asymmetric op-amp rail saturation (FR_THD_AUDIT.md P2).

A fast, focused loop for one question: how much op-amp rail asymmetry does the pedal's Boost
even-harmonic series demand, and does adding it cost anything in OD/Distortion or in the null?

comprehensive_report.py is the canonical instrument but renders 44 captures x 4 sweeps; this
renders a chosen subset once and prints exactly the three numbers P2 is fitted and guarded on:

  harm    H2-H7 re fundamental at the 100/200/400 Hz anchors, plugin vs pedal (same extraction
          as comprehensive_report.harmonics_at_anchors, so the numbers are comparable to
          `fr_thd_audit.py harm`)
  null    best-fit-gain time-domain null depth per sweep segment (the arbiter for anything that
          could carry phase — CLAUDE.md's metric lesson)
  bytes   SHA-256 of the render, for the OD/Distortion byte-identical guard

Usage:
  p2_rail_asym_fit.py                       # default subset: Boost + OD/Dist mid-gain controls
  p2_rail_asym_fit.py --modes Boost         # Boost only (the fit loop)
  p2_rail_asym_fit.py --save-renders DIR    # keep the WAVs (for the byte-identical guard)
  p2_rail_asym_fit.py --json OUT.json       # machine-readable, for A/B against a saved baseline
  p2_rail_asym_fit.py --compare BASE.json   # diff this run against a saved one

Needs the local-only captures in analysis/pedal_export2/ and a built tools/PedalRender.
"""
import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import analyze as A          # noqa: E402
import captures as C         # noqa: E402
import comprehensive_report as R  # noqa: E402
import null_test as NT      # noqa: E402

# Mid-gain controls per mode: enough to see the fit move without paying for all 44 captures.
DEFAULT_SUBSET = (
    "G2 T5 Clean", "G4 T5 Clean", "G5 T5 Clean", "G6 T5 Clean", "G8 T5 Clean", "G10 T5 Clean",
    "G5 T5 OD", "G7 T5 OD", "G5 T5 Dist", "G6 T5 Dist", "G7 T5 Dist",
)
NULL_SWEEPS = ("sweep_clean", "sweep_drv_-12", "sweep_drv_-6")
# Harmonics are read on the driven sweeps plus the quiet clean sweep (see analyse()).
HARM_SWEEPS = ("sweep_clean",) + R.DRIVEN_SWEEPS


def analyse(path, parsed, orig, binpath, os_factor, save_dir):
    """Render one capture's settings and return {harmonics, null, sha256}."""
    cached = R.get_pedal_features(path, orig, R.CACHE_DIR, True)
    if cached is None:
        return None
    cap_al, pedal_features = cached

    tmp = None
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        out_path = os.path.join(save_dir, parsed["label"].replace(" ", "_") + ".wav")
    else:
        tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
        out_path = tmp.name
        tmp.close()
    try:
        if not R.render_plugin(binpath, C.render_args(parsed), out_path, os_factor):
            return None
        sha = hashlib.sha256(open(out_path, "rb").read()).hexdigest()
        ren = A.load(out_path)
        ren_al, _ = A.align(ren, orig)

        # Harmonics on the DRIVEN sweeps (what P2 is fitted to) AND on the quiet clean sweep — the
        # clean sweep is where the rail asymmetry costs null depth, so we need to know whether the
        # pedal has even harmonics down there at all. If it does, the asymmetry belongs at every
        # level; if it does not, the asymmetry must be gated by clip depth.
        harm = {}
        for sw in HARM_SWEEPS:
            cap_farina = (pedal_features["farina"][sw] if sw in pedal_features["farina"]
                          else A.harmonic_thd_curve(A.seg_of(cap_al, sw), A.seg_of(orig, sw),
                                                    max_order=7))
            ren_farina = A.harmonic_thd_curve(A.seg_of(ren_al, sw), A.seg_of(orig, sw), max_order=7)
            harm[sw] = R.harmonics_at_anchors(cap_farina, ren_farina)

        # Null depth per sweep segment — null_test.best_null (integer + fractional lag + LS gain),
        # the same instrument run_validation.py uses. NOT analyze.null_depth, which skips alignment.
        null = {}
        for sw in NULL_SWEEPS:
            ref, test = A.seg_of(cap_al, sw), A.seg_of(ren_al, sw)
            n = min(len(ref), len(test))
            ref, test = ref[:n], test[:n]
            resid, _ = NT.best_null(ref, test)
            null[sw] = float(NT.null_db(ref, resid))

        # IMD, pedal vs plugin. A single swept sine cannot see intermodulation at all, which is
        # the standing risk of fitting harmonic SHAPE with a harder-kneed shaper (FR_THD_AUDIT.md
        # P3.1 route 2's caution). The test signal already carries both twin-tone segments, so the
        # guard is capture-referenced rather than by ear: SMPTE 60 Hz + 7 kHz straddles exactly the
        # low/mid injection split, CCIF 19+20 kHz catches anything aliasing.
        imd = {nm: {"pedal": float(A.imd(cap_al, nm, lo, hi)),
                    "plugin": float(A.imd(ren_al, nm, lo, hi))}
               for nm, lo, hi in (("imd_smpte", 60, 7000), ("imd_ccif", 19000, 20000))}

        return {"id": parsed["label"], "rev": parsed["rev"], "sha256": sha,
                "harmonics": harm, "null": null, "imd": imd}
    finally:
        if tmp and os.path.exists(out_path):
            os.unlink(out_path)


def print_harm(results, out=sys.stdout):
    print("\n=== H2-H7 re fundamental (dB) — plugin / pedal / delta, median per mode ===", file=out)
    for sw in HARM_SWEEPS:
        modes = [m for m in ("Boost", "Overdrive", "Distortion")
                 if any(r["rev"] == m for r in results)]
        print(f"--- {sw}", file=out)
        print(f"{'mode':>6}{'H':>4}" + "".join(f"{str(a) + ' Hz':>23}" for a in R.THD_ANCHORS),
              file=out)
        for m in modes:
            sel = [r for r in results if r["rev"] == m]
            for o in range(2, 8):
                row = f"{m[:6]:>6}{'H' + str(o):>4}"
                for ai in range(len(R.THD_ANCHORS)):
                    p = np.median([r["harmonics"][sw][f"H{o}"]["plugin_db"][ai] for r in sel])
                    q = np.median([r["harmonics"][sw][f"H{o}"]["pedal_db"][ai] for r in sel])
                    row += f"  {p:>7.1f}/{q:>7.1f} {p - q:>+6.1f}"
                print(row, file=out)
            print(file=out)


def print_null(results, out=sys.stdout):
    print("=== Null depth (dB, best-fit gain; more negative = better) ===", file=out)
    print(f"{'capture':<14}" + "".join(f"{s.replace('sweep_', ''):>14}" for s in NULL_SWEEPS),
          file=out)
    for r in results:
        print(f"{r['id']:<14}" + "".join(f"{r['null'][s]:>14.2f}" for s in NULL_SWEEPS), file=out)
    print(f"{'MEAN':<14}"
          + "".join(f"{np.mean([r['null'][s] for r in results]):>14.2f}" for s in NULL_SWEEPS),
          file=out)


def print_imd(results, out=sys.stdout):
    print("\n=== IMD products re carriers (dB) — plugin / pedal / delta ===", file=out)
    names = ("imd_smpte", "imd_ccif")
    print(f"{'capture':<14}" + "".join(f"{n.replace('imd_', ''):>26}" for n in names), file=out)
    for r in results:
        if "imd" not in r:
            continue
        row = f"{r['id']:<14}"
        for n in names:
            p, q = r["imd"][n]["plugin"], r["imd"][n]["pedal"]
            row += f"  {p:>8.1f}/{q:>8.1f} {p - q:>+6.1f}"
        print(row, file=out)


def print_compare(results, base, out=sys.stdout):
    """Delta vs a saved run. Negative null delta = improvement; harmonic delta = |plugin-pedal|
    error change (negative = closer to the pedal)."""
    by_id = {r["id"]: r for r in base}
    print("\n=== vs baseline — null delta (dB; negative = deeper/better) ===", file=out)
    print(f"{'capture':<14}" + "".join(f"{s.replace('sweep_', ''):>14}" for s in NULL_SWEEPS),
          file=out)
    acc = {s: [] for s in NULL_SWEEPS}
    for r in results:
        b = by_id.get(r["id"])
        if b is None:
            continue
        row = f"{r['id']:<14}"
        for s in NULL_SWEEPS:
            d = r["null"][s] - b["null"][s]
            acc[s].append(d)
            row += f"{d:>+14.2f}"
        print(row, file=out)
    print(f"{'MEAN':<14}" + "".join(f"{np.mean(acc[s]):>+14.2f}" for s in NULL_SWEEPS), file=out)

    print("\n=== vs baseline — |harmonic error| change, median per mode (dB; negative = better) ===",
          file=out)
    for sw in HARM_SWEEPS:
        # A baseline saved before a sweep was added to HARM_SWEEPS simply has no row for it.
        if not all(sw in b["harmonics"] for b in by_id.values()):
            print(f"--- {sw}   (not in baseline — skipped)", file=out)
            continue
        print(f"--- {sw}", file=out)
        for m in sorted({r["rev"] for r in results}):
            sel = [r for r in results if r["rev"] == m and r["id"] in by_id]
            if not sel:
                continue
            row = f"{m[:6]:>6}  "
            for o in range(2, 8):
                d = []
                for r in sel:
                    b = by_id[r["id"]]
                    for ai in range(len(R.THD_ANCHORS)):
                        h, hb = r["harmonics"][sw][f"H{o}"], b["harmonics"][sw][f"H{o}"]
                        d.append(abs(h["plugin_db"][ai] - h["pedal_db"][ai])
                                 - abs(hb["plugin_db"][ai] - hb["pedal_db"][ai]))
                row += f"H{o} {np.median(d):>+6.2f}  "
            print(row, file=out)

    print("\n=== vs baseline — |IMD error| change (dB; negative = closer to the pedal) ===",
          file=out)
    for nm in ("imd_smpte", "imd_ccif"):
        d = [abs(r["imd"][nm]["plugin"] - r["imd"][nm]["pedal"])
             - abs(by_id[r["id"]]["imd"][nm]["plugin"] - by_id[r["id"]]["imd"][nm]["pedal"])
             for r in results if r["id"] in by_id and "imd" in r and "imd" in by_id[r["id"]]]
        print(f"  {nm:<10} " + ("no baseline" if not d else
                                f"median {np.median(d):>+6.2f}   worst {max(d):>+6.2f}"), file=out)

    print("\n=== byte-identical guard (renders unchanged vs baseline) ===", file=out)
    for r in results:
        b = by_id.get(r["id"])
        if b is None:
            continue
        same = "IDENTICAL" if b["sha256"] == r["sha256"] else "changed"
        print(f"  {r['id']:<14} {same}", file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--os", type=int, default=8, choices=sorted(C.OS_FACTOR_TO_INDEX))
    ap.add_argument("--modes", nargs="*", default=None,
                    help="restrict to these modes (Boost/Overdrive/Distortion)")
    ap.add_argument("--captures", nargs="*", default=list(DEFAULT_SUBSET),
                    help="capture labels, or 'all'")
    ap.add_argument("--anchors", nargs="*", type=int, default=None,
                    help="anchor frequencies for the harmonic table (default: "
                         "comprehensive_report.THD_ANCHORS). P3.1 fits the mid/high injection "
                         "path, which owns the band ABOVE asymMidFc — add 800 to see it.")
    ap.add_argument("--save-renders", default=None)
    ap.add_argument("--json", default=None, help="write results here")
    ap.add_argument("--compare", default=None, help="diff against a saved --json run")
    ap.add_argument("--jobs", type=int, default=None)
    a = ap.parse_args()

    # harmonics_at_anchors() reads this module-level tuple at call time, so overriding it here
    # re-anchors both the table and the saved JSON (a --compare baseline must use the same set).
    if a.anchors:
        R.THD_ANCHORS = tuple(a.anchors)

    if not os.path.exists(a.bin):
        sys.exit(f"PedalRender not found at {a.bin} — cmake --build build --target PedalRender")
    orig = A.load(A.ORIG)

    found = C.find_captures()
    wanted = None if a.captures == ["all"] else set(a.captures)
    todo = [(p, q) for p, q in found
            if (wanted is None or q["label"] in wanted)
            and (a.modes is None or q["rev"] in a.modes)]
    if not todo:
        sys.exit("no captures matched")

    jobs = a.jobs or R.default_jobs()
    results = []
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = {ex.submit(analyse, p, q, orig, a.bin, a.os, a.save_renders): q["label"]
                for p, q in todo}
        for f in cf.as_completed(futs):
            r = f.result()
            if r:
                results.append(r)
            print(f"  rendered {futs[f]}", file=sys.stderr)
    results.sort(key=lambda r: (r["rev"], r["id"]))

    print_harm(results)
    print_null(results)
    print_imd(results)
    if a.compare:
        print_compare(results, json.load(open(a.compare)))
    if a.json:
        json.dump(results, open(a.json, "w"), indent=1)
        print(f"\nwrote {a.json}")


if __name__ == "__main__":
    main()
