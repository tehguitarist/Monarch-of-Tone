# DSP Rules

## WDF Implementation

- Use `chowdsp_wdf` (header-only, C++17), the **compile-time API** (`chowdsp::wdft` namespace) —
  not the runtime `chowdsp::wdf`.
- **Use `double` precision for all WDF types** — `float` causes audible NR-iteration errors in the
  diode models at audio rates.
- Nonlinear elements use explicit per-component datasheet parameters (circuit.md §6) — never
  generic defaults. Both clip stages use **symmetric `DiodePairT`** (never two `DiodeT`).
- VREF (BIAS) is signal ground throughout — model bipolar (±V around 0).
- **Never reconstruct the WDF tree at runtime for switch changes.** Linear stages: precomputed
  scattering matrices, switch via `setSMatrixData()`. Nonlinear clip switches: structural
  (include/bypass the diode path).

### Per-stage formulation (as implemented)
- **Stage 1** (IC_A, non-inverting): linear, **no R-type matrix**. Ideal op-amp decouples
  Z_lower/Z_upper, so it solves two one-ports — V-source = V(pin3+) → Z_lower → read current i;
  I-source = i → Z_upper → read voltage; V(NodeG) = V(pin3+) + i·Z_upper. No PolarityInverterT.
- **Stage 2** (IC_B, inverting ×−22): root R-type adaptor; the op-amp VCVS closes the loop and
  carries the inversion via terminal assignment (in+ = BIAS, in− = pin6−), so V(out) = −A·V(pin6−).
  Output read off the **passive** R10 port. No separate `PolarityInverterT`. The gate is the
  measured **−22 inverting gain**, not the presence of any specific element (a non-inverting VCVS
  assignment would be positive feedback → NaN, so a passing −22 proves the inversion is physical).
- **SW-1 soft clip:** current-source / diode-root. The virtual ground forces a known
  i_in = Vin/Z_in (Z_in = R9 + 1/sC7) into `R10 ∥ [R11 + DiodePairT]`, the diode as nonlinear
  root. SW-1 OFF reduces to V(pin7) = −i_in·R10 = −22·Vin. `nDiodes` (4th DiodePairT arg) scales
  Vt — pass `n_eff = 2·n_MA856`.
- **SW-2 hard clip:** R12(1k) always in series from pin7; `DiodePairT` shunt at node_HC → BIAS.
- **Tone:** 3-port R-type (parallel) adaptor at the TONE wiper (R_a / R_b+C8 / R13), R_a+R_b = 25k
  linear, updated per block under `ScopedDeferImpedancePropagation`.
- Nonlinear stages solved by Newton-Raphson via chowdsp's nonlinear solver (Wright-Omega).

### ⚠ Passive-port readout rule (load-bearing — prevents a real bug we hit)
When reading an output by combining R-type port voltages, **read only passive ports** (resistors,
caps, R+C series) so shared-node terms cancel in the same time frame. **Never read a
`ResistiveVoltageSourceT` / source port** — its `a`/`b` are scheduled a sample apart, so its node
voltage is a 2-point average of Vs[n], Vs[n−1] = a spurious low-pass. Reading Stage 1's output via
the source port (instead of `voltage(branch1) − voltage(driveR)`) once drooped the HF response
~0.2 dB by 5 kHz and dragged the gain peak down ~880 Hz — it looked like bilinear warping but was
this bug.

### Linear stages run oversampled (top-octave warp fix, 2026-06-29)
With the passive-port readout the WDF matches the analog circuit's bilinear transform to within the
expected warp at the gain peak (Stage-1 peak vs analog 3803 Hz: −74 Hz @48k). BUT near Nyquist that
warp is large: at 48 kHz the **top octave droops** (16 kHz −6.6 dB vs the 192 kHz solve), which A/B
showed as a real treble deficit vs the captures (NOT capture aliasing — NAM captures null to ~−50 dB
and are accurate up there). Fix: **the whole channel — linear stages included — now runs at the
oversampled rate** (`processSamplesUp` wraps `processPre`+`processClip`+`processPost`), so the warp
shrinks with the OS factor (16 kHz deficit: −2.4 dB @48k → −0.2 @96k → ~0 @192k). Voicing is now
(correctly) more accurate at higher OS. At **1x** the linear rate == session rate, so the warp
remains; a per-channel rate-scaled high-shelf (`warp*` in MonarchChannel, `×(48k/rate)^4`) roughly
compensates the recoverable 8–12 kHz there (16 kHz+ stays deficient at 1x — a first-order shelf
can't match the near-Nyquist cliff; use 2x+ for full top-octave fidelity). Prewarping was rejected
earlier (a fixed prewarp freezes the gain peak, but the analog peak sweeps ~2.8–5.0 kHz with DRIVE).

### prepareToPlay
Call `.prepare(sampleRate)` on **every** `CapacitorT` in both channels (missing one → silence /
wrong behaviour). Also reset both oversamplers, smoothed values, bypass crossfades, and init all
precomputed scattering matrices.

---

## chowdsp_wdf API Reference

`#include <chowdsp_wdf/chowdsp_wdf.h>`, namespace `chowdsp::wdft`.

```cpp
// Passive
wdft::ResistorT<double> r { 1.0e3 };
wdft::CapacitorT<double> c { 1.0e-6 };               // call c.prepare(sampleRate)
wdft::ResistorCapacitorSeriesT<double> rc { R, C };

// Adaptors
wdft::WDFSeriesT<double, decltype(a), decltype(b)> s { a, b };
wdft::WDFParallelT<double, decltype(a), decltype(b)> p { a, b };
wdft::PolarityInverterT<double, decltype(s)> inv { s };
wdft::RtypeAdaptor<double, upPort, ImpedanceCalc, Port0, Port1, ...> rtype { p0, p1, ... };
rtype.setSMatrixData({{ ... }});                     // switch topology, no tree rebuild

// Nonlinear symmetric pair (use DiodeQuality::Best — Wright-Omega, correct for audio)
wdft::DiodePairT<double, decltype(next), wdft::DiodeQuality::Best> dp { next, Is, Vt, n };

// Readout / deferred propagation
double v = wdft::voltage<double>(element);
{ wdft::ScopedDeferImpedancePropagation deferrer { port0, port1 }; r.setResistanceValue(R); }
```

SW-1 diode-root skeleton:
```cpp
wdft::ResistiveCurrentSourceT<double> iSrc { 220.0e3 };  // i_in ‖ R10
wdft::ResistorT<double> r11 { 6.8e3 };
wdft::WDFSeriesT<double, decltype(r11), decltype(iSrc)> fbSeries { r11, iSrc };
wdft::DiodePairT<double, decltype(fbSeries), wdft::DiodeQuality::Best> dp { fbSeries, Is_MA856, Vt, n_eff_MA856 };
// per sample: iSrc.setCurrent(i_in); dp.incident(fbSeries.reflected()); fbSeries.incident(dp.reflected());
//             V(pin7) = voltage(iSrc);   // passive read
```

---

## Dual-Channel & Signal Order

One `MonarchChannel` class, instantiated twice — `channelYellow{false}` (stock Stage 1),
`channelRed{true}` (fixed Hi-Gain Stage 1; the `hiGain` ctor flag selects the Stage-1 floor).
Each channel: Stage1 → Stage2/SW1 → rail-sat → SW2 → Tone → Volume + its own oversampler.

> **Signal order: Red is FIRST, Yellow is SECOND** (real pedal's flow). Member/parameter naming
> still lists Yellow first (save-state compatibility) but `processBlock` runs Red then Yellow.

```
input → channelRed.process() → channelYellow.process() → output
```

Independent APVTS params per channel (`drive_yellow`, `drive_red`, …). **No `hi_gain_*` param** —
Hi-Gain is fixed on Red.

---

## Oversampling

- `juce::dsp::Oversampling`, one per channel, now wrapping **the whole channel** (`processSamplesUp`
  → `processPre`+`processClip`+`processPost` → `processSamplesDown`), not just the clip span. Both
  `prepareLinear` AND `prepareClip` are re-called at the oversampled rate on factor change. So the OS
  factor changes anti-aliasing of the clip stages AND removes the linear stages' near-Nyquist
  bilinear warp (higher OS = more accurate top octave; see "Linear stages run oversampled" above).
- Two APVTS settings, both `AudioParameterChoice` "1x"/"2x"/"4x"/"8x":
  `oversampling_realtime` (live, default **2x**) and `oversampling_render` (default **4x**,
  selected when `isNonRealtime()`). **IIR low-latency live, FIR max-quality render.**
- **Bypassed channels skip the oversampler** (raw pass-through). Factor change → reinit both
  oversamplers at block start via `pendingOversamplingFactor` (one-block gap acceptable). Report
  latency.

## ADAA — rail-saturation only

First-order antiderivative antialiasing on the op-amp rail-sat knee (`railSaturateADAA`), in
addition to oversampling — replaces pointwise `f(x)` with `(F(x)−F(x₋₁))/(x−x₋₁)` (midpoint
fallback when `x≈x₋₁`). Most audible in **Boost** (rails are the only nonlinearity there). State
resets in `prepareClip`/`reset`; `setSupplyVoltage` recomputes it when the rails move.

**Diode-stage ADAA is deferred:** those stages are WDF nonlinear *roots* (not memoryless `y=f(x)`
maps) and chowdsp has no ADAA support for that case — true ADAA there is the research-grade
DAFx-2020 WDF-root method. Left to oversampling. (Ref: DAFx-2020 "Antiderivative antialiasing in
nonlinear wave digital filters".)

## Op-amp Rail Saturation

After each op-amp stage's linear solve, soft-saturate the output toward the rail (9V → 3.3V mean;
soft tanh knee starting at rail − 0.3, linear below it). Required so **Boost** clips like the
hardware. Load-bearing in Boost (always) and Distortion (linear Stage2 ×−22 → ~13.9 V clamped
before the hard-clip shunt); in **OD** the feedback soft-clip holds pin7 at ±1.64 V, far below the
knee, so it never engages — that is the tone-safety guarantee, and it is verified byte-for-byte.

### Asymmetric ceilings (v1.4 P2, 2026-07-28) — where Boost's even harmonics come from
The two ceilings are **not equal**: `railVPos = railV + railAsymV`, `railVNeg = railV − railAsymV`,
`railAsymV = 0.60 V`, each with its own knee. This is the entire mechanism behind Boost's even
series — H2/H4/H6 went from 21/29/39 dB short to within ~2 dB of the captures, with the internal
H2:H4:H6 ratios falling out of the clipping duty cycle automatically. The old empirical
`asymBoost` injection is retired to 0 (it now moves nothing). Three rules that came out of fitting it:

- **The sign is invisible to the harmonics.** ±railAsymV give identical magnitude spectra. It was
  fixed by the **null**: + improves the driven Boost nulls, − degrades them equally. Positive
  ceiling is the higher one.
- **An asymmetric clipper rectifies — strip the DC at source.** `railDcBlock` subtracts a 50 ms
  running mean (3.2 Hz, an octave below the 20 Hz sweep floor, so no real harmonic is touched)
  right after the rail-sat. Without it the DC step decays through C11/R14's **0.16 Hz** corner for
  ~1 s and smears into whatever follows — measured 8.7% DC on a loud tone vs the pedal's 0.08%.
  Keep it even if the asymmetry is ever removed: at `railAsymV = 0` it alone deepens the OD/Dist
  nulls 0.5–0.7 dB.
- **The asymmetry is load-gated, not global.** Distortion IS rail-clamped, so it inherits the
  asymmetry — and ungated that makes its even series ~26 dB hot, while the real pedal's Distortion
  is nearly symmetric (H2 −51.5 dBc vs Boost's −21.2). The physical difference is the load the clip
  switch selects: Boost/OD leave pin7 driving the 25k tone stack (~0.13 mA), Distortion's diode
  shunt makes it drive ~3.3 mA. Op-amp saturation voltages are quoted per load because both
  ceilings collapse toward the supply under load. So `railAsymLoadedScale` (fitted to **0**) scales
  the asymmetry when `sw2On`. `updateRails()` recomputes both ceilings and is called from BOTH
  `setSupplyVoltage` (moves the mean) and `setClippingMode` (changes the load) — never set the
  ceilings anywhere else.

The ADAA antiderivative handles the asymmetry correctly: with unequal ceilings `railSaturate` is no
longer odd, so `railAntideriv` is no longer even, but each side integrates from 0 with its own
parameters (for v<0 the negated integrand and reversed limits cancel), so the same |v| expression
holds per side, F stays continuous with F(0)=0, and the difference quotient stays exact across a
sample pair that straddles zero.

**Supply-voltage mod moves only the MEAN.** The asymmetry comes from fixed drops (the bias offset,
the output stage's Voh/Vol difference) that do not scale with the supply.

## Supply Voltage (9 / 12 / 18 V mod)

`supply_voltage` `AudioParameterChoice`, default 9V. `setSupplyVoltage(v)` moves the rail ceiling
to `railV = 3.3 + (v−9)·0.5` (knee = railV − 0.3): +0.5 V usable swing per +1 V supply. **Only the
op-amp ceiling scales** — diode thresholds are junction-fixed. 9V = the validated ±3.3 V baseline
exactly. Applied per block to both channels/strips.

## Even-Harmonic Injection (`MonarchChannel::injectEvenHarmonic`) — all three modes

> **Boost's mid/high path stays retired, but its LOW path is live** (v1.4 P2 → P3.2). P2 moved
> Boost's evens to the asymmetric rails and retired `asymBoost` to 0, and above the rail knee that
> is right. But the rails are knee-*triggered*, so below the knee the plugin was an exactly
> symmetric clipper reading ≈−160 dBc where the pedal reads −18…−59 — 30 of 143 H2 cells. P3.2 fixed
> that with `asymLowBoost` = −0.017: the low path is sourced from the clip output and is therefore
> **always-on**, which is exactly the property the rails lack, and its `lowEnv` wash-out hands over
> to the rails as drive rises. `asymBoost` (mid/high) remains 0 — the gap was never up there.
> The claim below that the circuit "structurally rejects" an internal asymmetry was only ever true
> of the diode modes — in Boost the rails ARE the nonlinearity, so there is nothing to reject.

The KOT's *diode* clippers are **symmetric** → no even harmonics from the topology, and they
structurally reject the circuit-accurate bias-shift route (an offset shifts clamp levels → equal
duty → DC, blocked downstream). So H2 is injected **empirically** at the clip output,
clip-depth-gated (clean stays symmetric) and DC-free (slow running-mean removal). The two paths
**split the spectrum** — this is load-bearing, see below:
- **Mid/high band:** sourced from a bounded `tanh(asymDriveScale·nodeG)` of the pre-clip drive
  (squares up then washes out at high drive, matching the captures' non-monotonic H2-vs-gain),
  **high-passed at `asymMidFc` = 400 Hz**.
- **Low band:** a second path sourced from a 150 Hz low-pass of the clip output (large only when
  clipping → self-gating), because Stage-1's high-shelf makes nodeG tiny <440 Hz so the mid/high
  gate misses low notes that still clip. Washed out by its **own** depth envelope (`lowEnv`,
  `asymLowWash`/`asymLowThresh`). Its clamp reference `clampRef` is per-mode
  (`asymClampOD`/`asymClampDist`, and `railV` in Boost), which is why it works unchanged in a mode
  with no diodes — that is what let P3.2 fix Boost by setting one coefficient.
- Per-mode coefficients (`asymOD/Dist/Boost`, `asymLowOD/Dist/Boost`). `asymBoost` is 0 (Boost's
  mid/high evens come from the rails); **`asymLowBoost` = −0.017** (P3.2). Empirical model of the
  coupling-cap blocking-distortion device physics, not a circuit element.

### Band split + low-band wash-out (v1.4 P3, 2026-07-28) — the two rules that came out of it

Both paths were running full-range, and **the same fact broke both**: Stage 1's high-shelf makes
`nodeG` small below a few hundred Hz. On the −6 dB sweep OD/Dist H2 ran +16 to +19 dB hot at
100 Hz with its level trend backwards (the pedal's H2 *falls* as it is driven harder).

1. **A `tanh` wash-out only washes out where its input is big.** The mid/high path's `tanh` never
   left its linear region at low frequency, so instead of collapsing the injection grew as
   `nodeG²`. Ablation put **the larger share of the error on this path** (+15.5 dB on its own),
   not the low one. Fixed by high-passing its source — the counterpart the low band's low-pass
   always implied. Corollary: the two paths must not overlap, or the one that cannot wash out in a
   band will dominate it.
2. **A clamped source cannot self-gate its own wash-out, and `clipEnv` cannot rescue it.** The low
   path's source `x` is clamped, so its low-passed square stops growing while the pedal's H2 keeps
   falling. `clipEnv`'s 0.37 V threshold is never reached at low frequency — that IS the shelving
   this path exists to cover — so a wash keyed to it moves H2 by 0.7 dB, i.e. nothing. The low
   band therefore carries its **own** envelope keyed to a low-passed `nodeG`, and the envelope's
   **threshold** is what makes the shape right rather than merely smaller (below it the wash is
   inert, so the quiet levels that already matched stay untouched).

Result over all 44 captures, driven sweeps: H2 rms error OD 10.3 → 6.9 dB, Dist 10.2 → 7.9, and
the systematic bias eliminated (+2.8 → 0.0, +3.0 → +0.2). Odd orders bit-identical, Boost renders
byte-identical, the time-domain null unchanged on every capture.

### Even-series SHAPE — the H2:H4:H6 ratio (v1.4 P3.1, 2026-07-28)

P3 left H4/H6 11–16 dB short and called it out of the mechanism's reach ("a squared source only
makes H2"). **Wrong** — `tanh(s·u)²` is not squaring a sine: tanh already carries 3f/5f, so its
square carries H4/H6 too. What was wrong was the **knee**, and each path was wrong differently:

- **`asymDriveScale` 1.70 → 3.50** (mid/high path). The knee alone sets the ratio between orders:
  for `tanh(a·sin)²`, H4−H2 runs −28 dB at a = 0.5, −13 at 1.7, −6 at 2.5, −2.7 at 4. The pedal
  wants H4 ≈ H2−9 and H6 ≈ H2−12. This constant had never been re-swept since before P2.
- **`asymLowDriveScale` = 4.90** (low path, NEW). Its source is a 150 Hz low-pass of the clip
  output, and the low-pass strips the clipped waveform's own harmonics — so `xLp` is nearly a
  **sine**, and a squared sine is pure H2 with **no H4 at all**. That is why sweeping
  `asymDriveScale` alone never moved the 100/200 Hz anchors. It now runs through the same tanh
  knee, normalised by the mode's clamp (`asymClampOD` 1.64 V / `asymClampDist` 0.584 V) so one
  constant sets the same operating point in both modes and the per-mode coefficients keep carrying
  level only.

**Fit the knee first, then re-zero the bias with the coefficients** — a joint search that scores all
orders together will happily drift H2 several dB hot to buy H4/H6 (it did: +3.7/+5.1). A gain moves
the whole even series together; the knee does not. All 44 captures, driven sweeps, OD/Dist: H4 bias
−11.1/−11.2 → **−1.5/−2.1**, H6 −22.0/−18.2 → **−7.4/−5.8**, H2 bias still **0.0**. Odd orders
0.00 dB changed, Boost byte-identical, null unchanged (worst +0.04 dB), SMPTE IMD unchanged.
H6 remains ~6 dB short — accepted; closing it costs H2 rms faster than it gains H6, and the honest
next step is the asymmetric diode pair (route 3 in `analysis/FR_THD_AUDIT.md` P3.1).

**Still true: do not raise the H2 injection to chase H4/H6** — that re-creates the LF overshoot P3
removed. Raise the knee, then re-level.

## OD clip-depth-gated low-mid restoration (`MonarchChannel::odLowShelf`)

Farina `linear_tf` audit vs the captures (`analysis/mid_eq_audit.py`) found the **Overdrive channel
alone** falls short in the low mids as it is driven HARD: a broad, ~flat shortfall of **~1.8 dB below
~500 Hz** that appears only at high clip depth (≈0 at normal levels, growing to −1.8 dB at the
hottest −6 dB sweep), consistent across every gain. Distortion matches (<0.6 dB) and Boost has a
separate knob-tilt — so it's OD-specific (the soft feedback clipper compresses the low mids more than
the real pedal's). Restored with a **first-order low-shelf on the clip output** (post-clip, so
clipping can't re-compress it; `odShelfMaxDb=2.0` @ `odShelfPivotHz=520`), its contribution BLENDED
IN by `gate = sw1On ? tanh(odGateScale·clipEnv) : 0` — **OD-only** (Boost/Dist stay byte-identical)
and gated by the existing clip-depth envelope so it's inert at normal levels and engages only when
digging in hard. Calibrated (`odGateScale=12`) to roughly halve the hot-drive deficit while keeping
the **time-domain null** neutral at normal levels (worst case ~+0.3 dB at the G10+hot extreme).

> **Metric caveat (load-bearing):** validate a clip-gated correction with the **time-domain null**,
> NOT the swept-sine `linear_tf` — the gate modulates across the sweep, which corrupts the Farina
> deconvolution (it shows a spurious deficit at moderate drive that the null proves isn't real). The
> reverted fixed 335 Hz pre-clip "presence bump" is the cautionary tale (see CLAUDE.md).

The fixed processor-level **capture-match tilt shelf** (`TiltShelf`, PluginProcessor.h) is
**retired** (`kEnabled = false`) and superseded by the drive-dependent correction below — a fixed
shelf cannot match a tilt that reverses sign with drive. Code kept for A/B only.

## Drive-dependent capture-match shelves (`MonarchChannel::updateDriveShelf` / `driveShelf`)

The model-vs-capture EQ error (best-fit-gain-aligned, 40 Hz–16 kHz, every gain/tone) is a clean,
**tone-independent tilt that reverses with drive**: treble-short at low drive, bass-short/treble-hot
at high drive, crossing near G4. (The literal 3-terminal DRIVE wiper-tap was re-derived and shown to
share the 2-terminal model's drive-dependence — the pot's dual action moves Stage 2's flat level,
not Stage 1's tilt — so this is a second-order/capture-chain effect, not a topology fix.) Corrected
with **two drive-scaled first-order shelves on Stage 1's output** (`processPre`, pre-clip so the
clipper sees the corrected spectrum; runs at the oversampled rate with the rest of the channel),
each unity by the G4–G5 crossover:
- **Treble high-shelf** (`shelfPivotHz` 450, `shelfMaxDb`/`shelfSlopeDb`): HF lift that fades OUT
  with drive — restores the Stage-1 HF shelf `Av=1+Z_upper/Z_lower` lets collapse at low drive.
- **Bass boost low-shelf** (`bassPivotHz` 105, `bassOnsetDrive`/`bassBoostSlopeDb`/`bassBoostMaxDb`):
  LF lift that fades IN with drive — counters the documented bass-bloom-under-drive.
- **Bass cut bell** (`bassCutPivotHz` 185, `bassCutQ` 0.45 — a WIDE bell, fades OUT with drive to 0 by
  `bassCutOffDrive`=0.5≈G5; `bassCutSlopeDb`/`bassCutMaxDb`, a peaking biquad `bc*`, 2026-07-04): removes
  the **low-drive low-mid EXCESS** — Boost/Clean ran ~+3 dB too bassy below ~250 Hz at G2 (a broad bump
  spanning 100–330 Hz, so a bell not a shelf — a shelf over-cuts sub-100 and under-cuts the peak; and the
  bell must be WIDE/low-Q to flatten the whole 100–330 span — a narrow bell centred at 160 left a +0.7 dB
  residual at 220–280). Refined 07-05 (160/Q0.7 → 185/Q0.45) to flatten it to ±0.2 dB at G2. OFF by G5 (leaves
  the mid/high-drive voicing untouched). Applies to all modes (pre-clip) but is only audible in Boost —
  OD/Dist clipping masks the excess. Validated: driven-sweep nulls **improve 1–2.8 dB at G2–G4 across all
  three modes**; the only cost is a small clean-sweep (very-quiet, below playing level) regression at
  G2/G3 that leaves them at still-excellent −15 to −18 dB (the excess is level-dependent — bigger at
  playing level than at the near-silent clean sweep — and a knob-keyed cut can't tell them apart).
- **Fixed HF-trim high-shelf** (`hfTrimPivotHz` 4.5k, `hfTrimDb` −1.3, drive-independent, `ht*`,
  2026-07-04): eases the slightly-hot top end so the plugin matches the captures within ~0.3 dB across
  2–4.5 kHz (where the captures are reliable; above that they roll off/alias erratically — 6 kHz shows a
  spurious −15 dB dip — so this is a conservative, by-ear-confirmable cut, NOT fit to those artifacts).
- **Warp high-shelf** (`warpPivotHz` 6.5k / `warpScaleDb`/`warpExp`, rate-scaled `×(48k/rate)^warpExp`,
  capped `warpMaxDb`, then **DC-normalized**): compensates the finite-rate bilinear top-octave droop.
  Recalibrated 06-30 — it was previously self-disabled by 2x (`^4`), which left the live default (2x)
  ~2–3 dB darker on top than the render path (4x/8x); now FIT to the warp-free-baseline-vs-8x deficit
  so **2x and 4x match 8x** through the audible top (DC–8 kHz ≤0.2 dB, 12 kHz ~0.4 dB, only the 16 kHz
  edge ~1.8 dB short at 2x — a first-order shelf can't reach Nyquist without over-brightening the
  6–8 kHz presence band, so the moderate pivot is deliberate). The DC-normalization (divide by H(z=1))
  keeps low/mid at exact unity at every rate — without it the near-Nyquist prewarp droops the whole
  spectrum (several dB at 1x). 1x stays the low-CPU/approximate-top mode (warpMaxDb cap).

All use the prewarped bilinear `shelfCoeffs` helper (a high-shelf sets Glo=1; a low-shelf sets
Ghi=1; Glo=Ghi → exact unity). Result (render/2x+ paths): **50 Hz–16 kHz within ~1.2 dB at all
gain/tone** (worst ~2.3 dB at the tone-down top-octave corner); also *improves* OD/Dist nulls at
mid/high drive. State (`hs*`/`ls*`/`bc*`/`ws*`/`ht*`) resets in `prepareLinear`/`reset`; drive-shelf +
bass-cut-bell coeffs update per block in `setDrive`, the warp + HF-trim shelves in `prepareLinear` (rate-only).

> **Deferred refinement — Red drive-shelf keying (2026-07-05, not yet needed):** these drive-dependent
> shelves are keyed to `drive01` (the raw knob) and were fit to the **Yellow/stock** captures. On **Red**
> (fixed Hi-Gain, floor = R6_floor + DRIVE_max/6 → Red@d ≈ Yellow@(d+1⁄6)), the gain/clipping/harmonics
> track the ACTUAL Stage-1 output (`nodeG`/`clipEnv`), so they correctly behave like Yellow@(d+1⁄6). But
> the EQ-correction shelves, being knob-keyed, apply Yellow@d's curve at Red's knob d — i.e. they do NOT
> shift by 1⁄6 the way the gain does. Effect: at LOW Red drive the bass cut bell over-cuts ~1–2 dB vs a
> gain-matched Yellow. **Potential fix:** on the hiGain channel, key the shelves off an EFFECTIVE drive
> `drive01 + 1⁄6` (clamped) so Red is a fully consistent gain-shifted Yellow in EQ too. Left as-is for now
> because Red has NO NAM reference — neither keying is validated, so it's a voicing choice either way.

---

## Pot Tapers

- **DRIVE 100kB, TONE 25kB, PRESENCE 50kB:** linear. `R = R_max · x`.
- **VOL 100kA:** audio. Wiper fraction `pow(10, 1.8·(x−1))` — noon = −18 dB (fitted to captures;
  ideal-log 2.0/−20 dB was ~2 dB too quiet). Wiper gain is **smoothed (~5 ms one-pole)** so VOLUME
  automation steps don't zipper; input/output trim likewise smoothed in PluginProcessor.
  DRIVE/TONE/PRESENCE are unsmoothed (WDF elements; continuous turns are already click-free).

Never audio-taper DRIVE/TONE/PRESENCE; never linear-taper VOL.

## Component Values

See circuit.md §1 (master table) and §6 (diode params). Stage-1 floors: Yellow R2∥R3 ≈ 990Ω, Red
≈ 17.7k (tamed Hi-Gain = R6_floor + DRIVE_max/6, a voicing choice over the literal R2=100k — Red@d
≈ Yellow@(d+1/6); the `hiGain` ctor flag). Input cap 22n; Z_lower = C4(10n) series [R4(27k) ∥
(R5(33k) + C3(10n))]; Z_upper HF cap C2 = 100pF.

## Signal Calibration

Internal voltages are **real circuit volts**, not normalized — do not normalize to ±1.0 internally,
or the clipping onset/feel is wrong. Host↔circuit scale `circuitVoltsPerFS = 0.87`. Input trim
(±12 dB) absorbs hotter/quieter pickups (positions the clipping); output trim re-matches level.
No tone-shaping in the trims. Chain: input trim → VU → **Red** → **Yellow** → VU → output trim.

## processBlock Structure

```
1. Active OS factor: isNonRealtime() ? oversampling_render : oversampling_realtime.
   If changed → pendingOversamplingFactor, reinit both oversamplers.
2. pendingClippingMode per channel → MonarchChannel::setClippingMode (structural; no matrix swap).
   (Hi-Gain is fixed at construction — no per-block Stage 1 swap.)
3. Read APVTS (cached atomic pointers, once/block). Tapers applied inside each stage.
4. Apply supply voltage + params to both channels. Input trim (× cal, smoothed). Input meters.
5. channelRed.process() first  — bypassed → copy in→out, skip DSP+oversampler; else up→WDF→down.
6. channelYellow.process()      — same.
7. Output trim (÷ cal, smoothed). Output meters.
```

Dual-mono stereo: one `ChannelStrip {MonarchChannel yellow, red}` per audio channel (L/R have
independent WDF state, shared knob settings). Bypass = ~5 ms click-free wet/dry crossfade.
