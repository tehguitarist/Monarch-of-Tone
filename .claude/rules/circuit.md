# Circuit Reference — Monarch of Tone (King of Tone Clone)

Two independent modified-Bluesbreaker overdrive channels in series, each with its own
footswitch. Per channel: input HPF → Stage 1 (non-inverting gain) → DRIVE → Stage 2
(inverting ×−22) → clip switches (SW-1 soft / SW-2 hard) → Tone → Presence → Volume.

**Sources** (cross-referenced; the model follows the **real KOT / Theseus** topology where the
two differ):
- `analysis/KoT_schematic_matsumin` — matsumin Ver2 community schematic (BMP).
- `analysis/KoT_schematic_Theseus.png` + Theseus kit parts list (p29) / IC voltages (p30) —
  authoritative for topology/values; this is the pedal the NAM captures came from.
- CCRMA paper (Kai-Chieh Huang, Stanford) — corroborates the gain-stage / feedback-clipper
  structure; stock circuit only (no Hi-Gain mod).

Component names below are **functional**, not tied to either schematic's numbering (the two
renumber the same parts — e.g. matsumin R10/R11/R12/R13/R14 ≡ Theseus R7/R8/R9/R10/R11). Where
the model's value/topology is a deliberate choice over a source, the decisions list (§2) says so.

---

## 1. Component Master Table

| Function | Value | Stage | Notes / transfer function |
|----------|-------|-------|---------------------------|
| Input pulldown | 1M | Input | node_IN→GND; bypass-thump suppression, negligible at audio |
| Input coupling cap | **22nF** | Input | Vin→pin3(+); DC block, HPF ≈7 Hz w/ 1M bias R. (matsumin = 10n; model uses Theseus 22n — both sub-audio) |
| Pin3 bias R | 1M | Input | pin3(+)→BIAS; DC bias / return for the coupling cap |
| Stage 1 Z_lower | C4=10n, R4=27k, R5=33k, C3=10n | Stage 1 | NodeF→GND = **C4 series [ R4 ∥ (R5 + C3) ]** (Theseus topology — single shared cap to GND). DC gain = 1 |
| Stage 1 Z_upper HF cap | 100pF | Stage 1 | NodeF↔NodeG, ∥ (floor+DRIVE) |
| Stage 1 feedback floor | Yellow **R2∥R3 ≈ 990 Ω**, Red **≈ 34.3 k** (tamed Hi-Gain) | Stage 1 | Stock (Yellow) = R2∥R3. Hi-Gain (Red) floor is a VOICING choice = R6_floor + DRIVE_max/3 (not the literal R2=100k — that was too hot); shifts Red's curve +⅓ knob so 9:00≈noon. Floor + DRIVE = Z_upper resistive leg |
| DRIVE pot | 100kB linear | Stage 1 | 2-terminal rheostat in Z_upper; floor+DRIVE = 0.99k–100k (Yellow) / 34.3k–134k (Red) |
| Stage 2 input coupling | 100nF | Stage 2 | HPF f_c = 159 Hz w/ R9 |
| Stage 2 input R (R9) | 10k | Stage 2 | gain = −R10/R9 = −22 |
| Stage 2 feedback R (R10) | 220k | Stage 2 | always present |
| Soft-clip diodes (SW-1) | MA856 ×4 | SW-1 | `[D4+D5] ∥ [D2+D3]` back-to-back series strings ≡ **1× DiodePairT, n_eff = 2·n_MA856 ≈ 3.024**, Vf≈1.64V |
| SW-1 branch series R (R11) | 6.8k | SW-1 | series w/ diode network; branch ∥ R10, gated by SW-1 |
| Hard-clip diodes (SW-2) | 1S1588 ×2 | SW-2 | true antiparallel pair, **1× DiodePairT**; shunt node_HC→BIAS; Vf≈0.584V |
| Stage 2 output R (R12) | 1k | SW-2/Tone | always in path: pin7 → R12 → node_HC → TONE top terminal |
| TONE pot | 25kB linear | Tone | 3-terminal tap (R-type adaptor at wiper): top=node_HC, bottom→C8→BIAS, wiper→R13 |
| Tone cut cap (C8) | 10nF | Tone | TONE bottom terminal → BIAS (treble cut) |
| Presence R (R13) | 6.8k | Tone | TONE wiper → node_T_out |
| Presence trim | 50kB linear | Tone | node_T_out → Trim → C9 → BIAS; 2-terminal rheostat; default fully CCW |
| Presence cap (C9) | 10nF | Tone | Trim → C9 → BIAS |
| VOL pot | 100kA audio | Volume | top=node_T_out, bottom=BIAS, wiper→C11 |
| Output coupling (C11) | 1µF | Volume | HPF f_c ≈ 0.16 Hz w/ R14 (DC block) |
| Output bleed (R14) | 1M | Volume | output → GND |
| Op-amp | JRC4580D | both | matsumin label "JRC4558D" is wrong. Modelled as ideal op-amp + rail saturation |

---

## 2. Key Decisions & Rationale

- **Input cap 22nF (Theseus), not 10nF (matsumin).** The model follows the Theseus circuit (the
  captured pedal). Difference is sub-audio either way (HPF ≈7 vs 16 Hz into the 1M bias R).
- **Op-amp = JRC4580D**, modelled ideal. The matsumin "JRC4558D" label is wrong; real KOT and
  Theseus both use 4580D. An ideal-op-amp model doesn't capture the difference (noise, slew).
- **Soft-clip diodes MA856** (Vf≈0.82V single). The matsumin 1N914+1N4004 pairing is a DIY
  substitution artifact, not the original — disregard. BAS33 (Theseus) is a close cross-check.
- **Hard-clip diodes 1S1588 = 1N914 = 1N4148** — identical Shockley parameters.
- **Stage-1 floor: Yellow R2∥R3 ≈ 990Ω, Red ≈ 34.3k (tamed Hi-Gain).** Stock (Yellow) runs the low
  ~1k floor → nearly-clean minimum. The literal Theseus Hi-Gain mod opens SW1B → floor = R2 = 100k,
  but that was measured as too hot (its minimum already very driven). With no Red NAM captures to
  constrain it, the Red floor is set as a VOICING choice to `R6_floor + DRIVE_max/3 ≈ 34.3k`, which
  shifts Red's whole DRIVE curve up by exactly one-third of the knob sweep so "9 o'clock acts like
  noon" (and noon like 3 o'clock) holds precisely — see `tests/Stage1_HiGain.cpp` (Δ ≈ 0 dB).
- **Tapers (parts-list confirmed):** DRIVE 100kB linear, TONE 25kB linear, Presence 50kB linear,
  VOL 100kA audio. Only VOL is audio taper. The VOL audio fraction is `pow(10, 1.8·(x−1))` (noon
  = −18 dB) — fitted to the captures (the ideal-log 2.0/−20 dB was ~2 dB too quiet at noon).
- **Calibration:** internal WDF voltages are **real circuit volts** (not normalized ±1.0) so the
  diode thresholds and op-amp rails are hit at the correct absolute levels. Host↔circuit scale
  `circuitVoltsPerFS = 0.87` (PluginProcessor.h), fitted to the captures' clean/Boost rail-clip
  onset.

---

## 3. Power, Bias, and Op-amp Rails

Single **9V** supply (verified both schematics + Theseus measured pins: V+ ≈9.15V, V−=0, bias
≈4.5V). **No charge pump / voltage doubler anywhere.** BIAS = VCC/2 = 4.5V = DSP signal ground
(0V in the bipolar model). All supply/bias components excluded from the WDF model.

**Op-amp output ceiling ≈ ±3.3V around bias** (JRC4580 saturates ~1.3–1.5V from each rail; soft,
gradual knee — not a hard comparator). This matters because:
- **Boost mode (SW-1/SW-2 both OFF) has no diodes — the rails are the ONLY nonlinearity.** Without
  modelling them, Boost would be an unphysical infinite-clean boost. So a soft rail saturation at
  ±3.3V is required (see dsp.md "Op-amp Rail Saturation"; ADAA'd).
- **OD / Distortion** clamp at the diodes (≈±1.64V / ≈±0.584V), far below ±3.3V, so the rail
  saturation **never engages → no tone change** in diode modes. That's the guarantee it's tone-safe.
- The **supply-voltage mod** (9/12/18V) scales only this ceiling (+0.5V swing per +1V supply);
  diode thresholds are junction-fixed and do not move (the real "18V mod" → more clean headroom).

---

## 4. Per-Channel Signal Flow (node-by-node)

```
IN ─┬─ R5(1M) → GND                      [input pulldown]
    └─ C_in(22n) → pin3(+) ─ R_bias(1M) → BIAS    [DC block + bias]

IC_A (non-inverting, no PolarityInverterT): pin3(+)=in, pin2(−)=NodeF, pin1=NodeG=out
  Z_lower (NodeF→GND): C4(10n) series [ R4(27k) ∥ (R5(33k)+C3(10n)) ]
  Z_upper (NodeF→NodeG): (floor + DRIVE 0–100k) ∥ C2(100pF)   [floor = 990Ω Yellow / 34.3k Red (tamed Hi-Gain)]
  Av(s) = 1 + Z_upper/Z_lower ;  DC gain = 1

NodeG → R9(10k) → C7(100n) [HPF 159 Hz] → IC_B pin6(−)

IC_B (inverting ×−22, PolarityInverterT equivalent via VCVS): pin5(+)=BIAS, pin7=out
  feedback: R10(220k) always; SW-1 ON adds R11(6.8k)+MA856 network in parallel with R10
  │
  pin7 → R12(1k) [always] → node_HC
           └─ SW-2 ON: 1S1588 antiparallel pair shunts node_HC → BIAS

node_HC → TONE(25kB) top; wiper → R13(6.8k) → node_T_out; bottom → C8(10n) → BIAS
node_T_out ─┬─ Presence trim(0–50k) → C9(10n) → BIAS
            └─ VOL(100kA) top; wiper → C11(1µF) → OUT ; R14(1M) bleed → GND
```

**Clipping modes** gate SW-1/SW-2 only (Hi-Gain is fixed per channel, not a runtime axis):

| Value | Mode | SW-1 | SW-2 | Clamp | Character |
|-------|------|------|------|-------|-----------|
| 0 | Boost | OFF | OFF | op-amp rails ±3.3V | Clean boost (rails clip at high gain) |
| 1 | Overdrive *(default)* | ON | OFF | ±1.64V | Soft, touch-sensitive |
| 2 | Distortion | OFF | ON | ±0.584V | Hard, RAT-style |

(The "Both" SW-1+SW-2 stacked mode was dropped for the 3-position toggle; `processClip` still
handles any SW combo, so re-adding is a 1-line change.)

---

## 5. WDF Modelling Notes (topology → adaptor)

- **Input network:** simple series/shunt tree (pin3 is the non-inverting input, draws no current).
- **Stage 1:** NO R-type matrix. For an ideal op-amp Z_lower/Z_upper decouple, so it solves two
  one-ports — drive Z_lower with a V-source = V(pin3+) → read current i; drive Z_upper with an
  I-source = i → read voltage; V(NodeG) = V(pin3+) + i·Z_upper. Read off **passive** ports only.
- **Stage 2:** root R-type adaptor; ideal op-amp VCVS closes the loop and carries the inversion
  (in+ = BIAS, in− = pin6−). Output read off the passive R10 port. No separate PolarityInverterT.
- **SW-1 soft clip:** current-source / diode-root — the op-amp virtual ground forces a known
  i_in = Vin/Z_in into `R10 ∥ [R11 + DiodePairT]` (diode = nonlinear root). SW-1 OFF → V(pin7) =
  −i_in·R10 = −22·Vin. Switch is structural (include/bypass the diode path), not a matrix swap.
- **SW-2 hard clip:** R12(1k) always in series from pin7; `DiodePairT` shunt at node_HC → BIAS.
- **Tone:** 3-port R-type (parallel) adaptor at the TONE wiper — port0 R_a (toward node_HC), port1
  R_b+C8 (toward BIAS), port2 R13 (toward node_T_out), with R_a+R_b = 25k linear. node_T_out also
  carries the Presence trim+C9 branch and the VOL pot body (100k) as linear loads.
- **Switch changes:** never reconstruct the tree at runtime — linear stages use precomputed
  scattering matrices via `setSMatrixData()`; the nonlinear clip switches are structural.

---

## 6. Diode Parameters

```cpp
// MA856 — SW-1 soft clip. Network [D4+D5]∥[D2+D3]: two series diodes ≡ one diode with n doubled.
constexpr double Is_MA856    = 7.74e-13;   // Vf ≈ 0.820V @ 1mA
constexpr double n_MA856     = 1.512;
constexpr double Vt          = 25.85e-3;
constexpr double n_eff_MA856 = 2.0 * n_MA856; // ≈ 3.024 → pass as DiodePairT's nDiodes arg
// → ONE DiodePairT(Is_MA856, Vt, n_eff_MA856) in series with R11(6.8k), branch ∥ R10(220k).

// 1S1588 = 1N914 = 1N4148 — SW-2 hard clip. ONE DiodePairT.
constexpr double Is_1S1588 = 2.52e-9;   // Vf ≈ 0.584V @ 1mA
constexpr double n_1S1588  = 1.752;
```

Both clipping stages are **symmetric** matched pairs — use `DiodePairT` only, never two `DiodeT`.

---

## 7. Known Model Simplifications (accepted)

- **DRIVE pot modelled as a 2-terminal rheostat** inside Z_upper, not its literal 3-terminal
  wiring (wiper=output, pin3→R6→Stage2). Tested: the literal 3-terminal model over-swings Stage-2
  gain (≈28 dB total vs the measured ~10.6 dB), so the 2-terminal approximation matches the
  captures *better*. Kept.
- **High-drive gain residual** vs the real pedal (real rises ~2–3 dB more at G8–G10, with a
  bass-bloom-under-drive) is accepted device-physics / capture variance, not a topology error —
  every Stage-1 value + topology was re-traced exact against the Theseus schematic. The model has
  the effect but under-drives it; boosting the plugin input reproduces the bloom exactly.
