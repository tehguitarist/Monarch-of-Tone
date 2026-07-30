#!/usr/bin/env python3
"""v1.5 step 5 — re-key the `warp*` shelf from OS RATE to DRIVE, and fit it to an exact target.

WHY THE SHELF HAS TO BE RE-KEYED, NOT JUST REFITTED
---------------------------------------------------
`warp*` corrects Stage 1's bilinear top-octave droop. Until step 5 Stage 1 ran at the OS rate, so
that droop shrank with the OS factor and the shelf was keyed to the rate: `scale*(48k/rate)^exp`.
Step 5 moves Stage 1 to the BASE rate at every factor, which removes the rate-dependence AT SOURCE:
`tests/OSFidelity` (a) with the shelf disabled reads, relative to its own 100 Hz plateau,

    1x  -0.18 / -0.24 / -0.26 dB at 8 / 12 / 16 kHz      (was -0.29 / -1.01 / -3.23)
    2x  -0.04 / -0.05 / -0.06                            (was -0.06 / -0.19 / -0.47)

i.e. Stage 1 was ~97 % of ALL the remaining bilinear warp in the plugin, and the OS factors now
agree to a quarter of a dB with NO shelf at all — three times better than the +0.28/-0.75 that
shipped WITH the shelf in step 3. So the rate-keyed law has nothing left to correct and must go to
zero, or it becomes an over-correction (measured: leaving it in place puts 1x +2.23 dB ABOVE 8x at
16 kHz).

What replaces it is the same physical defect seen properly. The droop did not disappear when the
rate-dependence did — it became ABSOLUTE, present at every factor, and it is keyed to DRIVE:
Z_upper is `R_leg || C2(100 pF)`, so C2's corner is 1/(2*pi*R_leg*C2) = 75.8 kHz at drive 0.2 and
15.8 kHz at drive 1.0. It walks INTO the top octave as DRIVE rises, and warping a corner that is
just outside the band is what the deficit is.

THE TARGET IS EXACT — this is the one EQ fit in the project that has a right answer
-----------------------------------------------------------------------------------
`v15_stage1_warp_probe fit` emits Stage 1's base-rate magnitude against an 8x-of-base solve of the
SAME filter, per (channel, session rate, drive, f). No captures, no null, no NAM model: the
reference is the same code at 64x the frequency resolution, whose own residual warp is ~1/64 of
what is being measured. So the usual FR_THD_AUDIT discipline (FR generates, the null decides) does
not apply here — the null CANNOT see this (it renders at 4x and the whole effect is above 8 kHz,
where the captures carry +-18 dB of spread). The null's job is only to confirm nothing else moved.

KEYED ON R_leg, NOT ON THE KNOB — which is what makes it right on Red for free
------------------------------------------------------------------------------
Every other drive-keyed instrument in this plugin (`bassCut*`, `bassBoost*`, `driveMakeup`) is
keyed to the raw knob and fitted to the Yellow captures, which is why dsp.md carries a standing
"deferred refinement" note about Red being mis-keyed by 1/6 of a knob turn. This one is keyed to
the physical `R_leg = floor + DRIVE*100k`, so Red's 17.7 k floor enters the law directly and one
expression covers both channels. The fit below is scored over BOTH channels and all four session
rates at once, and the residual table proves the single law covers them.

Usage:  python3.11 analysis/v15_s1warp_fit.py [--show lift0,rref,expn,pivot]
"""
import argparse
import csv
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = os.path.join(HERE, ".cache", "s1warp_target.csv")

# Fit band. Below 1 kHz the target is 0.00 by construction (C2 is an open circuit down there), and
# above ~0.36*rate a FIRST-ORDER shelf physically cannot follow: the warp diverges at Nyquist
# (+4.5 dB at 20 kHz on a 48 k session, +6.8 at drive 1.0) while a shelf flattens out at its own
# lift. That is an accepted undershoot, not a fit failure — see the residual table's `>band` column.
F_LO = 1000.0
BAND_FRAC = 0.36

# Weight by where a guitar amp'd overdrive actually has energy AND where the deficit is real.
# 4-8 kHz is the presence band the ear is most sensitive to; 16 kHz is worth a third of it.
def weight(f):
    if f < 2000.0:
        return 1.0
    if f < 6000.0:
        return 4.0
    if f < 11000.0:
        return 3.0
    return 1.0


def shelf_db(f, rate, lift_db, pivot):
    """MonarchChannel::shelfCoeffs(1, ghi, pivot) at `rate`, DC-normalized, evaluated at f Hz."""
    if lift_db <= 0.0:
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


def peak_db(f, rate, gain_db, centre, Q):
    """MonarchChannel::peakCoeffs (RBJ peaking biquad, designed directly in the digital domain)."""
    if gain_db <= 0.0:
        return 0.0
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * centre / rate
    alpha = math.sin(w0) / (2.0 * Q)
    a0 = 1.0 + alpha / A
    b0 = (1.0 + alpha * A) / a0
    b1 = (-2.0 * math.cos(w0)) / a0
    b2 = (1.0 - alpha * A) / a0
    a1 = (-2.0 * math.cos(w0)) / a0
    a2 = (1.0 - alpha / A) / a0
    w = 2.0 * math.pi * f / rate
    z = complex(math.cos(-w), math.sin(-w))
    return 20.0 * math.log10(abs((b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z)))


def load():
    rows = []
    with open(CSV) as fh:
        for r in csv.DictReader(fh):
            rows.append((int(r["channel"]), float(r["rate"]), float(r["drive"]),
                         float(r["rleg"]), float(r["f"]), float(r["deficit_db"])))
    return rows


# ---- the LAW ---------------------------------------------------------------------------------
# Two things set the deficit and both are in closed form already, so the law is written in terms of
# them rather than in terms of the knob:
#   * C2's corner, fc = 1/(2*pi*R_leg*C2) — the drive axis;
#   * how far the band reaches toward Nyquist, i.e. the session rate — the rate axis.
# The bilinear warp of a one-pole at fc, evaluated at the shelf's own pivot, is
#   10*log10( (1+(ftilde/fc)^2) / (1+(fpiv/fc)^2) ),  ftilde = (rate/pi)*tan(pi*fpiv/rate)
# which is the whole mechanism with ZERO free parameters. `lift0` is the only fitted number: it
# scales that analytic prediction to the composite Av = 1 + Z_upper/Z_lower (the deficit is C2's
# warp seen through the gain stage, not C2's warp alone), and `pivot` places the shelf.
C2 = 100.0e-12


def analytic_lift(rleg, rate, pivot, lift0):
    fc = 1.0 / (2.0 * math.pi * rleg * C2)
    ftil = (rate / math.pi) * math.tan(math.pi * pivot / rate)
    d = 10.0 * math.log10((1.0 + (ftil / fc) ** 2) / (1.0 + (pivot / fc) ** 2))
    return max(0.0, lift0 * d)


def residuals(lift0, pivot, rows):
    out = []
    for ch, rate, drive, rleg, f, target in rows:
        if f < F_LO or f > BAND_FRAC * rate:
            continue
        s = shelf_db(f, rate, analytic_lift(rleg, rate, pivot, lift0), pivot)
        out.append((ch, rate, drive, f, target, s, s - target))
    return out


def cost(lift0, pivot, rows):
    acc = wt = 0.0
    for _, _, _, f, _, _, e in residuals(lift0, pivot, rows):
        w = weight(f)
        acc += w * e * e
        wt += w
    return math.sqrt(acc / wt)


def show(lift0, pivot, rows):
    print(f"\n  s1WarpLift0 = {lift0:.4g}   s1WarpPivotHz = {pivot:.0f}")
    print(f"  weighted rms over the fit band = {cost(lift0, pivot, rows):.3f} dB\n")
    print("  residual (shelf MINUS target; + = over-correcting), dB")
    print("  " + "ch  rate   drive".ljust(22) + "".join(f"{f'{k}k':>8}" for k in (4, 6, 8, 10, 12, 14, 16))
          + "     lift   >band")
    for ch in (0, 1):
        for rate in (44100.0, 48000.0, 88200.0, 96000.0):
            for drive in (0.0, 0.2, 0.5, 0.7, 1.0):
                sel = [r for r in residuals(lift0, pivot, rows)
                       if r[0] == ch and r[1] == rate and abs(r[2] - drive) < 1e-9]
                if not sel:
                    continue
                rleg = next(r[3] for r in rows if r[0] == ch and abs(r[2] - drive) < 1e-9)
                line = f"  {'YR'[ch]}  {rate/1000:5.1f}  {drive:4.2f}".ljust(22)
                for k in (4, 6, 8, 10, 12, 14, 16):
                    near = min(sel, key=lambda r: abs(r[3] - k * 1000.0))
                    line += f"{near[6]:+8.2f}" if abs(near[3] - k * 1000.0) < 600.0 else f"{'-':>8}"
                # worst uncorrected target above the fit band = the accepted first-order undershoot
                above = [t for c, rt, d, rl, f, t in rows
                         if c == ch and rt == rate and abs(d - drive) < 1e-9 and f > BAND_FRAC * rate]
                line += f"{analytic_lift(rleg, rate, pivot, lift0):9.2f}"
                line += f"{(max(above) if above else 0.0):8.2f}"
                print(line)
        print()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--show", help="lift0,pivot")
    a = ap.parse_args()
    rows = load()

    if a.show:
        show(*(float(x) for x in a.show.split(",")), rows)
        raise SystemExit

    print("baseline (no correction) — weighted rms of the raw deficit:")
    acc = wt = 0.0
    for ch, rate, drive, rleg, f, t in rows:
        if f < F_LO or f > BAND_FRAC * rate:
            continue
        w = weight(f)
        acc += w * t * t
        wt += w
    print(f"  {math.sqrt(acc/wt):.3f} dB")

    # MonarchChannel::warpPoleMaxFrac — shelfCoeffs prewarps the pole to pivot*sqrt(ghi), which must
    # stay well inside Nyquist at the LOWEST session rate the plugin will see. Step 3 set that bound
    # and it binds harder here, because this shelf is live at every OS factor, not just at 1x.
    MIN_RATE, POLE_MAX_FRAC = 44100.0, 0.42
    best = None
    for pivot in range(4000, 16001, 250):
        for lift0 in [x / 200.0 for x in range(20, 401)]:      # 0.10 … 2.00
            worst = max(analytic_lift(rl, rt, pivot, lift0) for _, rt, _, rl, _, _ in rows)
            if pivot * 10.0 ** (worst / 40.0) > POLE_MAX_FRAC * MIN_RATE:
                continue
            c = cost(lift0, pivot, rows)
            if best is None or c < best[0]:
                best = (c, lift0, pivot)
    print("\n=== best on the constrained grid ===")
    show(best[1], best[2], rows)
