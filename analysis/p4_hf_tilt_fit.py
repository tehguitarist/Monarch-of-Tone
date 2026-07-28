#!/usr/bin/env python3
"""P4 fit harness — the 1.6-5 kHz tilt (FR_THD_AUDIT.md Finding 2 / P4).

Reads analysis/reports/comprehensive_data.json only (no rendering, <1 s) and does three things,
in the order P6's rule demands — characterise the error against every axis it could be indexed by
BEFORE proposing a mechanism:

  tilt    the 1 kHz-normalized FR error above 1 kHz, as a robust MEDIAN over all 44 captures x 4
          sweep levels, with the inter-quartile spread beside it. Mean is the wrong summary here:
          Overdrive at G10 flips the sign of this band (see `axes`), so a mean over modes reports a
          number no capture actually shows. This is the same aggregation trap P6 documented twice.
  axes    the same error broken out by MODE and by DRIVE. The tilt must be shown to be flat against
          both before a fixed shelf is the right instrument at all — a drive-indexed error is a
          gain error, not a tilt (P6), and a mode-indexed one is a clipping effect, not linear EQ.
  fit     grid-search over a replacement for the existing fixed HF-trim high-shelf (`hfTrimPivotHz`
          / `hfTrimDb`), scoring the PREDICTED residual: measured error + (new shelf - old shelf),
          re-anchored at 1 kHz exactly as the measurement is. Both shelves are evaluated with the
          audit's own `_shelf`/`_mag1`, which mirror MonarchChannel::shelfCoeffs, and the current
          constants are parsed live out of the header, so this cannot drift from the shipped values.

The fit band stops at 5120 Hz on purpose: 6.4 kHz up has a +-18 dB capture-side spread (CLAUDE.md /
FR_THD_AUDIT.md S4) and the warp shelf lives at 6.5 kHz, so anything fitted up there is fitting
noise and a neighbouring correction at once. What the candidate does above the fit band is printed
as a consequence, not scored.

Usage:
  p4_hf_tilt_fit.py                 # all three views
  p4_hf_tilt_fit.py tilt|axes|fit
  p4_hf_tilt_fit.py fit --lo 806 --hi 5120
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fr_thd_audit as A  # noqa: E402

FIT_LO, FIT_HI = 806.0, 5120.0     # trust band for fitting (see docstring)
WATCH = (201.6, 320.0, 508.0, 806.0, 1280.0, 1613.0, 2032.0, 2560.0, 3225.0, 4064.0, 5120.0,
         6450.8, 8127.5)


def all_errors(d, bands, caps):
    """(n, nbands) matrix of 1 kHz-normalized errors, one row per capture x sweep, + row labels."""
    ai = A.anchor_index(bands)
    rows, meta = [], []
    for c in sorted(caps, key=A.sort_key):
        for sw in d["meta"]["all_sweep_levels"]:
            rows.append(A.err_db(c, sw, bands, ai))
            meta.append((c["rev"], c["settings"]["drive"], c["settings"]["tone"], sw))
    return np.array(rows, float), meta


def _cols(bands):
    """Nearest band index for each WATCH frequency (the report's bands are 1612.7, 4063.7, ...)."""
    return sorted({int(np.argmin(np.abs(bands - w))) for w in WATCH})


def view_tilt(e, bands, out=sys.stdout):
    sel = _cols(bands)
    med = np.nanmedian(e, 0)
    q1, q3 = np.nanpercentile(e, 25, 0), np.nanpercentile(e, 75, 0)
    print(f"\n=== P4 tilt — median error over all captures x sweeps (n={len(e)}), 1 kHz-norm ===",
          file=out)
    print(f"{'Hz':>7} {'median':>8} {'IQR':>16}", file=out)
    for i in sel:
        print(f"{bands[i]:7.0f} {med[i]:+8.2f}   [{q1[i]:+5.2f}, {q3[i]:+5.2f}]", file=out)


def view_axes(e, meta, bands, out=sys.stdout):
    sel = _cols(bands)
    hdr = "  " + " ".join(f"{bands[i]:>7.0f}" for i in sel)

    print("\n=== by MODE (median) — is the tilt a linear-EQ error or a clipping one? ===", file=out)
    print(f"{'mode':<6}{'n':>4}" + hdr, file=out)
    for mode in A.MODES:
        idx = [j for j, m in enumerate(meta) if m[0] == mode]
        med = np.nanmedian(e[idx], 0)
        print(f"{mode[:5]:<6}{len(idx):>4}  " + " ".join(f"{med[i]:+7.2f}" for i in sel), file=out)

    print("\n=== by DRIVE (median) — an error indexed by the knob is a gain error, not a tilt ===",
          file=out)
    print(f"{'drive':<6}{'n':>4}" + hdr, file=out)
    for dv in sorted({m[1] for m in meta}):
        idx = [j for j, m in enumerate(meta) if m[1] == dv]
        med = np.nanmedian(e[idx], 0)
        print(f"{dv:<6.2f}{len(idx):>4}  " + " ".join(f"{med[i]:+7.2f}" for i in sel), file=out)

    print("\n=== by TONE (median) — knob-variance control (Finding 2's original test) ===", file=out)
    print(f"{'tone':<6}{'n':>4}" + hdr, file=out)
    for tv in sorted({m[2] for m in meta}):
        idx = [j for j, m in enumerate(meta) if m[2] == tv]
        if len(idx) < 8:
            continue
        med = np.nanmedian(e[idx], 0)
        print(f"{tv:<6.2f}{len(idx):>4}  " + " ".join(f"{med[i]:+7.2f}" for i in sel), file=out)


def shelf_db(pivot, depth_db, freqs, fs):
    return 20.0 * np.log10(A._mag1(*A._shelf(1.0, 10 ** (depth_db / 20.0), pivot, fs), freqs, fs))


def view_fit(e, bands, fs, lo=FIT_LO, hi=FIT_HI, out=sys.stdout):
    K = A.channel_consts()
    ai = A.anchor_index(bands)
    med = np.nanmedian(e, 0)
    old = shelf_db(K["hfTrimPivotHz"], K["hfTrimDb"], bands, fs)
    band = np.array([lo <= b <= hi for b in bands])
    sel = _cols(bands)

    print(f"\n=== P4 fit — replace hfTrim ({K['hfTrimPivotHz']:.0f} Hz, {K['hfTrimDb']:+.2f} dB) ===",
          file=out)
    print(f"fit band {lo:.0f}-{hi:.0f} Hz, fs={fs:.0f} (report OS rate); score = rms of the "
          f"predicted median residual", file=out)

    def residual(pivot, depth):
        new = shelf_db(pivot, depth, bands, fs)
        r = med + (new - old)
        return r - r[ai]

    base = med - med[ai]
    print(f"\nbaseline (no change):  rms {np.sqrt(np.nanmean(base[band] ** 2)):.3f} dB   "
          f"max |{np.nanmax(np.abs(base[band])):.2f}|", file=out)

    cands = []
    for pivot in np.arange(700.0, 6001.0, 25.0):
        for depth in np.arange(-4.0, 0.001, 0.05):
            r = residual(pivot, depth)
            cands.append((float(np.sqrt(np.nanmean(r[band] ** 2))), float(pivot), float(depth)))
    cands.sort()

    print(f"\n{'rms':>6} {'pivot':>7} {'depth':>7}   " +
          " ".join(f"{bands[i]:>7.0f}" for i in sel), file=out)
    seen = []
    for rms, pivot, depth in cands:
        if any(abs(pivot - p) < 400 for p in seen):
            continue
        seen.append(pivot)
        r = residual(pivot, depth)
        print(f"{rms:6.3f} {pivot:7.0f} {depth:+7.2f}   " +
              " ".join(f"{r[i]:+7.2f}" for i in sel), file=out)
        if len(seen) >= 8:
            break

    print("\n(columns beyond the fit band are consequences, not scored: 6451/8128 Hz)", file=out)
    print("(negative below 1 kHz = the candidate has pulled the low mids down with it)", file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("view", nargs="?", default="all", choices=["all", "tilt", "axes", "fit"])
    ap.add_argument("--lo", type=float, default=FIT_LO)
    ap.add_argument("--hi", type=float, default=FIT_HI)
    a = ap.parse_args()

    d, bands, caps = A.load()
    fs = 48000.0 * d["meta"]["os_factor"]
    e, meta = all_errors(d, bands, caps)

    if a.view in ("all", "tilt"):
        view_tilt(e, bands)
    if a.view in ("all", "axes"):
        view_axes(e, meta, bands)
    if a.view in ("all", "fit"):
        view_fit(e, bands, fs, a.lo, a.hi)


if __name__ == "__main__":
    main()
