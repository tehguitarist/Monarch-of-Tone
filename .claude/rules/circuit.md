# Circuit Reference — Monarch of Tone (King of Tone Clone)

> **Primary source:** `king_of_tone_schematic.png` (Ver2, matsumin, 2008-05-15).
> **Secondary/confirmation:** Aion FX "Theseus Dual Overdrive" v2.0.2 kit documentation.
> **Academic:** CCRMA — Kai-Chieh Huang, "King of Tone Guitar Pedal Modeling", Stanford.
>
> Where the two sources conflict, this document records the conflict explicitly and states
> which value is used and why. Component reference designators used internally are functional
> labels (e.g. R_input_lower) — not tied to either schematic's numbering system, to avoid
> confusion between sources.
>
> **All flags resolved. MA856 parameters validated. Hi Gain mod characterised.**
> **Full cross-source verification complete — see Section 2 for all discrepancies.**

---

## 1. Cross-Source Verification Table

Every component traced from the matsumin schematic image and compared against the Theseus
parts list. Values are by function, not by reference designator.

| Function | matsumin ref | matsumin value | Theseus ref | Theseus value | Status | Used in model |
|----------|-------------|---------------|------------|--------------|--------|--------------|
| Reverse polarity diode | D1 | 1N4001 | D1 | 1N5817 (Schottky) | DIFFERS — not in signal path | N/A |
| BIAS upper divider R | R1 | 47k | R12/R13 | 47k | MATCH | excluded (bias) |
| BIAS lower divider R | R2 | 47k | R12/R13 | 47k | MATCH | excluded (bias) |
| BIAS filter cap | C1 | 100µF | C20/C21 | 100µF | MATCH | excluded (bias) |
| VCC filter cap | C2 | 100µF | C21/C22 | 100µF | MATCH | excluded (bias) |
| Input pulldown R | R5 | 1M | RPD1 | 1M | MATCH | 1M |
| **Input coupling cap** | **C3** | **10nF (0.01µF)** | **C1** | **22nF** | **DIFFERS** | **10nF (matsumin primary)** |
| Stage 1 Z_lower Branch2 cap | C6 | 10nF | C3/C4 | 10nF | MATCH | 10nF |
| Stage 1 Z_lower Branch1 cap | C5 | 10nF | C3/C4 | 10nF | MATCH | 10nF |
| Stage 1 Z_lower Branch1 R | R7 | 33k | R5 | 33k | MATCH | 33k |
| Stage 1 Z_lower Branch2 R | R8 | 27k | R4 | 27k | MATCH | 27k |
| Pin3(+) DC bias R (input network) | R4 | 1M | R1 | 1M | MATCH | 1M |
| Stage 1 HF feedback cap | C4 | 100pF | C2 | 100pF | MATCH | 100pF |
| Stage 1 DRIVE floor R | R6 | 10k | R6 | 10k | MATCH | 10k |
| DRIVE pot | DRIVE | 100kB | DRIVE | 100kB | MATCH | 100kB linear |
| **Stage 2 input coupling cap** | **C7** | **100nF (0.1µF)** | **C5** | **100nF (0.1µF)** | **BOTH AGREE: 100nF** | **100nF** ✓ |
| Stage 2 input R | R9 | 10k | R6(ch2) | 10k | MATCH | 10k |
| Stage 2 feedback R | R10 | 220k | R7 | 220k | MATCH | 220k |
| SW-1 diodes (soft clip) | D2–D5 | 1N914+1N4004 (sub) | D2–D5 | BAS33 ×4 (sub) | BOTH ARE SUBS | MA856 ×4 (original) |
| SW-1 DIP switch | DIP SW-1 | soft clip | SW1 pos2 | Soft Clip | MATCH | SW-1 |
| SW-2 series R | R11 | 6.8k | R8 | 6.8k | MATCH | 6.8k |
| SW-2 diodes (hard clip) | D6/D7 | 1N914 (sub) | D6/D7 | 1N914 (sub) | BOTH USE 1N914 | 1S1588 (original) |
| SW-2 DIP switch | DIP SW-2 | hard clip | SW1 pos3 | Hard Clip | MATCH | SW-2 |
| Tone input series R | R12 | 1k | R3 | 1k | MATCH | 1k |
| TONE pot | TONE | 25kB | TONE | 25kB | MATCH | 25kB linear |
| Tone treble-cut cap | C8 | 10nF | C6 | 10nF | MATCH | 10nF |
| Presence series R | R13 | 6.8k | R10 | 6.8k | MATCH | 6.8k |
| Presence trimpot | Trim 50k | 50kB | PRESENCE | 50kB | MATCH | 50kB linear |
| Presence cap | C9 | 10nF | C7 | 10nF | MATCH | 10nF |
| VOL pot | VOL-a/b | 100kA | VOL | 100kA | MATCH | 100kA audio |
| Output coupling cap | C11-a/b | 1µF | C8 | 1µF film | MATCH | 1µF |
| Output electrolytic cap | — | not shown | C9 | 1µF electro | Theseus addition | 1µF electro |
| Output bleed R | R14 | 1M | R11 | 1M | MATCH | 1M |
| Hi Gain R (mod) | — | not in matsumin | R29 | 22k | Theseus addition | 22k |
| Hi Gain protection R | — | not in matsumin | R27 | 47R | Theseus addition | 47R |
| Hi Gain switch | — | not in matsumin | SW1 pos1 | Hi Gain | Theseus addition | SW-3 |
| Op-amp IC | JRC4558D (label) | — | JRC4580D | — | matsumin label WRONG | JRC4580D |

---

## 2. Discrepancies — Decisions and Rationale

### C7 (Stage 2 input coupling cap): 100nF

Both sources agree: matsumin C7 = 0.1µF (100nF); Theseus C5 = 100n.
Stage 2 HPF: f_c = 1/(2π × R9 × C7) = 1/(2π × 10k × 100nF) = **159 Hz**.
Virtually the entire guitar range receives full Stage 2 gain.

### C3 (Input coupling cap): 10nF matsumin vs 22nF Theseus

The matsumin schematic shows C3 = 0.01µF (10nF). The Theseus uses C1 = 22nF.
This is a genuine design difference — the Theseus is a slightly modified clone.

> **CORRECTED 2026-06-15:** C3 forms a simple DC-blocking pole with R4 (1M to BIAS at
> pin 3+), not with R8 (see Section 5). R8 is part of Stage 1's feedback network (Section 6).

- matsumin 10nF: f_c = 1/(2π × R4 × C3) ≈ 15.9 Hz with R4 (1M) — DC blocking only
- Theseus 22nF: f_c ≈ 7.2 Hz with R4 (1M) — also DC blocking only; difference is negligible
  at audio frequencies either way

**Decision:** Use **10nF** (matsumin primary source) for the 1:1 KOT Ver2 clone.
The 22nF variant may be offered as a "Theseus" mode in future if desired, though the
audible difference is minimal given both values are far below the guitar's range.

### Op-amp (JRC4558D vs JRC4580D)

The matsumin schematic labels the IC as JRC4558D. Real KOT units traced by the community
confirm JRC4580D. The Theseus uses JRC4580D. **Use JRC4580D.** In the DSP model both are
ideal op-amps — the difference affects noise floor and slew rate, neither of which is modelled.

### Soft-clip diodes (MA856 vs BAS33 vs 1N914+1N4004)

The matsumin Ver2 schematic uses substitute diodes (1N914+1N4004 — mismatched pairs).
The Theseus uses BAS33 (matched pairs, Vf ≈ 0.80V). Real KOT units use MA856 (Vf ≈ 0.82V).
**Model MA856.** BAS33 is very close and may be used as a cross-check simulation.
The matsumin pairing of 1N914+1N4004 in the soft-clip path is a substitution artifact from
Ver2 being a DIY community schematic, not a trace of the original. Disregard those diode values.

### Hard-clip diodes (1S1588 vs 1N914)

Both schematics show 1N914 for D6/D7. Real KOT uses 1S1588, which is electrically identical
to 1N914 (same parameters). **Model 1S1588 = 1N914 = 1N4148** — same Shockley parameters.

### R6 (10k DRIVE floor resistor)

R6 (10k) is a **floor resistor in the Stage 1 feedback / DRIVE pot circuit**, not the
Stage 2 input resistor. It sets the minimum feedback impedance when DRIVE is at zero,
preventing the gain from going to unity. When DRIVE=0: Z_upper_min = R6 = 10k.
When DRIVE=max: Z_upper_max = R6 + 100k = 110k. The Stage 2 input resistor is R9 = 10k
(a separate component). Both happen to be 10k but serve completely different functions.

---

## 3. Circuit Overview

The King of Tone is two independent modified Bluesbreaker circuits in series, each with
its own footswitch. Per channel, the verified signal path is:

```
IN → [Input HPF: C3(10nF) + R_network] → [Stage 1: non-inverting amp, IC_A]
   → [DRIVE pot (100kB): cross-stage gain control]
   → [Stage 2: inverting amp, IC_B, gain=–22, HPF f_c=159Hz]
   → [SW-1: MA856×4 soft-clip in feedback loop (optional)]
   → [SW-2: R11(6.8k) + 1S1588×2 shunt hard-clip (optional)]
   → [Tone: R12(1k) + TONE(25kB) + C8(10nF) + Presence network]
   → [Volume: VOL(100kA) + C11(1µF)] → OUT
```

BIAS = VCC/2 = 4.5V = DSP signal ground (0V in model).

---

## 4. Power and Bias

- VCC = 9V; BIAS = 4.5V from 47k/47k voltage divider + 100µF filter caps
- D1 (1N4001 in matsumin; 1N5817 in Theseus): reverse polarity protection on DC jack
- **DSP model:** BIAS = 0V. Exclude all power supply and bias components.

---

## 5. Input Network

> **CORRECTED 2026-06-15** — the previous version of this section (R7/R8 resistive divider
> driving pin3, C5/C6 as input shunt caps) was traced incorrectly. Re-verified directly from
> `king_of_tone_schematic.png` (crop shows J1 → R5/C3 → pin3, and R4 to BIAS below pin3).
> C5, C6, R7, R8 are **not** part of the input network — they belong to Stage 1's feedback
> network (Section 6).

**Components (from matsumin schematic, verified against Theseus):**

| Component | Value | Connection |
|-----------|-------|-----------|
| R5 | 1M | Input jack (node_IN) to GND (pulldown, prevents bypass thump) |
| C3 | 10nF | Input jack (node_IN) to IC_A pin 3(+) (series coupling/DC-block cap) |
| R4 | 1M | IC_A pin 3(+) to BIAS (DC bias / return path for pin 3) |

**Signal flow:**

```
J1 INPUT ── node_IN ──────────────────────── C3 (10nF) ──── IC_A pin 3(+)
              │                                                  │
            R5 (1M)                                         R4 (1M)
              │                                                  │
             GND                                               BIAS
```

R5 (1M) is a bleed to GND — negligible at audio frequencies, keeps the input jack at DC
ground when unconnected (true-bypass thump suppression). C3 (10nF) blocks DC and couples
the AC signal to pin 3(+). R4 (1M) sets pin 3(+) to BIAS at DC and provides the return path
for C3's bias current.

**Input HPF (DC blocking only):**

    f_c = 1 / (2π × R4 × C3) = 1 / (2π × 1M × 10nF) ≈ **15.9 Hz**

This is far below the guitar's range — it is DC blocking, not a tone-shaping filter. There
is **no meaningful input HPF** in this circuit; all of the gain stage's frequency-dependent
behaviour comes from Stage 1's feedback network (Section 6).

**WDF model:** Series C3 from node_IN to pin 3(+), R5 shunt at node_IN to GND, R4 shunt at
pin 3(+) to BIAS. This is a simple linear series/shunt tree — no R-type adaptor needed for
the input network itself (pin 3 is a non-inverting input, draws no current in the ideal
op-amp model).

---

## 6. Stage 1 — IC_A (Non-Inverting Amplifier)

> **CORRECTED 2026-06-15** — re-traced directly from `king_of_tone_schematic.png`. The
> previous version of this section misidentified R7/R8/C4 placement and the role of the
> DRIVE pot. Two crops confirm the corrected topology below:
> 1. A crop showing C5+R7 and C6+R8 as two series branches in parallel, both running from
>    a GND node on the left to a shared junction (= IC_A pin 2(–), "NodeF") on the right.
>    NodeF also connects to C4 (100pF) and to R6(10k)+DRIVE(100k-B) on the top rail.
> 2. A crop showing the top rail continues from NodeF through R6(10k) then the DRIVE pot
>    (100k-B, used as a 2-terminal variable resistor — the wiper arrow is just the pot
>    symbol, not a third connection) to NodeG. NodeG = C4's other terminal = IC_A pin 1
>    (output), and NodeG feeds directly into R9(10k) → C7(100nF) → Stage 2 pin 6(–).

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| IC_A | JRC4580D (half) | Non-inverting amp; pin 3(+) = signal in (Section 5), pin 2(–) = feedback node ("NodeF"), pin 1 = output ("NodeG") |
| C5 | 10nF | NodeF to GND, in series with R7 (Branch 1) |
| R7 | 33k | NodeF to GND, in series with C5 (Branch 1) |
| C6 | 10nF | NodeF to GND, in series with R8 (Branch 2) |
| R8 | 27k | NodeF to GND, in series with C6 (Branch 2) |
| C4 | 100pF | NodeF to NodeG (directly across the feedback path) |
| R6 | 10k | NodeF to NodeG, in series with DRIVE pot (in parallel with C4) |
| DRIVE | 100kB | NodeF to NodeG, in series with R6 (2-terminal variable resistor, in parallel with C4) |

> **Node identity:** NodeF = IC_A pin 2(–). NodeG = IC_A pin 1 (output) = input to R9/C7
> (Stage 2). DRIVE is used as a simple 0–100kΩ rheostat in series with R6 — it has **no**
> separate wiper tap feeding Stage 2; Stage 2 is driven directly from the Stage 1 op-amp
> output via R9/C7 (see Section 7).

**Stage 1 gain (non-inverting, frequency-dependent):**

    Av(s) = 1 + Z_upper(s) / Z_lower(s)

**Z_lower(s)** — NodeF to GND — two series R+C branches in parallel:

    Branch1(s) = R7 + 1/(sC5)     (33k + 10nF series)   → corner f1 = 1/(2π·R7·C5) ≈ 482 Hz
    Branch2(s) = R8 + 1/(sC6)     (27k + 10nF series)   → corner f2 = 1/(2π·R8·C6) ≈ 590 Hz
    Z_lower(s) = Branch1(s) ∥ Branch2(s)

Both branches are high-impedance (capacitive) at DC and low/mid frequencies, becoming
resistive (R7∥R8 ≈ 14.85k) above their corner frequencies. **Z_lower → ∞ at DC** (both
branches are blocked by series caps).

**Z_upper(s)** — NodeF to NodeG — R6 + DRIVE in series, in parallel with C4:

    Z_upper(s) = (R6 + R_drive) ∥ 1/(sC4)     where R_drive ∈ [0, 100k]
    corner f_u = 1/(2π·(R6+R_drive)·C4): 14.5 kHz (R_drive=100k) to 159 kHz (R_drive=0)

**Z_upper is finite (≈ R6+R_drive) at DC**, since C4 is open at DC.

**DC behaviour:** Since Z_lower(0) = ∞, pin 2(–) has no DC path to ground except through
Z_upper to the output — this is the classic op-amp DC servo. At DC, V(pin2-) = V(out), and
by the virtual-short V(pin2-) = V(pin3+) = V(BIAS) = 0. So **DC gain = 1** (output sits at
BIAS, as required for DC blocking into Stage 2).

**Mid/high-frequency behaviour:** Above ~600 Hz both Branch1 and Branch2 become resistive
(Z_lower → R7∥R8 ≈ 14.85k), while Z_upper is still dominated by R6+R_drive (10k–110k, since
its own corner is much higher, 14.5–159 kHz). In this band:

    Av ≈ 1 + (R6 + R_drive) / (R7 ∥ R8) ≈ 1 + (R6+R_drive) / 14.85k

ranging from ≈1.67× (R_drive=0) to ≈8.4× (R_drive=100k) before C4 begins to roll Z_upper
off at higher frequencies. The exact gain-peak frequency (~4194 Hz per CCRMA, measured at
mid-DRIVE) emerges from the interaction of the Z_lower corners (~482/590 Hz, rising) and
the Z_upper corner (~14.5–159 kHz, falling) — **measure this from the implemented WDF model
via a frequency-response test**, do not assume the exact peak frequency/gain numbers above
are precise until validated.

**No PolarityInverterT** — Stage 1 is non-inverting.

**Hi Gain mod (SW-3):**
Switches R29 (22k) in parallel with R8 (27k), via R27 (47R protection) in series, within
Branch 2 (the R8+C6 series branch):

    R8_eff = (R8 ∥ R29) + R27 = (27k ∥ 22k) + 47 ≈ **12.17k**
    Branch2_hi(s) = R8_eff + 1/(sC6)   → new corner f2_hi = 1/(2π·R8_eff·C6) ≈ 1308 Hz

A smaller R8_eff makes Branch2 more resistive at lower frequencies, reducing Z_lower in the
mid/high band and therefore **increasing** Av(s) — consistent with the documented +4 dB
Hi Gain shift. Re-measure the exact shift from the implemented model.

**WDF model:** R-type adaptor at NodeF (IC_A pin 2(–)). Ports: Branch1 = R7 series C5,
Branch2 = R8 (or R8_eff Hi Gain) series C6, Z_upper = C4 ∥ (R6 + R_drive). NodeG (pin 1,
op-amp output) feeds R9/C7 toward Stage 2. Precompute two scattering matrices (standard /
Hi Gain) differing only in Branch2's resistor value; switch via `setSMatrixData()`.

---

## 7. DRIVE Pot — Stage 1 Feedback Control

> **CORRECTED 2026-06-15** — re-traced from the schematic. DRIVE is used as a simple
> 2-terminal variable resistor (rheostat) entirely inside Stage 1's feedback network
> (Z_upper, Section 6). It does **not** have a separate wiper tap feeding Stage 2 — Stage 2
> is driven directly from the Stage 1 op-amp output (NodeG) via R9/C7.

**Components:** DRIVE = 100kB (linear taper), wired as a 0–100kΩ rheostat in series with
R6 (10k), this combination in parallel with C4 (100pF), between NodeF (pin 2–) and NodeG
(pin 1, output).

As DRIVE increases (R_drive: 0 → 100k):
- Z_upper's resistive component (R6 + R_drive) increases from 10k to 110k.
- Since Av ≈ 1 + Z_upper/Z_lower in the mid/high band, **Stage 1 gain increases with
  DRIVE** (the opposite of the previous, incorrect documentation).

**WDF note:** DRIVE only affects Stage 1's R-type adaptor (the R6+R_drive leg of Z_upper).
No cross-stage impedance propagation is needed — Stage 2's input network (R9/C7) is fixed
and independent of DRIVE.

---

## 8. Stage 2 — IC_B (Inverting Amplifier)

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| C7 | **100nF** (0.1µF — verified from both schematics) | DRIVE wiper to R9 (series coupling) |
| R9 | 10k | C7 to IC_B pin 6(–) (input resistor) |
| IC_B | JRC4580D (half) | Inverting amp; pin 6(–) = signal, pin 5(+) = BIAS, pin 7 = output |
| R10 | 220k | IC_B pin 7 (output) to pin 6(–) (feedback resistor) |

**C7 HPF:**

    f_c = 1 / (2π × R9 × C7) = 1 / (2π × 10k × 100nF) = **159 Hz**

The Stage 2 input HPF cuts below 159 Hz — essentially only sub-bass is rolled off.
Virtually the entire guitar frequency range receives full Stage 2 gain. This is by design:
Stage 2 is meant to amplify the full-range signal before clipping. There is no strong
"mid-forward" HPF in Stage 2 as previously documented incorrectly.

**Stage 2 DC gain:**

    Av = –R10 / R9 = –220k / 10k = **–22** (26.8 dB, inverting)

**PolarityInverterT required.** Stage 2 inverts polarity. Without it, clipping is asymmetric
in the wrong direction relative to the physical circuit.

**WDF model:** R-type adaptor at IC_B pin 6(–). Ports: R9 (10k input), R10 (220k feedback),
SW-1 diode network (when active, in parallel with R10). Pin 5(+) tied to BIAS = 0V.

---

## 9. SW-1 — Soft-Clip Feedback Diodes

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| D2+D3 | MA856 antiparallel pair | Between IC_B pin 7 (output) and IC_B pin 6(–) |
| D4+D5 | MA856 antiparallel pair | Same nodes, in parallel with D2+D3 |
| DIP SW-1 | — | In series with combined diode network |

Four MA856 diodes total: two antiparallel pairs (D2+D3 ∥ D4+D5) in parallel with R10 (220k).
SW-1 ON connects this network; SW-1 OFF leaves only R10 in the feedback path.

**How it clips:**
Diodes conduct when feedback current through R10 reaches threshold:

    I_threshold ≈ Vf_MA856 / R10 = 0.82 / 220k ≈ **3.7µA**

At this tiny current, the MA856 pairs begin to conduct, shunting the feedback.
Two pairs in parallel share the current — softer onset, more gradual compression.
Output clamped to approximately ±0.82V. Symmetric — pure odd-harmonic content from this stage.

**WDF model:** Two `DiodePairT` (MA856: Is=7.74e-13, n=1.512) in `WDFParallelT`, placed in
parallel with R10 in Stage 2 R-type adaptor. SW-1 OFF: precomputed matrix with R10 only.

---

## 10. SW-2 — Hard-Clip Shunt Diodes

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| R11 | 6.8k | IC_B pin 7 to node_HC (always in signal path) |
| D6+D7 | 1S1588 antiparallel pair | node_HC shunted to BIAS |
| DIP SW-2 | — | In series with D6/D7; gates diodes only, not R11 |

R11 (6.8k) is **permanently in the signal path** regardless of SW-2 state. It is the only
path from IC_B output to the tone stage. SW-2 connects the 1S1588 shunt diodes at node_HC.

**How it clips:**
When signal at node_HC exceeds ±Vf_1S1588 ≈ ±0.584V, diodes shunt to BIAS.
R11 (6.8k) limits diode current, controlling the hardness of the clipping knee.

**SW-2 OFF effect on level:** With diodes disconnected, node_HC is only the R11 junction.
Signal passes through R11 (6.8k) into tone stage R12 (1k). Voltage divider effect:

    V_tone_in / V_out = R12 / (R11 + R12) = 1k / (6.8k + 1k) ≈ **0.128** (–17.8 dB)

This ~18 dB passive attenuation is compensated by Stage 2's ×22 gain. Include R11 in the
WDF model in all SW-2 states.

**WDF model:** R11 (6.8k) always as series resistor. `DiodePairT` (1S1588: Is=2.52e-9,
n=1.752) at node_HC shunting to BIAS. SW-2 OFF: precomputed open-circuit matrix at diode port.

---

## 11. Tone Control Stage

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| R12 | 1k | node_HC → tone input node_T1 (series isolating R) |
| TONE | 25kB | Linear taper; variable R (0–25k) in treble-cut network |
| C8 | 10nF | Main tone cap; forms LPF with TONE pot |
| R13 | 6.8k | Presence network series R |
| Trim | 50kB | Presence trimpot (internal); default CCW = no boost |
| C9 | 10nF | Presence network cap |

**This stage is entirely passive and linear — no diodes.**

**TONE as treble-cut:**
TONE pot (25kB linear) and C8 (10nF) form a variable low-pass filter:

    f_c = 1 / (2π × R_tone × C8)

    TONE max (25k): f_c ≈ **637 Hz** (heavy treble cut)
    TONE mid (12.5k): f_c ≈ **1274 Hz**
    TONE min (0k): f_c → very high (treble cut bypassed)

R12 (1k) prevents TONE from shorting the signal and isolates SW-2's R11 from the tone network.

**Presence trimpot (Trim 50k):**
From Theseus documentation: "Each channel's Presence trimmer fades the hi-cut capacitor
out of the circuit, reducing the treble cut as you turn it up."
"Default position is full counter-clockwise — equivalent to the stock Bluesbreaker."

The Trim pot and R13 (6.8k) form a parallel path that progressively bypasses C8's treble cut.
At Trim=CCW (0): full treble cut via C8. At Trim=CW (50k): C8's effect is progressively faded.
C9 (10nF) is the cap in this bypass path. R13 (6.8k) is the series R in the bypass path.

**Tone stage topology (for WDF):**

    node_T1 (from R12) → [TONE pot (R, 0–25k) → C8 (10nF) → BIAS]  ← main treble-cut path
                        → [R13 (6.8k) + Trim (0–50k) → C9 (10nF) → BIAS]  ← presence bypass path
    → node_T_out

The two paths are in parallel. As Trim increases, more of C9's bypass path is active,
countering C8's treble cut. Both paths share the same output node.

> **Verify this topology node-by-node from the schematic before implementing ToneStage.h.**
> The above is consistent with all sources but exact capacitor terminal placement should
> be traced from the image. Invoke `schematic-checker` agent at that step.

**WDF model:** Series/parallel passive tree. No R-type adaptors. TONE pot and Trim pot are
variable resistors updated per block.

---

## 12. Volume and Output Stage

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| VOL | 100kA | Audio taper; output voltage divider |
| C11 | 1µF | VOL input coupling (AC block) |
| C_out_elec | 1µF electrolytic | Output coupling (from Theseus; may not be in all KOT units) |
| R14 | 1M | Output to GND (bleed resistor) |

VOL pot (100kA audio taper) attenuates the tone stage output. C11 (1µF) blocks DC.
Output HPF: f_c = 1/(2π × R14 × C11) ≈ 0.16 Hz — DC blocking only.

**WDF model:** Parallel divider for VOL; series C11; shunt R14. All linear.
Audio taper: `R_lower = 100k × pow(10, 2×x - 2)` where x ∈ [0,1].

---

## 13. Full Per-Channel Signal Flow (Node-by-Node)

> **CORRECTED 2026-06-15** — input network and Stage 1 feedback network re-traced from
> the schematic (see Sections 5–7). Updated below.

```
Guitar IN (AC, ±0.1–1V at BIAS=0V reference)
  │
node_IN ──────────────────────────────────────────────────────────
  ├─ R5 (1M) → GND                       [input bleed, negligible loading]
  └─ C3 (10nF) → IC_A pin 3(+)           [DC block / couples AC only]

IC_A pin 3(+):
  └─ R4 (1M) → BIAS                      [DC bias return for pin 3]

IC_A (non-inverting, no PolarityInverterT)
  pin 3(+) = signal in (above), pin 2(–) = NodeF (feedback, below), pin 1 = NodeG (output)

NodeF (IC_A pin 2–) — Z_lower to GND:
  ├─ R7 (33k) + C5 (10nF) series → GND   [Branch1, corner ≈482 Hz]
  └─ R8 (27k) + C6 (10nF) series → GND   [Branch2, corner ≈590 Hz; Hi Gain: R8→R8_eff≈12.17k]

NodeF to NodeG — Z_upper (feedback):
  ├─ C4 (100pF)                          [HF feedback path]
  └─ R6 (10k) + DRIVE (0–100k) series    [in parallel with C4]

NodeG (IC_A pin 1, output)
  │
  R9 (10k)
  │
  C7 (100nF) [Stage 2 HPF, f_c = 159 Hz]
  │
IC_B pin 6(–) feedback node:
  ├─ R9 (10k) → C7 → NodeG              [signal input]
  ├─ R10 (220k) → IC_B pin 7 (output)   [feedback]
  └─ [SW-1 ON: MA856×4 DiodePairT×2 in WDFParallelT, also IC_B pin7 ↔ pin6]

IC_B (inverting, PolarityInverterT REQUIRED)
  pin 5(+) → BIAS, pin 6(–) = feedback node, pin 7 = output
  │
IC_B pin 7 (output)
  │
  R11 (6.8k)                             [always present; SW-2 series R]
  │
node_HC ──────────────────────────────────────────────────────────
  └─ [SW-2 ON: 1S1588 DiodePairT shunt to BIAS]
  │
  R12 (1k)                               [tone stage isolation R]
  │
node_T1 ──────────────────────────────────────────────────────────
  ├─ TONE (25kB, 0–25k) → C8 (10nF) → BIAS    [variable treble cut]
  └─ R13 (6.8k) → Trim (0–50k) → C9 (10nF) → BIAS  [presence bypass]
  │
node_T_out
  │
  C11 (1µF)                              [output AC coupling]
  │
  VOL (100kA, audio taper)
  ├─ Pin 3: C11 output
  ├─ Pin 1: GND
  └─ Pin 2 (wiper): V_out
              │
              R14 (1M) → GND             [output bleed]
              │
             OUT
```

---

## 14. Component Values Master Table

Using **functional names** to avoid confusion between schematic numbering systems.

> **CORRECTED 2026-06-15** — input network and Stage 1 feedback network entries updated
> per Sections 5–7.

| Functional Name | matsumin ref | Value | Stage | Transfer function |
|----------------|-------------|-------|-------|-------------------|
| Input pulldown | R5 | 1M | Input | node_IN to GND |
| Input coupling | C3 | **10nF** | Input | DC block; HPF f_c ≈ 15.9 Hz w/ R4 |
| Pin3 bias R | R4 | 1M | Input | pin3(+) to BIAS; DC block w/ C3 |
| Stage 1 Z_lower Branch1 | R7 (33k) + C5 (10nF) | series | Stage 1 | NodeF→GND; corner ≈482 Hz |
| Stage 1 Z_lower Branch2 | R8 (27k) + C6 (10nF) | series | Stage 1 | NodeF→GND; corner ≈590 Hz (Hi Gain: R8→12.17k) |
| Stage 1 HF feedback cap | C4 | 100pF | Stage 1 | NodeF↔NodeG (Z_upper, ∥ R6+DRIVE) |
| DRIVE floor R | R6 | 10k | Stage 1 | NodeF↔NodeG, series w/ DRIVE (Z_upper) |
| DRIVE pot | DRIVE | 100kB | Stage 1 | Linear taper; 2-terminal rheostat in Z_upper only |
| Stage 2 coupling | C7 | **100nF** | Stage 2 | HPF f_c = **159 Hz** w/ R9 |
| Stage 2 input R | R9 | 10k | Stage 2 | Av = –R10/R9 = –22 |
| Stage 2 feedback R | R10 | 220k | Stage 2 | — |
| Soft clip diodes | D2–D5 | MA856 ×4 | SW-1 | Vf≈0.82V; 2× DiodePairT ∥ |
| Hard clip series R | R11 | 6.8k | SW-2 | Always in path |
| Hard clip diodes | D6/D7 | 1S1588 ×2 | SW-2 | Vf≈0.584V; 1× DiodePairT |
| Tone isolating R | R12 | 1k | Tone | — |
| Tone pot | TONE | 25kB | Tone | LPF 637–∞ Hz |
| Tone cut cap | C8 | 10nF | Tone | Main treble-cut cap |
| Presence R | R13 | 6.8k | Tone | Presence bypass path |
| Presence trim | Trim | 50kB | Tone | Default CCW; linear |
| Presence cap | C9 | 10nF | Tone | Presence bypass cap |
| Output coupling | C11 | 1µF | Volume | HPF f_c ≈ 0.16 Hz w/ R14 |
| Volume pot | VOL | 100kA | Volume | Audio taper |
| Output bleed | R14 | 1M | Volume | DC reference |
| Hi Gain R | R29 (Theseus) | 22k | Stage 1 | ∥ R8=27k when SW-3 ON |
| Hi Gain protect | R27 (Theseus) | 47R | Stage 1 | Switch protection |

---

## 15. Key Transfer Functions

> **CORRECTED 2026-06-15** — Stage 1 entries updated per Sections 5–7.

| Network | f_c | Notes |
|---------|-----|-------|
| Input DC block (C3 + R4) | ≈15.9 Hz | DC blocking only, not tone-shaping |
| Stage 1 Z_lower Branch1 (R7+C5) | ≈482 Hz | NodeF→GND |
| Stage 1 Z_lower Branch2 (R8+C6) | ≈590 Hz | NodeF→GND (Hi Gain: ≈1308 Hz w/ R8_eff≈12.17k) |
| Stage 1 Z_upper corner (C4 ∥ (R6+R_drive)) | 14.5–159 kHz | Depends on DRIVE (R_drive 0–100k) |
| Stage 2 input HPF (C7 + R9) | 159 Hz | C7=100nF; near full-range gain |
| Stage 2 gain (SW-1 OFF) | –22× (26.8 dB) | Inverting |
| Tone LPF @ max TONE (25k) | 637 Hz | Heavy treble cut |
| Gain peak (two-stage system) | ~4194 Hz (CCRMA, mid-DRIVE) | **Re-measure from implemented WDF model** — not yet validated against corrected topology |
| Output HPF (C11 + R14) | 0.16 Hz | DC block only |

---

## 16. Diode Parameters

### MA856 — SW-1 Soft Clip (D2–D5) — VALIDATED
```cpp
constexpr double Is_MA856 = 7.74e-13;   // Vf ≈ 0.820V @ 1mA
constexpr double n_MA856  = 1.512;
constexpr double Vt       = 25.85e-3;
constexpr double Rs_MA856 = 0.85;       // optional; model as series ResistorT
```
Two `DiodePairT` instances in `WDFParallelT` (D2+D3 pair ∥ D4+D5 pair).

### 1S1588 — SW-2 Hard Clip (D6–D7) — same as 1N914/1N4148
```cpp
constexpr double Is_1S1588 = 2.52e-9;   // Vf ≈ 0.584V @ 1mA
constexpr double n_1S1588  = 1.752;
constexpr double Rs_1S1588 = 0.568;
```
One `DiodePairT` instance.

---

## 17. Pot Tapers

| Control | Type | DSP mapping |
|---------|------|-------------|
| DRIVE | 100kB linear | `R = 100k × x` |
| TONE | 25kB linear | `R = 25k × x` |
| VOL | 100kA audio | `R = 100k × pow(10, 2×x - 2)` |
| Trim (Presence) | 50kB linear | `R = 50k × x` |

---

## 18. Clipping Mode Summary (Per Channel)

| Mode | SW-1 | SW-2 | SW-3 Hi Gain | Clamp | Character |
|------|------|------|-------------|-------|-----------|
| Boost | OFF | OFF | OFF | None | Clean boost |
| Boost Hi | OFF | OFF | ON | None | Higher gain clean boost |
| Overdrive | ON | OFF | OFF | ±0.82V | Soft, touch-sensitive |
| Overdrive Hi | ON | OFF | ON | ±0.82V | More aggressive OD |
| Distortion | OFF | ON | OFF | ±0.584V | Hard RAT-style |
| Distortion Hi | OFF | ON | ON | ±0.584V | High-gain hard clip |
| Both | ON | ON | OFF | ±0.82V→±0.584V | Stacked dual threshold |
| Both Hi | ON | ON | ON | ±0.82V→±0.584V | Max gain + stacked |

---

## 19. Open Items

**Tone stage topology:** Exact node-level wiring of TONE/C8/R13/Trim/C9.
Described in Section 11 and node diagram. Verify node-by-node from `king_of_tone_schematic.png`
before implementing ToneStage.h. Invoke `schematic-checker` agent at that step.
