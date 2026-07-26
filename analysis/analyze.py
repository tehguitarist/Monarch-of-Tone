#!/usr/bin/env python3
"""Comprehensive A/B analysis: plugin render vs real-pedal (NAM) capture, on the v2 test signal.

v2 (2026-06-27) for the full reference-validation suite. Segment timings come from
gen_test_signal.segment_times() (single source of truth). Both this CLI and run_validation.py
call the analysis functions here.

Metrics:
  - frequency_response : Farina-ESS deconvolution of the clean sweep -> continuous magnitude
                         20 Hz-20 kHz; RMS-dB error vs real per 1/3-octave band.
  - thd_vs_freq        : Farina on the driven sweeps -> continuous THD% and per-order (H2..)
                         magnitude vs frequency, summarised per band (emphasis 1-2/2-4/4-8 kHz).
  - harmonics_at_tones : full H2..H10 + odd/even split at the discrete tones (spot checks).
  - imd                : SMPTE / CCIF intermodulation product levels.
  - dynamics           : gain & THD vs instantaneous level along the plucked-note decay.
  - compression        : output level vs input level from the 1 kHz steps.

CLI:
  analyze.py REAL.wav PLUGIN.wav [LABEL]      # full report
"""
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import welch
from scipy import signal as sps

import gen_test_signal as gts

FS = gts.FS
TIMES = gts.segment_times()

THIRD_OCT_CENTRES = [25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630,
                     800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000,
                     12500, 16000, 20000]
THD_BANDS = [("low <250", 20, 250), ("250-1k", 250, 1000), ("1-2k", 1000, 2000),
             ("2-4k", 2000, 4000), ("4-8k", 4000, 8000), ("8k+", 8000, 20000)]


# ----------------------------------------------------------------------------- I/O + helpers
def load_mono(path):
    sr, x = wavfile.read(path)
    if x.dtype.kind == 'i':
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    else:
        x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    assert sr == FS, f"{path}: expected {FS} Hz, got {sr}"
    return x


def seg(x, name, guard=0.15):
    t0, t1 = TIMES[name]
    a, b = int((t0 + guard) * FS), int((t1 - guard) * FS)
    return x[a:min(b, len(x))]


def rms(x):
    return np.sqrt(np.mean(x ** 2)) + 1e-20


def db(x):
    return 20 * np.log10(np.abs(x) + 1e-20)


def level_match(real, plug):
    """Re-gain plug to match real on the quiet clean-sweep head (near-linear in every mode)."""
    t0, _ = TIMES["sweep_clean"]
    a, b = int((t0 + 0.5) * FS), int((t0 + 3.0) * FS)
    g = rms(real[a:b]) / rms(plug[a:b])
    return plug * g, db(g)


# ------------------------------------------------------------------- Farina ESS deconvolution
def farina_inverse(sec, f0=gts.SWEEP_F0, f1=gts.SWEEP_F1):
    """Inverse filter for the ESS: time-reversed sweep with +6 dB/oct amplitude correction so
    conv(sweep, inverse) ~ Dirac."""
    n = int(sec * FS)
    t = np.arange(n) / FS
    k = np.log(f1 / f0)
    sweep = np.sin(2 * np.pi * f0 * sec / k * (np.exp(t / sec * k) - 1.0))
    return sweep[::-1] * np.exp(-t / sec * k), k, n


def deconvolve(y, sec):
    """Return (impulse_response, peak_index, k, n) for a swept response y."""
    inv, k, n = farina_inverse(sec)
    L = len(y) + len(inv) - 1
    H = np.fft.irfft(np.fft.rfft(y, L) * np.fft.rfft(inv, L), L)
    return H, int(np.argmax(np.abs(H))), k, n


def linear_tf(y, sec, nfft=16384):
    """Linear transfer function (freqs, complex H) by windowing the linear IR of a swept response."""
    H, pk, k, n = deconvolve(y, sec)
    half = nfft // 2
    a, b = max(0, pk - half), min(len(H), pk + half)
    ir = H[a:b]
    F = np.fft.rfftfreq(len(ir), 1 / FS)
    return F, np.fft.rfft(ir)


def harmonic_order_tf(y, sec, order, nfft=8192):
    """Transfer function of a given harmonic order (1=linear, 2=H2, ...) from a swept response.
    Order m appears as a pre-echo at dt = sec*ln(m)/k before the linear peak."""
    H, pk, k, n = deconvolve(y, sec)
    dt = sec * np.log(order) / k
    idx = int(round(pk - dt * FS))
    half = nfft // 2
    a, b = max(0, idx - half), min(len(H), idx + half)
    ir = H[a:b]
    F = np.fft.rfftfreq(len(ir), 1 / FS)
    Hf = np.fft.rfft(ir)
    # Frequencies map to the FUNDAMENTAL the harmonic came from: harmonic at freq f was excited
    # by fundamental f/order, but here we report magnitude vs the harmonic's own analysis freq.
    return F, Hf


# ----------------------------------------------------------------------------------- metrics
def frequency_response(real, plug):
    """RMS-dB error vs real per 1/3-octave band, from the clean-sweep linear TF."""
    Fr, Hr = linear_tf(seg(real, "sweep_clean", guard=0.3), gts.SWEEP_CLEAN_SEC)
    Fp, Hp = linear_tf(seg(plug, "sweep_clean", guard=0.3), gts.SWEEP_CLEAN_SEC)
    mr, mp = db(Hr), db(Hp)
    # normalise each to its own 1 kHz value, then compare
    mr -= mr[np.argmin(np.abs(Fr - 1000))]
    mp -= mp[np.argmin(np.abs(Fp - 1000))]
    rows = []
    for fc in THIRD_OCT_CENTRES:
        lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        br = (Fr >= lo) & (Fr < hi)
        bp = (Fp >= lo) & (Fp < hi)
        if br.sum() and bp.sum():
            rows.append((fc, float(mr[br].mean()), float(mp[bp].mean())))
    err = np.array([p - r for _, r, p in rows])
    return rows, float(np.sqrt(np.mean(err ** 2))), float(np.max(np.abs(err)))


def thd_vs_freq(y, sec, max_order=8):
    """Continuous THD%(f) from a driven sweep, summarised per band. f is the fundamental freq."""
    H, pk, k, n = deconvolve(y, sec)
    nfft = 8192
    half = nfft // 2

    def order_mag(order):
        dt = sec * np.log(order) / k
        idx = int(round(pk - dt * FS))
        a, b = max(0, idx - half), min(len(H), idx + half)
        ir = H[a:b]
        return np.fft.rfftfreq(len(ir), 1 / FS), np.abs(np.fft.rfft(ir))

    Ff, h1 = order_mag(1)
    orders = []
    for m in range(2, max_order + 1):
        Fm, hm = order_mag(m)
        # harmonic m at analysis-freq f came from fundamental f/m; resample onto fundamental grid
        hm_on_f = np.interp(Ff, Fm / m, hm, left=0, right=0)
        orders.append(hm_on_f)
    orders = np.array(orders)
    thd = np.sqrt(np.sum(orders ** 2, axis=0)) / (h1 + 1e-20)
    bands = {}
    for name, lo, hi in THD_BANDS:
        m = (Ff >= lo) & (Ff < hi) & (h1 > h1.max() * 1e-3)
        bands[name] = float(100 * np.median(thd[m])) if m.sum() else float('nan')
    return bands


def harmonics_at_tones(x):
    """Per-tone H2..H10 (dB below fundamental) + odd/even energy split."""
    out = {}
    for f in gts.TONE_FREQS:
        s = seg(x, f"tone_{f:g}")
        w = np.hanning(len(s))
        X = np.abs(np.fft.rfft(s * w))
        res = FS / len(s)

        def mag(fc):
            k = int(round(fc / res))
            lo, hi = max(0, k - 3), min(len(X), k + 4)
            return np.sqrt(np.sum(X[lo:hi] ** 2))
        h = np.array([mag((i + 1) * f) for i in range(10)])
        thd = np.sqrt(np.sum(h[1:] ** 2)) / (h[0] + 1e-20)
        even = np.sqrt(np.sum(h[1::2] ** 2))   # h[1]=H2, h[3]=H4...
        odd = np.sqrt(np.sum(h[2::2] ** 2))    # h[2]=H3, h[4]=H5...
        out[f] = dict(thd=100 * thd, h2=db(h[1] / h[0]), h3=db(h[2] / h[0]),
                      odd_even_db=db((odd + 1e-20) / (even + 1e-20)))
    return out


def imd(x, name, f_lo, f_hi):
    """Intermodulation product energy (sum/diff orders) relative to the two carriers, in dB."""
    s = seg(x, name)
    w = np.hanning(len(s))
    X = np.abs(np.fft.rfft(s * w))
    res = FS / len(s)

    def mag(fc):
        if fc <= 0 or fc >= FS / 2:
            return 1e-20
        k = int(round(fc / res))
        lo, hi = max(0, k - 3), min(len(X), k + 4)
        return np.sqrt(np.sum(X[lo:hi] ** 2))
    carriers = mag(f_lo) + mag(f_hi)
    products = 0.0
    for a in (1, 2, 3):
        for b in (1, 2):
            for s2 in (f_hi + a * f_lo, f_hi - a * f_lo, a * f_hi + b * f_lo, a * f_hi - b * f_lo):
                if abs(s2 - f_lo) > res * 4 and abs(s2 - f_hi) > res * 4:
                    products += mag(s2) ** 2
    return db(np.sqrt(products) / (carriers + 1e-20))


def dynamics(x, name, f, n_windows=8):
    """Along a plucked-note decay: instantaneous level (dBFS) and THD% in successive windows."""
    s = seg(x, name, guard=0.02)
    win = len(s) // n_windows
    rows = []
    for i in range(n_windows):
        w = s[i * win:(i + 1) * win]
        if len(w) < 256:
            continue
        ww = np.hanning(len(w))
        X = np.abs(np.fft.rfft(w * ww))
        res = FS / len(w)

        def mag(fc):
            k = int(round(fc / res))
            lo, hi = max(0, k - 2), min(len(X), k + 3)
            return np.sqrt(np.sum(X[lo:hi] ** 2))
        h = np.array([mag((j + 1) * f) for j in range(6)])
        thd = np.sqrt(np.sum(h[1:] ** 2)) / (h[0] + 1e-20)
        rows.append((db(rms(w)), 100 * thd))
    return rows


def compression(x):
    return [(d, db(rms(seg(x, f"lvl_{d}")))) for d in gts.LEVEL_STEPS_DB]


# --------------------------------------------------------- comprehensive_report.py compatibility
# Second, independent metric family imported from the Guitar-Pedal-Plugin-Template harness
# (analysis/comprehensive_report.py + dashboard_gen.py there — see that project's analysis/
# README.md) to drive the per-band JSON the dashboard renders: Welch/CSD transfer (coarser but
# very robust) instead of the Farina linear_tf() above, and a harmonic-gated Farina THD curve
# instead of thd_vs_freq(). The CLI report + metrics above are untouched and still what
# run_validation.py / internal_checks.py / mid_eq_audit.py use.
ORIG = "analysis/test_signal_48k.wav"
ORDER_LIMIT_MARGIN = 0.95   # keep order N only while N*f <= SWEEP_F1*this (edge spike sits AT f1/N)

load = load_mono   # comprehensive_report.py's naming for this module's WAV loader


def is_full_length(x, orig, frac=0.95):
    """Guard against truncated captures: a short file's missing segments read as zeros and produce
    garbage (huge fake deltas / -200 dB nulls) rather than an honest skip. Check BEFORE align()
    (align pads to full length, which would defeat this check)."""
    return len(x) >= frac * len(orig)


def align(render, orig):
    """Integer-sample align `render` to `orig` via FFT cross-correlation on the clean sweep."""
    a, b = TIMES["sweep_clean"]
    ref = orig[int(a * FS):int(b * FS)]
    s = render[int(a * FS):int(min(len(render), (b + 0.5) * FS))]
    n = min(len(ref), len(s))
    corr = sps.correlate(s[:n] - s[:n].mean(), ref[:n] - ref[:n].mean(), mode="full", method="fft")
    lag = int(np.argmax(np.abs(corr))) - (n - 1)
    if lag > 0:
        render = render[lag:]
    elif lag < 0:
        render = np.concatenate([np.zeros(-lag), render])
    if len(render) < len(orig):
        render = np.concatenate([render, np.zeros(len(orig) - len(render))])
    return render[:len(orig)], lag


def seg_of(x, name):
    t0, t1 = TIMES[name]
    return x[int(t0 * FS):int(t1 * FS)]


def transfer(out, inp):
    """Welch/CSD magnitude transfer function, dB — see the module docstring above this section."""
    f, Pxy = sps.csd(inp, out, FS, nperseg=8192)
    f, Pxx = sps.welch(inp, FS, nperseg=8192)
    H = np.abs(Pxy) / (Pxx + 1e-20)
    return f, 20 * np.log10(H + 1e-12)


def fractional_octave_freqs(f_lo=20.0, f_hi=20000.0, frac=3):
    import math
    n = int(math.floor(frac * math.log2(f_hi / f_lo)))
    return [f_lo * 2.0 ** (i / frac) for i in range(n + 1)]


def thd(x, f0):
    """Discrete-tone THD%: harmonics 2..8 RSS re the fundamental, +-3 bin window."""
    w = np.hanning(len(x)); X = np.abs(np.fft.rfft(x * w)); fr = np.fft.rfftfreq(len(x), 1 / FS)
    def amp(fc):
        i = int(np.argmin(np.abs(fr - fc))); return np.max(X[max(0, i - 3):i + 4])
    fund = amp(f0)
    harm = np.sqrt(sum(amp(f0 * k) ** 2 for k in range(2, 9)))
    return 100 * harm / (fund + 1e-20), fund


def harmonic_thd_curve(capture_sweep, ref_sweep, max_order=7, order_limit=True):
    """Continuous THD(f) via Farina exponential-sweep harmonic separation. Deconvolve the captured
    driven sweep against the clean reference sweep; the N-th harmonic IR is time-advanced by
    dt_N = T*ln(N)/ln(f1/f0), so gate each, FFT, and map to the fundamental axis. Returns
    (freqs, thd_pct, {order: |H|}).

    ORDER LIMITING (default on): the reference sweep has no energy above SWEEP_F1 (20 kHz), so
    order N is only measurable while N*f <= SWEEP_F1 — past that the regularised division blows up
    and produces a spurious edge spike at f = SWEEP_F1/N. Nothing below ~2714 Hz (7th order) is
    affected; above ~9.5 kHz no order past H2 is measurable, and above ~12 kHz THD doesn't exist at
    48 kHz at all (H2 lands past Nyquist)."""
    n = min(len(capture_sweep), len(ref_sweep))
    y = capture_sweep[:n].astype(np.float64); x = ref_sweep[:n].astype(np.float64)
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    X = np.fft.rfft(x, nfft); Y = np.fft.rfft(y, nfft)
    eps = 1e-6 * np.mean(np.abs(X) ** 2)
    ir = np.fft.irfft(Y * np.conj(X) / (np.abs(X) ** 2 + eps), nfft)
    T_sweep = n / FS
    R = np.log(gts.SWEEP_F1 / gts.SWEEP_F0)

    def gated_spectrum(order):
        dt = T_sweep * np.log(order) / R
        center = int(round((-dt) * FS)) % nfft
        if order == 1:
            half = int(0.04 * FS)
        else:
            gap = (T_sweep / R) * np.log((order + 1) / order)   # secs to the next-higher order
            half = int(0.35 * gap * FS)                          # 35% of the gap -> no overlap
        half = max(half, int(0.01 * FS))
        idx = (np.arange(center - half, center + half) % nfft)
        spec = np.fft.rfft(ir[idx] * np.hanning(len(idx)), nfft)
        return np.fft.rfftfreq(nfft, 1 / FS), np.abs(spec)

    fr, H1 = gated_spectrum(1)
    Hn = {1: H1}
    for N in range(2, max_order + 1):
        frN, mag = gated_spectrum(N)
        Hn[N] = np.interp(fr, frN / N, mag, left=0.0, right=0.0)   # remap harmonic->fundamental axis
        if order_limit:
            Hn[N] = np.where(N * fr <= gts.SWEEP_F1 * ORDER_LIMIT_MARGIN, Hn[N], 0.0)
    with np.errstate(divide="ignore", invalid="ignore"):
        harm = np.sqrt(sum(Hn[N] ** 2 for N in range(2, max_order + 1)))
        thd_pct = 100.0 * harm / (H1 + 1e-20)
    return fr, thd_pct, Hn


def thd_max_measurable_hz(max_order=2):
    """Highest fundamental at which THD is measurable from this sweep, using orders up to
    `max_order`. THD needs at least H2, so the ceiling is SWEEP_F1*margin/2 ~= 9.5 kHz — and no
    test signal can beat FS/4 = 12 kHz at 48 kHz, because H2 lands past Nyquist above that."""
    return min(gts.SWEEP_F1 * ORDER_LIMIT_MARGIN / max_order, FS / (2.0 * max_order))


def frac_align(test, ref):
    """Shift `test` by a FRACTIONAL number of samples to best line up with `ref` (FFT phase ramp;
    parabolic refinement of the xcorr peak). Integer alignment isn't enough for a deep null —
    1 sample at 20 kHz is ~150 deg of phase error."""
    n = min(len(test), len(ref))
    a = test[:n] - test[:n].mean(); b = ref[:n] - ref[:n].mean()
    corr = sps.correlate(a, b, mode="full", method="fft")
    k = int(np.argmax(np.abs(corr)))
    if 0 < k < len(corr) - 1:
        y0, y1, y2 = np.abs(corr[k - 1]), np.abs(corr[k]), np.abs(corr[k + 1])
        denom = (y0 - 2 * y1 + y2); delta = 0.5 * (y0 - y2) / denom if denom else 0.0
    else:
        delta = 0.0
    lag = (k - (n - 1)) + delta
    X = np.fft.rfft(test); freqs = np.fft.rfftfreq(len(test))
    return np.fft.irfft(X * np.exp(-1j * 2 * np.pi * freqs * (-lag)), len(test))


def null_depth(ref, test):
    """Optimal-gain-match `test` to `ref`, subtract, return (null_dB, applied_gain_dB). frac_align
    `test` first — this is the RAW null against whatever's passed in, no integer-lag search (see
    null_test.best_null for the version run_validation.py uses, which also does an integer-lag
    search before the fractional one)."""
    g = float(np.dot(ref, test) / (np.dot(test, test) + 1e-30))
    resid = ref - g * test
    null_db = 20 * np.log10((np.sqrt(np.mean(resid ** 2)) + 1e-20) / (np.sqrt(np.mean(ref ** 2)) + 1e-20))
    return null_db, 20 * np.log10(abs(g) + 1e-20)


# ------------------------------------------------------------------------------------- report
def main():
    real_path, plug_path = sys.argv[1], sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else "(render)"
    real = load_mono(real_path)
    plug, gdb = level_match(real, load_mono(plug_path))

    print(f"\n=== {label} ===")
    print(f"plugin re-gained {gdb:+.2f} dB to match real (clean-sweep head)")

    rows, rms_err, max_err = frequency_response(real, plug)
    print(f"\n  FREQUENCY RESPONSE (rel 1 kHz): RMS error {rms_err:.2f} dB, worst {max_err:.2f} dB")
    print("  %-7s %8s %8s %7s" % ("Hz", "REAL", "PLUG", "Δ"))
    for fc, r, p in rows:
        if fc in (50, 100, 250, 500, 1000, 2000, 4000, 8000, 16000):
            print("  %-7g %8.2f %8.2f %+7.2f" % (fc, r, p, p - r))

    print("\n  THD% vs FREQUENCY by band (driven sweeps):")
    print("  %-10s" % "level" + "".join("%9s" % b[0] for b in THD_BANDS))
    for lvl in gts.DRIVEN_LEVELS_DB:
        br = thd_vs_freq(seg(real, f"sweep_drv_{lvl}", guard=0.3), gts.SWEEP_DRIVEN_SEC)
        bp = thd_vs_freq(seg(plug, f"sweep_drv_{lvl}", guard=0.3), gts.SWEEP_DRIVEN_SEC)
        print("  %-10s" % f"{lvl}dB R" + "".join("%9.1f" % br[b[0]] for b in THD_BANDS))
        print("  %-10s" % f"{lvl}dB P" + "".join("%9.1f" % bp[b[0]] for b in THD_BANDS))

    print("\n  HARMONICS at tones (H2,H3 dB below fund; odd-even dB, +=odd-dominant):")
    hr, hp = harmonics_at_tones(real), harmonics_at_tones(plug)
    print("  %-7s  %-22s  %-22s" % ("Hz", "REAL", "PLUGIN"))
    for f in gts.TONE_FREQS:
        print("  %-7g  THD%5.1f H2%4.0f H3%4.0f o/e%+4.0f  THD%5.1f H2%4.0f H3%4.0f o/e%+4.0f"
              % (f, hr[f]['thd'], hr[f]['h2'], hr[f]['h3'], hr[f]['odd_even_db'],
                 hp[f]['thd'], hp[f]['h2'], hp[f]['h3'], hp[f]['odd_even_db']))

    print("\n  IMD (products dB below carriers):")
    for nm, lo, hi in (("imd_smpte", 60, 7000), ("imd_ccif", 19000, 20000)):
        print("  %-10s REAL %+6.1f   PLUG %+6.1f" % (nm, imd(real, nm, lo, hi), imd(plug, nm, lo, hi)))

    print("\n  DYNAMICS (plucked decay: level dBFS -> THD%):")
    for nm, f in (("decay_220", 220), ("decay_1k", 1000)):
        dr, dp = dynamics(real, nm, f), dynamics(plug, nm, f)
        print(f"  {nm}: " + " ".join("R%.0f/%.0f" % (l, t) for l, t in dr[:5]))
        print(f"  {' '*len(nm)}  " + " ".join("P%.0f/%.0f" % (l, t) for l, t in dp[:5]))

    print("\n  COMPRESSION (1 kHz, in dBFS -> out dBFS):")
    cr, cp = dict(compression(real)), dict(compression(plug))
    for d in gts.LEVEL_STEPS_DB:
        print("  in %3d : REAL %7.2f  PLUG %7.2f  Δ %+5.2f" % (d, cr[d], cp[d], cp[d] - cr[d]))


if __name__ == "__main__":
    main()
