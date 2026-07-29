#!/usr/bin/env python3
"""v1.5 step 3 — refit the `warp*` high-shelf after the ADAA identity-region early-out.

WHY A REFIT IS MANDATORY, NOT OPTIONAL
--------------------------------------
`warp*` was fitted (2026-06-30) to a "warp-free baseline vs 8x" deficit that was mostly NOT
bilinear warp: CPU_AUDIT.md §5 shows 12.04 of the 13.12 dB at 12 kHz and 24.08 of the 32.52 at
16 kHz (1x) was the ADAA identity-region midpoint droop. Remove the droop and leave the shelf
alone and 1x/2x/4x come out BRIGHTER than 8x — measured +1.76 / +2.44 / +2.10 dB at 2x
(8 / 12 / 16 kHz). Sixth instance of P7's rule: two corrections overlapping in band and in
keying, fitted as one.

THE INSTRUMENT
--------------
`tests/OSFidelity` (a), run with `warpScaleDb = 0`, i.e. the shelf disabled — the same
warp-free-baseline-vs-8x construction the original fit used, re-measured with the droop gone.
This is an INTERNAL-CONSISTENCY target (do the low OS factors sound like 8x?), not a
capture-accuracy one, so it needs no renders and the 44-capture null is not its arbiter — the
null's job here is only to confirm 8x/4x, the render path, did not move.

Two things the model has to get right, both easy to get wrong:
  * the shelf is applied ONCE PER PEDAL CHANNEL and the two run in series, so the path carries
    2x its dB response;
  * (a) is measured RELATIVE TO 8x, which carries the shelf too — so the residual is
    deficit(rate) + 2*S(rate) - 2*S(8x), not deficit(rate) + 2*S(rate).

TWO CONSTRAINTS THAT ARE NOT OPTIONAL
-------------------------------------
  * the shelf must VANISH at 8x. Left free, the grid happily picks a cap so low that the lift is
    rate-independent and the whole thing "works" only through per-rate prewarping differences —
    which moves the 8x accuracy reference, i.e. it buys 1x by re-voicing the render path.
  * the prewarped POLE (pivot*sqrt(ghi), see shelfCoeffs) must stay inside Nyquist at the lowest
    session rate. Unconstrained, the fit wants pivot 18 kHz — which is an UNSTABLE filter at 1x on
    a 44.1 kHz session. MonarchChannel::warpPoleMaxFrac enforces the same bound at runtime.

RESULT (2026-07-30, shipped): scale 10.6 -> 1.0, exp 2.20 -> 1.80, pivot 6500 -> 17000, cap 3.0 ->
1.0. Weighted rms 0.415 (no shelf) -> 0.155. Measured on the real processor, deviation from 8x:
1x -3.46/-13.21/-32.59 -> +0.28/+0.22/-0.75 and 2x +1.02/-0.47/-3.51 -> +0.08/+0.05/-0.14 at
8/12/16 kHz. 44-capture null neutral (median -23.45 unchanged). See CPU_AUDIT.md §5b.

Usage:  python3.11 analysis/v15_warp_refit.py [--show scale,exp,pivot,cap]
        (no args = print the shelf-disabled baseline, then grid-search)
"""
import argparse
import math

FS = 48000.0
RATES = {"1x": 48000.0, "2x": 96000.0, "4x": 192000.0, "8x": 384000.0}

# tests/OSFidelity (a), 2026-07-30: early-out ON, warpScaleDb = 0, level 5e-4. dB vs the 8x arm.
# ⚠ At (a)'s previous 0.01 FS this table read -0.41/-1.30/-3.78/-8.03 at 1x — read through the
# soft clipper, not linear (see the level comment in tests/OSFidelity.cpp). The corrected table is
# the whole finding: once the ADAA droop is gone, the residual BILINEAR WARP is ~nothing below
# 12 kHz at every rate, and -3.08 dB at 16 kHz at 1x alone. The model below reproduces a candidate
# shelf's measured contribution to 0.01 dB at every rate against THIS baseline, and was off by
# 1.64 dB at 1x/8 kHz against the contaminated one.
FREQS = [100, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000]
DEFICIT = {
    "1x": [+0.15, +0.15, +0.16, +0.17, +0.17, +0.12, -0.14, -0.86, -3.08],
    "2x": [+0.03, +0.03, +0.04, +0.04, +0.04, +0.03, -0.03, -0.16, -0.44],
    "4x": [+0.01, +0.01, +0.01, +0.01, +0.01, +0.01, -0.01, -0.03, -0.08],
    "8x": [0.0] * len(FREQS),
}

# The audible top matters more than the Nyquist edge: a first-order shelf cannot be flat at 8 kHz
# AND steep at 16 kHz, and the guitar has energy in the presence band and none at 16 kHz. Same
# weighting rationale the 06-30 fit used for choosing a moderate pivot.
WEIGHT = {100: 1.0, 250: 1.0, 500: 1.0, 1000: 1.0, 2000: 2.0,
          4000: 4.0, 8000: 4.0, 12000: 2.0, 16000: 0.7}


def shelf_db(f, rate, lift_db, pivot):
    """MonarchChannel::shelfCoeffs(1, ghi, pivot) at `rate`, DC-normalized, evaluated at f Hz."""
    if lift_db == 0.0:
        return 0.0
    ghi = 10.0 ** (lift_db / 20.0)
    rt = math.sqrt(ghi)          # glo = 1
    fz, fp = pivot / rt, pivot * rt
    K = 2.0 * rate
    wz = K * math.tan(math.pi * fz / rate)
    wp = K * math.tan(math.pi * fp / rate)
    a0 = K + wp
    a1 = (wp - K) / a0
    b0 = ghi * (K + wz) / a0
    b1 = ghi * (wz - K) / a0
    dc = (b0 + b1) / (1.0 + a1)  # prepareLinear's DC normalization
    b0, b1 = b0 / dc, b1 / dc
    w = 2.0 * math.pi * f / rate
    z = complex(math.cos(-w), math.sin(-w))
    return 20.0 * math.log10(abs((b0 + b1 * z) / (1.0 + a1 * z)))


def lift(rate, scale, exp, cap):
    return min(cap, scale * (48000.0 / rate) ** exp)


def residual(rate_name, f, scale, exp, pivot, cap):
    i = FREQS.index(f)
    s = shelf_db(f, RATES[rate_name], lift(RATES[rate_name], scale, exp, cap), pivot)
    s8 = shelf_db(f, RATES["8x"], lift(RATES["8x"], scale, exp, cap), pivot)
    return DEFICIT[rate_name][i] + 2.0 * (s - s8)   # two pedal channels in series


def cost(scale, exp, pivot, cap):
    acc = wt = 0.0
    for r in ("1x", "2x", "4x"):
        for f in FREQS:
            w = WEIGHT[f]
            acc += w * residual(r, f, scale, exp, pivot, cap) ** 2
            wt += w
    return math.sqrt(acc / wt)


def show(scale, exp, pivot, cap):
    print(f"\n  warpScaleDb={scale:.4g}  warpExp={exp:.4g}  warpPivotHz={pivot:.0f}  warpMaxDb={cap:.4g}")
    print("  per-rate shelf lift (dB): " + "  ".join(
        f"{r} {lift(RATES[r], scale, exp, cap):.3f}" for r in RATES))
    print("\n  predicted OSFidelity (a) — deviation from 8x (dB)")
    print("  " + "Hz".ljust(6) + "".join(f"{f:8.0f}" for f in FREQS))
    for r in ("1x", "2x", "4x"):
        print("  " + r.ljust(6) + "".join(f"{residual(r, f, scale, exp, pivot, cap):+8.2f}" for f in FREQS))
    print(f"\n  weighted rms = {cost(scale, exp, pivot, cap):.3f} dB")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--show", help="scale,exp,pivot,cap")
    a = ap.parse_args()
    if a.show:
        show(*(float(x) for x in a.show.split(",")))
        raise SystemExit

    print("baseline (shelf disabled):")
    show(0.0, 1.0, 17000.0, 0.0)

    MIN_SESSION_RATE = 44100.0   # lowest rate the shelf is fitted for; see warpPoleMaxFrac
    POLE_MAX_FRAC = 0.42
    MAX_LIFT_AT_8X = 0.05        # dB — the shelf must not re-voice the accuracy reference

    best = None
    for pivot in range(5000, 17001, 500):
        for scale in [x / 8 for x in range(0, 161)]:            # 0 … 20
            for exp in [x / 20 for x in range(20, 121)]:        # 1.0 … 6.0
                if scale * 0.125 ** exp > MAX_LIFT_AT_8X:
                    continue
                for cap in [x / 10 for x in range(5, 41)]:      # 0.5 … 4.0
                    if pivot * 10.0 ** (cap / 40.0) > POLE_MAX_FRAC * MIN_SESSION_RATE:
                        continue
                    c = cost(scale, exp, pivot, cap)
                    if best is None or c < best[0]:
                        best = (c, scale, exp, pivot, cap)
    print("\n=== best on the weighted, constrained grid ===")
    show(*best[1:])
