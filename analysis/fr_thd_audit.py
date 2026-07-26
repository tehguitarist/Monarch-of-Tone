#!/usr/bin/env python3
"""FR / THD / harmonic audit — plugin vs the 44 real-KOT captures, read off the comprehensive report.

Built 2026-07-26 to settle four suspected shortcomings spotted by eye in the comprehensive dashboard
(light 20-80 Hz, hot above 800 Hz, wandering FR peak, missing 6-8.5 kHz THD). Findings + the
resulting work plan live in analysis/FR_THD_AUDIT.md — this script regenerates every table in it.

Most views read analysis/reports/comprehensive_data.json only (fast, no rendering). The `h2` view
re-renders a few settings through tools/PedalRender because H2-vs-frequency is not in the JSON.

The guardrails this tool encodes (they are why the four observations do NOT all survive):

  G1  Strip the correction shelves before blaming the circuit. `raw` evaluates driveShelf()'s exact
      coefficients (parsed live from MonarchChannel.h, so it can't drift) and removes them from the
      measured plugin FR. A deficit that only exists WITH the shelves is a mis-tuned shelf; one that
      survives removal is a circuit/topology gap. This is what separated the sub-64 Hz shortfall
      (real, in the raw circuit, at every drive) from the 100-330 Hz bump (already corrected).

  G2  Normalize at 1 kHz, not by best-fit gain. comprehensive_report.py's `plugin_db` carries a
      time-domain best-fit-gain offset, which spreads a pure tilt across the whole spectrum and
      makes both ends look wrong. Every table here re-anchors at 1 kHz so a tilt reads as a tilt.

  G3  Check whether an error tracks the knob before calling it knob variance. `bands --by tone`
      groups by tone position; the >800 Hz excess does NOT track it (see the doc).

  G4  Know where the measurement dies. `alias` shows that the discrete-tone THD estimator is invalid
      at 6 and 8 kHz at FS=48k (harmonics fold onto the fundamental), and the swept Farina bands
      above ~5 kHz are H2-only and read inconsistently between adjacent bands of one capture. FR
      above ~8 kHz has a ±18 dB capture-side spread. Do not fit anything to those bands.

Usage:
  fr_thd_audit.py bands [--by drive|tone|mode] [--sweep NAME]   1 kHz-normalized FR error grid
  fr_thd_audit.py raw                                          FR error with driveShelf() removed
  fr_thd_audit.py peaks [--sweep NAME]                          overall-FR peak freq, plugin vs pedal
  fr_thd_audit.py thd                                           THD ratio per band per mode
  fr_thd_audit.py harm                                          H2-H7 at the 100/200/400 Hz anchors
  fr_thd_audit.py alias                                         where discrete-tone harmonics land
  fr_thd_audit.py h2                                            H2 vs frequency (renders; needs PedalRender)
  fr_thd_audit.py all                                           every view above except h2
  fr_thd_audit.py --report                                      write analysis/FR_THD_AUDIT.md tables

Requires analysis/reports/comprehensive_data.json (run comprehensive_report.py first). The `h2` view
additionally needs the local-only captures in analysis/pedal_export2/ and a built tools/PedalRender.
"""
import argparse
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(HERE, "reports", "comprehensive_data.json")
CHANNEL_H = os.path.join(ROOT, "src", "dsp", "MonarchChannel.h")

ANCHOR_HZ = 1015.9   # the 1/3-octave band the tables normalize at
MODES = ("Boost", "Overdrive", "Distortion")


# ------------------------------------------------------------------------------------ data access
def load():
    import json
    if not os.path.exists(DATA):
        sys.exit(f"{DATA} not found — run: python3 analysis/comprehensive_report.py")
    d = json.load(open(DATA))
    return d, np.array(d["meta"]["bands"]), d["captures"]


def anchor_index(bands):
    return int(np.argmin(np.abs(bands - ANCHOR_HZ)))


def err_db(cap, sweep, bands, ai):
    """plugin − pedal, dB, re-anchored at 1 kHz (G2 — undoes the report's best-fit-gain offset)."""
    f = cap["fr"][sweep]
    e = np.array(f["plugin_db"], float) - np.array(f["pedal_db"], float)
    return e - e[ai]


def sort_key(c):
    return (c["rev"], c["settings"]["drive"], c["settings"]["tone"])


# ------------------------------------------------- driveShelf() magnitude response (guardrail G1)
def channel_consts():
    """Parse the shelf constants out of MonarchChannel.h so this audit can never drift from the
    shipped values (a hardcoded copy would silently lie the moment a shelf is retuned)."""
    src = open(CHANNEL_H).read()
    return {m.group(1): float(m.group(2))
            for m in re.finditer(r"static constexpr double (\w+)\s*=\s*(-?[\d.]+(?:[eE][-+]?\d+)?)", src)}


def _shelf(glo, ghi, pivot, fs):
    """First-order prewarped-bilinear shelf — mirrors MonarchChannel::shelfCoeffs."""
    rt = np.sqrt(ghi / glo)
    K = 2.0 * fs
    wz = K * np.tan(np.pi * (pivot / rt) / fs)
    wp = K * np.tan(np.pi * (pivot * rt) / fs)
    a0 = K + wp
    return ghi * (K + wz) / a0, ghi * (wz - K) / a0, (wp - K) / a0


def _peak(fc, gain_db, Q, fs):
    """RBJ peaking biquad — mirrors MonarchChannel::peakCoeffs."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * fc / fs
    al = np.sin(w0) / (2.0 * Q)
    a0 = 1.0 + al / A
    return ((1.0 + al * A) / a0, (-2.0 * np.cos(w0)) / a0, (1.0 - al * A) / a0,
            (-2.0 * np.cos(w0)) / a0, (1.0 - al / A) / a0)


def _mag1(b0, b1, a1, f, fs):
    z = np.exp(-2j * np.pi * np.asarray(f) / fs)
    return np.abs((b0 + b1 * z) / (1.0 + a1 * z))


def _mag2(b0, b1, b2, a1, a2, f, fs):
    z = np.exp(-2j * np.pi * np.asarray(f) / fs)
    return np.abs((b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z))


def drive_shelf_db(drive01, freqs, fs, K):
    """Total dB of MonarchChannel::driveShelf() at `drive01`: treble high-shelf, bass-boost
    low-shelf, bass-cut bell, warp shelf, HF trim — the full pre-clip correction chain.
    `fs` is the OS'd processing rate (shBaseRate), i.e. session rate × the report's os_factor."""
    treble = max(0.0, K["shelfMaxDb"] - K["shelfSlopeDb"] * drive01)
    bass_boost = min(K["bassBoostMaxDb"], max(0.0, K["bassBoostSlopeDb"] * (drive01 - K["bassOnsetDrive"])))
    bass_cut = -min(K["bassCutMaxDb"], max(0.0, K["bassCutSlopeDb"] * (K["bassCutOffDrive"] - drive01)))
    warp = min(K["warpMaxDb"], K["warpScaleDb"] * (48000.0 / fs) ** K["warpExp"])

    m = _mag1(*_shelf(1.0, 10 ** (treble / 20), K["shelfPivotHz"], fs), freqs, fs)
    m = m * _mag1(*_shelf(10 ** (bass_boost / 20), 1.0, K["bassPivotHz"], fs), freqs, fs)
    m = m * _mag2(*_peak(K["bassCutPivotHz"], bass_cut, K["bassCutQ"], fs), freqs, fs)
    m = m * _mag1(*_shelf(1.0, 10 ** (warp / 20), K["warpPivotHz"], fs), freqs, fs)
    m = m * _mag1(*_shelf(1.0, 10 ** (K["hfTrimDb"] / 20), K["hfTrimPivotHz"], fs), freqs, fs)
    return 20.0 * np.log10(m)


# ------------------------------------------------------------------------------------------ views
def view_bands(d, bands, caps, by="drive", sweep="sweep_clean", out=sys.stdout):
    ai = anchor_index(bands)
    sel = [0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 29]
    groups = {}
    for c in caps:
        k = c["rev"] if by == "mode" else c["settings"][by]
        groups.setdefault(k, []).append(err_db(c, sweep, bands, ai))

    print(f"\n=== FR error (plugin − pedal, dB, 1 kHz-normalized) — {sweep}, by {by} ===", file=out)
    print(f"{by[:5]:>5} {'n':>2}  " + "  ".join(f"{bands[i]:>7.0f}" for i in sel), file=out)
    for k in sorted(groups, key=lambda x: (str(x) if by == "mode" else x)):
        e = np.mean(groups[k], axis=0)
        label = str(k)[:5] if by == "mode" else f"{k:.2f}"
        print(f"{label:>5} {len(groups[k]):2d}  " + "  ".join(f"{e[i]:>+7.2f}" for i in sel), file=out)


def view_core(d, bands, caps, out=sys.stdout):
    """The cleanest error shape: mid drive only, where the bass-cut bell is off and the high-drive
    bloom is small, so neither confound is in the picture."""
    ai = anchor_index(bands)
    sel = [c for c in caps if 0.45 <= c["settings"]["drive"] <= 0.75]
    print(f"\n=== CORE error shape — drive 0.5–0.7 only (bell off, bloom small), sweep_clean, "
          f"1 kHz-norm, n={len(sel)} ===", file=out)
    print(f"{'band':>7}" + "".join(f"{m[:5]:>9}" for m in MODES) + f"{'ALL':>9}", file=out)
    per = {m: [err_db(c, "sweep_clean", bands, ai) for c in sel if c["rev"] == m] for m in MODES}
    alle = np.array([err_db(c, "sweep_clean", bands, ai) for c in sel])
    for i, b in enumerate(bands):
        row = f"{b:7.0f}" + "".join(f"{np.mean([x[i] for x in per[m]]):>+9.2f}" for m in MODES)
        print(row + f"{alle[:, i].mean():>+9.2f}", file=out)


def view_raw(d, bands, caps, out=sys.stdout):
    """Guardrail G1 — remove driveShelf() to see the RAW WDF circuit vs the pedal."""
    K = channel_consts()
    fs = 48000.0 * d["meta"]["os_factor"]
    ai = anchor_index(bands)
    drives = sorted({c["settings"]["drive"] for c in caps})
    print(f"\n=== RAW circuit vs pedal (driveShelf removed), sweep_clean, 1 kHz-norm "
          f"[shBaseRate {fs/1000:.0f} kHz] ===", file=out)
    print("  negative = plugin short.  Compare with `bands` to see what the shelves already fix.",
          file=out)
    print(f"{'band':>7}" + "".join(f"{'G'+f'{dv*10:g}':>8}" for dv in drives), file=out)
    cols = {}
    for dv in drives:
        sel = [c for c in caps if abs(c["settings"]["drive"] - dv) < 1e-9]
        cd = drive_shelf_db(dv, bands, fs, K)
        acc = []
        for c in sel:
            f = c["fr"]["sweep_clean"]
            y = (np.array(f["plugin_db"], float) - np.array(f["pedal_db"], float)) - cd
            acc.append(y - y[ai])
        cols[dv] = np.mean(acc, axis=0)
    for i, b in enumerate(bands):
        if b > 2100:
            continue
        print(f"{b:7.0f}" + "".join(f"{cols[dv][i]:>+8.2f}" for dv in drives), file=out)


def _peak_hz(y, bands, lo=25.0, hi=12000.0):
    lb = np.log2(bands)
    m = (bands >= lo) & (bands <= hi)
    yy = np.asarray(y, float)[m]
    bb = lb[m]
    k = int(np.argmax(yy))
    if 0 < k < len(yy) - 1:
        a, b, c = yy[k - 1], yy[k], yy[k + 1]
        den = a - 2 * b + c
        off = float(np.clip(0.5 * (a - c) / den, -1, 1)) if den < 0 else 0.0
        return float(2 ** (bb[k] + off * (bb[k + 1] - bb[k])))
    return float(2 ** bb[k])


def view_peaks(d, bands, caps, sweep="sweep_clean", out=sys.stdout):
    print(f"\n=== Overall-FR peak frequency, plugin vs pedal — {sweep} ===", file=out)
    print(f"{'capture':<16}{'plug Hz':>9}{'ped Hz':>9}{'oct':>7}", file=out)
    rows = []
    for c in sorted(caps, key=sort_key):
        f = c["fr"][sweep]
        p, q = _peak_hz(f["plugin_db"], bands), _peak_hz(f["pedal_db"], bands)
        rows.append((c, np.log2(p / q)))
        print(f"{c['id']:<16}{p:>9.0f}{q:>9.0f}{np.log2(p/q):>+7.2f}", file=out)
    a = np.array([r[1] for r in rows])
    mid = np.array([r[1] for r in rows if 0.25 < r[0]["settings"]["drive"] < 0.75])
    print(f"  all: mean {a.mean():+.2f} med {np.median(a):+.2f} sd {a.std():.2f} | "
          f"G3–G7 only: mean {mid.mean():+.2f} max|.| {np.abs(mid).max():.2f}", file=out)


def view_thd(d, bands, caps, out=sys.stdout):
    src = d["meta"]["thd_band_sources"]
    print("\n=== THD ratio, 20·log10(plugin/pedal) dB, median per mode  [+ = plugin distorts more] ===",
          file=out)
    for sw in d["meta"]["driven_sweeps"]:
        print(f"--- {sw}", file=out)
        print(f"{'band':>8}" + "".join(f"{m[:5]:>9}" for m in MODES)
              + "     median pedal% / plugin%", file=out)
        for i, b in enumerate(bands):
            if src[i] == "na":
                continue
            row = f"{b:8.0f}"
            for m in MODES:
                v = []
                for c in caps:
                    if c["rev"] != m:
                        continue
                    t = c["thd"][sw]
                    if t["plugin_pct"][i] is not None and t["pedal_pct"][i] is not None:
                        v.append(20 * np.log10((t["plugin_pct"][i] + 1e-9) / (t["pedal_pct"][i] + 1e-9)))
                row += f"{np.median(v):>+9.1f}" if v else f"{'-':>9}"
            pe = [c["thd"][sw]["pedal_pct"][i] for c in caps if c["thd"][sw]["pedal_pct"][i]]
            pl = [c["thd"][sw]["plugin_pct"][i] for c in caps if c["thd"][sw]["plugin_pct"][i]]
            note = "   <- G4: H2-only, unreliable" if b > 5200 else ""
            print(row + f"     {np.median(pe):7.2f} / {np.median(pl):7.2f}{note}", file=out)
        print(file=out)


def view_harm(d, bands, caps, out=sys.stdout):
    A = d["meta"]["thd_anchors"]
    print("\n=== Harmonic level re fundamental (dB) — plugin / pedal / delta, median per mode ===",
          file=out)
    for sw in d["meta"]["driven_sweeps"]:
        print(f"--- {sw}", file=out)
        print(f"{'mode':>6}{'H':>4}" + "".join(f"{str(a)+' Hz':>23}" for a in A), file=out)
        for m in MODES:
            sel = [c for c in caps if c["rev"] == m]
            for o in range(2, 8):
                row = f"{m[:6]:>6}{'H'+str(o):>4}"
                for ai in range(len(A)):
                    p = np.median([c["harmonics"][sw][f"H{o}"]["plugin_db"][ai] for c in sel])
                    q = np.median([c["harmonics"][sw][f"H{o}"]["pedal_db"][ai] for c in sel])
                    row += f"  {p:>7.1f}/{q:>7.1f} {p-q:>+6.1f}"
                print(row, file=out)
            print(file=out)


def view_alias(out=sys.stdout, fs=48000.0):
    """Guardrail G4 — the discrete-tone THD estimator sums k=2..8; show where each lands."""
    import gen_test_signal as G
    print(f"\n=== Discrete-tone THD estimator validity at FS={fs:.0f} "
          f"(analyze.thd sums harmonics k=2..8) ===", file=out)
    for f0 in G.TONE_FREQS:
        parts, bad = [], False
        for k in range(2, 9):
            fold = (k * f0) % fs
            if fold > fs / 2:
                fold = fs - fold
            hit = abs(fold - f0) < 50 or fold < 50
            bad = bad or hit
            parts.append(f"H{k}->{fold/1000:.1f}k" + ("*" if hit else ""))
        print(f"  f0={f0:>7.0f}  " + " ".join(parts) + ("   INVALID" if bad else ""), file=out)
    print("  * = folds onto the fundamental or DC — the estimator counts signal as distortion.",
          file=out)


def view_h2(out=sys.stdout, sweep="sweep_drv_-6"):
    """H2 vs frequency from the Farina sweep (valid to ~9.5 kHz). Not in the JSON — renders live."""
    import subprocess
    import tempfile
    sys.path.insert(0, HERE)
    import analyze as A
    import captures as C

    if not os.path.exists(C.RENDER_BIN):
        sys.exit(f"PedalRender not found at {C.RENDER_BIN} — "
                 f"build it: cmake --build build --target PedalRender")
    orig = A.load(A.ORIG)
    cases = [("G5 T5 Clean", 0.5, 0.5, 0), ("G10 T5 Clean", 1.0, 0.5, 0),
             ("G5 T5 OD", 0.5, 0.5, 1), ("G5 T5 Dist", 0.5, 0.5, 2)]
    freqs = [100, 200, 400, 800, 1600, 3200, 6400, 9000]
    print(f"\n=== H2 level re fundamental (dB) vs frequency — {sweep} "
          f"[Farina, valid to ~9.5 kHz] ===", file=out)
    print(f"{'case':<12}{'':>4}" + "".join(f"{f:>9}" for f in freqs), file=out)
    for label, dr, to, cl in cases:
        hits = [p for p, _ in C.find_captures() if os.path.basename(p).startswith(label)]
        if not hits:
            print(f"  (missing capture: {label})", file=out)
            continue
        cap, _ = A.align(C.load_capture(hits[0]), orig)
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as t:
            tmp = t.name
        subprocess.run([C.RENDER_BIN, A.ORIG, tmp, f"{dr:.4f}", f"{to:.4f}", "0.5000", "0.0000",
                        str(cl), "render", "3"], check=True, capture_output=True)
        ren, _ = A.align(A.load(tmp), orig)
        os.unlink(tmp)
        ref = A.seg_of(orig, sweep)
        for nm, sig in (("ped", cap), ("plug", ren)):
            fr, _, Hn = A.harmonic_thd_curve(A.seg_of(sig, sweep), ref, max_order=3)
            row = f"{label if nm == 'ped' else '':<12}{nm:>4}"
            for f in freqs:
                i = int(np.argmin(np.abs(fr - f)))
                row += f"{20*np.log10(Hn[2][i] / (Hn[1][i] + 1e-20) + 1e-20):>9.1f}"
            print(row, file=out)


# ------------------------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("view", nargs="?", default="all",
                    choices=["bands", "core", "raw", "peaks", "thd", "harm", "alias", "h2", "all"])
    ap.add_argument("--by", default="drive", choices=["drive", "tone", "mode"])
    ap.add_argument("--sweep", default="sweep_clean")
    ap.add_argument("--report", action="store_true",
                    help="append the regenerated tables to analysis/FR_THD_AUDIT.md's data section")
    a = ap.parse_args()

    sys.path.insert(0, HERE)
    d, bands, caps = load()
    out = sys.stdout

    if a.view in ("bands", "all"):
        for by in (["drive", "tone", "mode"] if a.view == "all" else [a.by]):
            view_bands(d, bands, caps, by, a.sweep, out)
    if a.view in ("core", "all"):
        view_core(d, bands, caps, out)
    if a.view in ("raw", "all"):
        view_raw(d, bands, caps, out)
    if a.view in ("peaks", "all"):
        view_peaks(d, bands, caps, a.sweep, out)
    if a.view in ("thd", "all"):
        view_thd(d, bands, caps, out)
    if a.view in ("harm", "all"):
        view_harm(d, bands, caps, out)
    if a.view in ("alias", "all"):
        view_alias(out)
    if a.view == "h2":
        view_h2(out)

    if a.report:
        print("\n(--report: paste the tables above into analysis/FR_THD_AUDIT.md's "
              "'Regenerating the tables' section; the prose findings are hand-written.)",
              file=sys.stderr)


if __name__ == "__main__":
    main()
