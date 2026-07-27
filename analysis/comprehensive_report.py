#!/usr/bin/env python3
"""Comprehensive plugin-vs-capture analysis — FR, THD, H2-H7 harmonics -> JSON dashboard.

Imported from the Guitar-Pedal-Plugin-Template harness (that project's analysis/README.md has the
general contract) and customised for Monarch of Tone: captures.py implements the pedal-specific
I/O (analysis/pedal_export2/, PedalRender's positional CLI), and analyze.py gained the handful of
functions this script needs (transfer/harmonic_thd_curve/null_depth/frac_align/...) alongside the
existing Farina-based CLI report those functions don't replace.

Reads every capture in analysis/pedal_export2/, renders the plugin (Yellow channel, via
tools/PedalRender) at matching settings, and writes a JSON report consumable by dashboard_gen.py.

Run from repo root:
    python3 analysis/comprehensive_report.py [--os 8] [--keep-renders DIR] [--jobs N]

Captures are analysed in parallel across a process pool (each capture's render + analysis is
independent). Defaults to all cores minus a reservation for the OS and other running processes —
override with --jobs, or pass --jobs 1 to run serially.

Real-pedal capture analysis (load, align, transfer/FR curve, Farina harmonic curve, discrete-tone
THD) depends only on the capture .wav + the reference test signal — never on the plugin — so it's
cached to disk per capture file (analysis/.cache/) and skipped entirely on later runs as you
iterate on the plugin. Pass --no-cache to bypass.

Output: analysis/reports/comprehensive_data.json
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import pickle
import subprocess
import sys
import tempfile
from collections import defaultdict
from datetime import datetime, timezone

import numpy as np

import analyze as A
import gen_test_signal as G

import captures as C

DEFAULT_BIN = C.RENDER_BIN
OUTPUT_JSON = "analysis/reports/comprehensive_data.json"
CACHE_DIR = "analysis/.cache/pedal_features"
CACHE_VERSION = 1  # bump to invalidate every cache entry after a change to the analysis below
DRIVEN_SWEEPS = ("sweep_drv_-18", "sweep_drv_-12", "sweep_drv_-6")
ALL_SWEEP_LEVELS = ("sweep_clean",) + DRIVEN_SWEEPS
THD_ANCHORS = (100, 200, 400)
HARMONIC_ORDERS = tuple(range(2, 8))
TONE_FREQS = G.TONE_FREQS

# --- where THD stops being measurable (FR_THD_AUDIT.md Finding 4 / P0) -----------------------
# THD is an RSS of H2..H7, and for a symmetric clipper it is DOMINATED by the odd orders — H3
# above all. `thd_max_measurable_hz(2)` (~9.5 kHz) is only where H2 alone still survives the
# sweep's order limit; a band that has lost H3 no longer reports THD, it reports H2, and the two
# are not comparable to the bands below. So trust the Farina THD only while H3 is measurable
# (~6.3 kHz). Above that BOTH paths fail: Farina is H2-only, and the discrete-tone fallback
# aliases (below). This deliberately routes the 6451/8128 Hz bands to 'na' — the dramatic THD
# cliff they used to draw on the dashboard was a measurement artifact, not plugin error.
THD_FARINA_CEILING_HZ = A.thd_max_measurable_hz(max_order=3)
# H2 vs frequency is a single-order view, so it stays valid up to the H2 limit (~9.5 kHz).
H2_CEILING_HZ = A.thd_max_measurable_hz(max_order=2)
ALIAS_GUARD_HZ = 50.0  # a folded harmonic this close to f0 (or DC) contaminates the estimator

# FR bands worth scoring. Below 40 Hz the sweep has little energy (N-004); above 8 kHz the NAM
# captures themselves spread by +18.8/-4.4 dB (FR_THD_AUDIT.md Finding 2) — reference noise, not
# plugin error. dashboard_gen.py reads these back out of `meta` so both agree by construction.
FR_TRUST_LO, FR_TRUST_HI = 40.0, 8000.0


def discrete_tone_is_valid(f0, fs=A.FS):
    """False when `analyze.thd`'s k=2..8 sum is contaminated by aliasing at this fundamental.

    Above fs/(2k) the k-th harmonic folds back below Nyquist; if it lands on the fundamental (or
    DC) the estimator counts SIGNAL as distortion. At fs=48k that kills f0=6000 (H7 folds to 6 kHz,
    H8 to DC) and f0=8000 (H5 and H7 fold to 8 kHz, H6 to DC) — the captures read up to 291% THD
    there, which is physically impossible. `fr_thd_audit.py alias` prints the full landing map."""
    for k in range(2, 9):
        fold = (k * f0) % fs
        if fold > fs / 2:
            fold = fs - fold
        if abs(fold - f0) < ALIAS_GUARD_HZ or fold < ALIAS_GUARD_HZ:
            return False
    return True


def build_band_source_map(bands):
    """Return list of (band_hz, source_str) — 'farina', 'discrete', or 'na'."""
    result = []
    for b in bands:
        if b <= THD_FARINA_CEILING_HZ + 1e-6:
            result.append((b, "farina"))
            continue
        nearest_tone = min(TONE_FREQS, key=lambda t: abs(t - b))
        usable_tone = (abs(nearest_tone - b) / b < 0.06
                       and nearest_tone > THD_FARINA_CEILING_HZ
                       and discrete_tone_is_valid(nearest_tone))
        result.append((b, "discrete" if usable_tone else "na"))
    return result


def render_plugin(binpath, args, out_path, os_factor):
    """PedalRender's CLI is positional (no generic --os flag): in out drive tone vol pres clip
    [live|render] [osIndex]. `args` is captures.render_args()'s 5-element [drive,tone,vol,pres,clip]
    list; this appends the always-render quality mode + the OS factor mapped to PedalRender's
    0..3 index (1x/2x/4x/8x)."""
    os_index = C.OS_FACTOR_TO_INDEX.get(os_factor, 3)
    cmd = [binpath, A.ORIG, out_path] + args + ["render", str(os_index)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(f"  ! render failed: {r.stderr.strip() or r.stdout.strip()}\n")
        return False
    return True


def _pedal_cache_key(path):
    """Identity of a capture file + the reference signal it's aligned against + the analysis
    version. A capture is a real recording — it never changes once made — so keying on
    (path, mtime, size) is enough to detect an edit/re-record without hashing file contents."""
    st = os.stat(path)
    ost = os.stat(A.ORIG)
    payload = {
        "version": CACHE_VERSION,
        "path": os.path.abspath(path),
        "mtime": st.st_mtime,
        "size": st.st_size,
        "orig_mtime": ost.st_mtime,
        "orig_size": ost.st_size,
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


def compute_pedal_features(cap_al, orig):
    """All CAPTURE-side (real-pedal) analysis — transfer/FR curve, Farina harmonic curve,
    discrete-tone THD — for every sweep/tone this report ever reads. None of this depends on the
    plugin render, so it's exactly what's safe to cache to disk keyed by the capture file.

    Each level is deconvolved against ITS OWN pristine sweep segment from `orig` (not always
    "sweep_clean"): unlike the template's reference signal (every sweep the same 10 s so one clean
    reference works for all), this project's gen_test_signal.py gives the clean sweep 10 s but the
    driven sweeps 8 s (different SWEEP_CLEAN_SEC/SWEEP_DRIVEN_SEC) — cross-deconvolving a driven
    capture against the differently-timed clean reference divides mismatched frequency-vs-time
    curves and produces garbage (THD in the millions of percent, confirmed while wiring this up)."""
    fr_transfer = {sw: A.transfer(A.seg_of(cap_al, sw), A.seg_of(orig, sw)) for sw in ALL_SWEEP_LEVELS}
    farina = {sw: A.harmonic_thd_curve(A.seg_of(cap_al, sw), A.seg_of(orig, sw), max_order=7)
              for sw in DRIVEN_SWEEPS}

    tone_thd = {}
    for t in TONE_FREQS:
        seg_name = f"tone_{t:g}"
        try:
            tone_thd[seg_name] = A.thd(A.seg_of(cap_al, seg_name), t)
        except Exception:
            tone_thd[seg_name] = (None, None)

    return {"fr_transfer": fr_transfer, "farina": farina, "tone_thd": tone_thd}


def get_pedal_features(path, orig, cache_dir, use_cache=True):
    """Return (cap_al, pedal_features), loading from disk cache when the capture + reference
    signal identity matches a prior run. Returns None if the capture is truncated."""
    cpath = os.path.join(cache_dir, _pedal_cache_key(path) + ".pkl") if use_cache else None

    if use_cache and os.path.exists(cpath):
        try:
            with open(cpath, "rb") as fh:
                return pickle.load(fh)
        except Exception:
            pass  # corrupt/partial cache entry -> fall through and recompute

    cap = C.load_capture(path)
    if not A.is_full_length(cap, orig):
        return None
    cap_al, _ = A.align(cap, orig)
    result = (cap_al, compute_pedal_features(cap_al, orig))

    if use_cache:
        os.makedirs(cache_dir, exist_ok=True)
        tmp = f"{cpath}.tmp{os.getpid()}"
        with open(tmp, "wb") as fh:
            pickle.dump(result, fh, protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, cpath)

    return result


def fr_at_bands(cap_al, ren_al, orig, sweep_name, bands, pedal_features):
    """Return (plugin_db, pedal_db, gain_db_applied) at each band."""
    inp = A.seg_of(orig, sweep_name)   # this level's own pristine reference — see compute_pedal_features
    cap_seg = A.seg_of(cap_al, sweep_name)
    ren_seg = A.seg_of(ren_al, sweep_name)
    ren_seg_aligned = A.frac_align(ren_seg, cap_seg)
    _, gain_db = A.null_depth(cap_seg, ren_seg_aligned)

    f_cap, H_cap = pedal_features["fr_transfer"][sweep_name]
    f, H_ren = A.transfer(ren_seg, inp)
    plugin_db = [float(np.interp(b, f, H_ren)) + gain_db for b in bands]
    pedal_db = [float(np.interp(b, f_cap, H_cap)) for b in bands]
    return plugin_db, pedal_db, float(gain_db)


def thd_at_bands(ren_al, sweep_name, band_source_map, pedal_features, cap_farina, ren_farina):
    """Return (plugin_pct, pedal_pct, source) arrays at each band."""
    tone_cache = {}

    plugin_pct = []
    pedal_pct = []
    sources = []

    for band_hz, source in band_source_map:
        if source == "farina":
            fr_c, thd_c, _ = cap_farina
            fr_r, thd_r, _ = ren_farina
            plugin_pct.append(float(np.interp(band_hz, fr_r, thd_r)))
            pedal_pct.append(float(np.interp(band_hz, fr_c, thd_c)))
            sources.append("farina")
        elif source == "discrete":
            nearest_tone = min(TONE_FREQS, key=lambda t: abs(t - band_hz))
            tone_seg = f"tone_{nearest_tone:g}"
            if tone_seg not in tone_cache:
                try:
                    thd_cap, _ = pedal_features["tone_thd"][tone_seg]
                    thd_ren, _ = A.thd(A.seg_of(ren_al, tone_seg), nearest_tone)
                    tone_cache[tone_seg] = (float(thd_cap), float(thd_ren))
                except Exception:
                    tone_cache[tone_seg] = (None, None)
            p_cap, p_ren = tone_cache[tone_seg]
            plugin_pct.append(p_ren)
            pedal_pct.append(p_cap)
            sources.append("discrete")
        else:
            plugin_pct.append(None)
            pedal_pct.append(None)
            sources.append("na")

    return plugin_pct, pedal_pct, sources


def harmonics_at_anchors(cap_farina, ren_farina):
    """Return {order: {plugin_db, pedal_db}} at each anchor freq."""
    fr_c, _, Hn_c = cap_farina
    fr_r, _, Hn_r = ren_farina

    har = {}
    for order in range(2, 8):
        plugin_db = []
        pedal_db = []
        for ahz in THD_ANCHORS:
            idx_c = int(np.argmin(np.abs(fr_c - ahz)))
            idx_r = int(np.argmin(np.abs(fr_r - ahz)))
            H1_c = Hn_c[1][idx_c] if 1 in Hn_c else 1e-20
            H1_r = Hn_r[1][idx_r] if 1 in Hn_r else 1e-20
            val_c = float(20.0 * np.log10(Hn_c[order][idx_c] / (H1_c + 1e-20) + 1e-20))
            val_r = float(20.0 * np.log10(Hn_r[order][idx_r] / (H1_r + 1e-20) + 1e-20))
            pedal_db.append(val_c)
            plugin_db.append(val_r)
        har[f"H{order}"] = {"plugin_db": plugin_db, "pedal_db": pedal_db}
    return har


def h2_curve_at_bands(bands, cap_farina, ren_farina):
    """Return (plugin_db, pedal_db) — H2 level re the fundamental, dB, at every band.

    The most diagnostic single view of the even-harmonic gap (FR_THD_AUDIT.md Finding 4). The
    3-anchor harmonic heatmap cannot show it: the pedal's H2 has strong frequency STRUCTURE (a deep
    trough around 800 Hz, rising either side) that the plugin's injected, near-flat H2 does not
    reproduce at all — and that shape mismatch, not the 6-8.5 kHz "THD cliff", is the real signal up
    there. None above H2_CEILING_HZ, where H2 itself leaves the sweep's order limit / Nyquist."""
    def curve(farina):
        fr, _, Hn = farina
        out = []
        for b in bands:
            if b > H2_CEILING_HZ or 1 not in Hn or 2 not in Hn:
                out.append(None)
                continue
            i = int(np.argmin(np.abs(fr - b)))
            h1, h2 = float(Hn[1][i]), float(Hn[2][i])
            out.append(float(20.0 * np.log10(h2 / h1)) if h1 > 0.0 and h2 > 0.0 else None)
        return out

    return curve(ren_farina), curve(cap_farina)


def short_id(parsed):
    """Compact capture label — captures.py already builds one from the filename, e.g. 'G6 T5 OD'."""
    return parsed.get("label", "?")


def analyse_one(path, parsed, orig, binpath, os_factor, keep_dir, bands, band_source_map,
                 cache_dir, use_cache):
    cached = get_pedal_features(path, orig, cache_dir, use_cache)
    if cached is None:
        sys.stderr.write(f"  SKIP (truncated): {os.path.basename(path)}\n")
        return None
    cap_al, pedal_features = cached

    args = C.render_args(parsed)
    tmp = None
    try:
        if keep_dir:
            os.makedirs(keep_dir, exist_ok=True)
            out_path = os.path.join(keep_dir,
                                    os.path.splitext(os.path.basename(path))[0] + "_plugin.wav")
        else:
            tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
            out_path = tmp.name
            tmp.close()

        if not render_plugin(binpath, args, out_path, os_factor):
            return None
        ren = A.load(out_path)
        ren_al, _ = A.align(ren, orig)

        settings = {}
        for k, v in parsed.items():
            if k in ("rev", "label"):
                continue
            if v is not None:
                settings[k] = float(v) if not isinstance(v, (int, float)) else v

        result = {
            "id": short_id(parsed),
            "rev": parsed.get("rev", "?"),
            "file": os.path.basename(path),
            "settings": settings,
            "fr": {},
            "thd": {},
            "harmonics": {},
            "h2": {},
        }

        for sw in ALL_SWEEP_LEVELS:
            plugin_db, pedal_db, gain_db = fr_at_bands(cap_al, ren_al, orig, sw, bands, pedal_features)
            result["fr"][sw] = {"plugin_db": plugin_db, "pedal_db": pedal_db, "gain_db_applied": gain_db}

        for sw in DRIVEN_SWEEPS:
            # One Farina decomposition of the render per sweep, shared by all three views below —
            # THD, the H2-H7 anchors and the H2 curve are all read off the same {order: |H|} set.
            cap_farina = pedal_features["farina"][sw]
            ren_farina = A.harmonic_thd_curve(A.seg_of(ren_al, sw), A.seg_of(orig, sw), max_order=7)

            plugin_pct, pedal_pct, sources = thd_at_bands(
                ren_al, sw, band_source_map, pedal_features, cap_farina, ren_farina)
            result["thd"][sw] = {
                "plugin_pct": plugin_pct, "pedal_pct": pedal_pct, "source": sources,
            }
            result["harmonics"][sw] = harmonics_at_anchors(cap_farina, ren_farina)
            h2_plugin, h2_pedal = h2_curve_at_bands(bands, cap_farina, ren_farina)
            result["h2"][sw] = {"plugin_db": h2_plugin, "pedal_db": h2_pedal}

        return result

    finally:
        if tmp and os.path.exists(out_path):
            os.unlink(out_path)


def fr_shape_rms(fr, bands):
    """Per-band delta with the row's median offset removed, rms'd over the TRUSTED band only.

    Two corrections vs the raw all-band rms this used to be, both from FR_THD_AUDIT.md P0:
    removing the median makes it a SHAPE score (a pure level offset is a volume difference, not a
    voicing error — and it is what the dashboard heatmap has always plotted, so the tiles now agree
    with the cells above them); and restricting to FR_TRUST_LO..FR_TRUST_HI drops the bands where
    the CAPTURES spread by +18.8/-4.4 dB, which was inflating every mode's score with reference
    noise. Below 40 Hz the sweep is thin (N-004); above 8 kHz the NAM captures roll off and alias."""
    idx = [i for i, b in enumerate(bands) if FR_TRUST_LO <= b <= FR_TRUST_HI]
    diffs = [fr["plugin_db"][i] - fr["pedal_db"][i] for i in idx
             if fr["plugin_db"][i] is not None and fr["pedal_db"][i] is not None]
    if not diffs:
        return 0.0
    shape = np.array(diffs) - float(np.median(diffs))
    return float(np.sqrt(np.mean(shape ** 2)))


def compute_summary(results, bands):
    """Per-mode (Boost/Overdrive/Distortion) aggregate scores (derives modes from the data)."""
    by_rev = defaultdict(list)
    for r in results:
        if r:
            by_rev[r["rev"]].append(r)

    out = {}
    for rev, rev_caps in by_rev.items():
        fr_rms_vals = []
        best_rms = float("inf")
        worst_rms = float("-inf")
        best_id = worst_id = ""
        for r in rev_caps:
            rms = fr_shape_rms(r["fr"]["sweep_clean"], bands)
            fr_rms_vals.append(rms)
            if rms < best_rms:
                best_rms = rms
                best_id = r["id"]
            if rms > worst_rms:
                worst_rms = rms
                worst_id = r["id"]
        out[rev] = {
            "n_captures": len(rev_caps),
            "fr_rms_mean": float(np.mean(fr_rms_vals)),
            "fr_rms_median": float(np.median(fr_rms_vals)),
            "fr_rms_min": best_rms,
            "fr_rms_max": worst_rms,
            "best_capture": best_id,
            "worst_capture": worst_id,
        }
    return {"by_revision": out}


def default_jobs():
    """All cores minus a reservation for the OS + other running processes."""
    n = os.cpu_count() or 4
    reserved = max(1, round(n * 0.2))
    return max(1, n - reserved)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("--os", type=int, default=8, choices=sorted(C.OS_FACTOR_TO_INDEX),
                     help="oversampling factor (default: %(default)s)")
    ap.add_argument("--keep-renders", default=None)
    ap.add_argument("--jobs", type=int, default=None,
                     help=f"parallel worker processes (default: {default_jobs()} "
                          f"of {os.cpu_count()} cores, reserving some for the OS)")
    ap.add_argument("--cache-dir", default=CACHE_DIR,
                     help="disk cache dir for capture-side (pedal) analysis (default: %(default)s)")
    ap.add_argument("--no-cache", action="store_true",
                     help="recompute capture-side analysis fresh; don't read or write the cache")
    a = ap.parse_args()
    jobs = a.jobs if a.jobs and a.jobs > 0 else default_jobs()
    use_cache = not a.no_cache

    if not os.path.exists(a.bin):
        sys.exit(f"PedalRender not found at {a.bin} — build it: "
                 f"cmake --build build --target PedalRender")
    if not os.path.exists(A.ORIG):
        sys.exit(f"Reference not found at {A.ORIG} — run analysis/gen_test_signal.py first")

    bands = [round(b, 1) for b in A.fractional_octave_freqs(20.0, 20000.0, 3)]
    band_source_map = build_band_source_map(bands)

    orig = A.load(A.ORIG)
    caps = C.find_captures()

    sys.stderr.write(f"Comprehensive report: {len(caps)} captures | OS={a.os}x | {len(bands)} bands\n")
    sys.stderr.write(f"  THD coverage: {sum(1 for _, s in band_source_map if s != 'na')}/{len(bands)} bands\n")
    sys.stderr.write(f"  jobs: {jobs} (of {os.cpu_count()} cores) | cache: "
                     f"{'off' if not use_cache else a.cache_dir}\n\n")

    results = [None] * len(caps)
    if jobs <= 1:
        for i, (path, parsed) in enumerate(caps):
            sys.stderr.write(f"[{i + 1}/{len(caps)}] {short_id(parsed)} ... ")
            sys.stderr.flush()
            res = analyse_one(path, parsed, orig, a.bin, a.os, a.keep_renders, bands, band_source_map,
                               a.cache_dir, use_cache)
            sys.stderr.write("done\n" if res else "FAILED\n")
            results[i] = res
    else:
        with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as ex:
            futures = {
                ex.submit(analyse_one, path, parsed, orig, a.bin, a.os, a.keep_renders,
                          bands, band_source_map, a.cache_dir, use_cache): i
                for i, (path, parsed) in enumerate(caps)
            }
            completed = 0
            for fut in concurrent.futures.as_completed(futures):
                i = futures[fut]
                _, parsed = caps[i]
                completed += 1
                try:
                    res = fut.result()
                except Exception as e:
                    sys.stderr.write(f"[{completed}/{len(caps)}] {short_id(parsed)} ... FAILED ({e})\n")
                    res = None
                else:
                    sys.stderr.write(f"[{completed}/{len(caps)}] {short_id(parsed)} ... "
                                      f"{'done' if res else 'FAILED'}\n")
                results[i] = res

    ok = [r for r in results if r]
    sys.stderr.write(f"\n{len(ok)}/{len(results)} captures analysed.\n")

    summary = compute_summary(ok, bands)

    out = {
        "meta": {
            "generated": datetime.now(timezone.utc).isoformat(),
            "os_factor": a.os,
            "num_captures": len(ok),
            "num_bands": len(bands),
            "bands": bands,
            "thd_anchors": list(THD_ANCHORS),
            "harmonic_orders": list(HARMONIC_ORDERS),
            "driven_sweeps": list(DRIVEN_SWEEPS),
            "all_sweep_levels": list(ALL_SWEEP_LEVELS),
            "thd_band_sources": [s for _, s in band_source_map],
            "thd_farina_ceiling_hz": THD_FARINA_CEILING_HZ,
            "h2_ceiling_hz": H2_CEILING_HZ,
            "fr_trust_lo_hz": FR_TRUST_LO,
            "fr_trust_hi_hz": FR_TRUST_HI,
        },
        "captures": ok,
        "summary": summary,
    }

    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    with open(OUTPUT_JSON, "w") as fh:
        json.dump(out, fh, indent=2)
    sys.stderr.write(f"wrote {OUTPUT_JSON}  ({os.path.getsize(OUTPUT_JSON)} bytes)\n")


if __name__ == "__main__":
    main()
