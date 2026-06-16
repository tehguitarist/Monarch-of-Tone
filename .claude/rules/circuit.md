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
| **SW-1 diodes (soft clip)** | **D2–D5** | 1N914+1N4004 (sub) | D2–D5 | BAS33 ×4 (sub) | BOTH ARE SUBS — **topology also corrected, see Section 9** | MA856 ×4 (original); modeled as 1× `DiodePairT` w/ n_eff=2×n_MA856 |
| SW-1 DIP switch | DIP SW-1 | soft clip | SW1 pos2 | Soft Clip | MATCH (functional gating only) | SW-1 |
| **SW-1 feedback branch series R** | **R11** | 6.8k | R8 | 6.8k | MATCH (value) — **role corrected, see Section 9** | 6.8k, in series w/ diode network, branch ∥ R10, gated by SW-1 |
| SW-2 diodes (hard clip) | D6/D7 | 1N914 (sub) | D6/D7 | 1N914 (sub) | BOTH USE 1N914 | 1S1588 (original) |
| SW-2 DIP switch | DIP SW-2 | hard clip | SW1 pos3 | Hard Clip | MATCH (functional gating only) | SW-2 |
| **Stage 2 output / SW-2+Tone series R** | **R12** | 1k | R3 | 1k | MATCH (value) — **role corrected, see Section 10** | 1k, always in path: IC_B pin7 → R12 → node_HC → TONE pot top terminal |
| TONE pot | TONE | 25kB | TONE | 25kB | MATCH | 25kB linear |
| Tone treble-cut cap | C8 | 10nF | C6 | 10nF | MATCH | 10nF |
| Presence series R | R13 | 6.8k | R10 | 6.8k | MATCH | 6.8k |
| Presence trimpot | Trim 50k | 50kB | PRESENCE | 50kB | MATCH | 50kB linear |
| Presence cap | C9 | 10nF | C7 | 10nF | MATCH | 10nF |
| VOL pot | VOL-a/b | 100kA | VOL | 100kA | MATCH | 100kA audio |
| Output coupling cap | C11-a/b | 1µF | C8 | 1µF film | MATCH | 1µF |
| Output electrolytic cap | — | not shown | C9 | 1µF electro | Theseus addition | 1µF electro |
| Output bleed R | R14 | 1M | R11 | 1M | MATCH | 1M |
| ~~Hi Gain R (mod)~~ → **bypass-LED current-limit R** | — | not in matsumin | R29/R30 | 22k | **CORRECTED 2026-06-16: power-supply, not gain stage** (LED resistor) | not a gain-stage part |
| ~~Hi Gain protection R~~ → **VCC supply-filter R** | — | not in matsumin | R27/R28 | 47R | **CORRECTED 2026-06-16: feeds VA/VC rails w/ C21/C22**, not gain stage | not a gain-stage part |
| **Hi Gain element (actual)** | — | not in matsumin | SW1B + R3 | 1k | **Theseus addition — corrected**; SW1B switches R3(1k) in Stage-1 DRIVE feedback (exact wiring TBC, see Section 6) | TBC |
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

### SW-1/SW-2 topology, R11/R12 roles, and Tone stage — CORRECTED 2026-06-16

> Full re-trace of `king_of_tone_schematic.png` from IC_B pin 7 (output) through the
> clipping switches and into the tone/presence/volume network, via multiple crops
> (R10/R11/diode region, D2–D5 zoom, SW-2/D6D7 region, TONE/Presence/Trim/VOL region).
> The previous version of Sections 9–11 contained several structural errors, listed below.

1. **R11 (6.8k) is part of the SW-1 (soft-clip) feedback branch, not a "hard clip series R"
   always in the signal path.** R11 is in series with the D2–D5 diode network; this
   R11+diodes branch is gated by SW-1 and sits in parallel with R10 (220k) at IC_B's
   feedback node (pin 6–). When SW-1 is OFF, R11 and the diodes are entirely out of circuit
   — only R10 forms the feedback path.

2. **R12 (1k) is the always-present resistor from IC_B pin 7 (output) into the rest of the
   chain** (previously mis-assigned as "tone isolating R" only). R12 connects Stage 2's
   output directly to node_HC, which is the SW-2 hard-clip node AND the TONE pot's top
   terminal. R12 is present in every clipping mode.

3. **SW-1 diode topology is two back-to-back 2-diode series strings, not two antiparallel
   pairs.** D4+D5 (in series, same orientation) sit in parallel with D2+D3 (in series,
   opposite orientation) — i.e. `[D4→D5] ∥ [D2→D3]`. This is electrically a single
   symmetric diode pair whose effective per-branch threshold is **2×Vf_MA856 ≈ 1.64V**
   (two diodes in series ≡ one diode with `n_eff = 2×n`, same `Is`, for an ideal Shockley
   diode — see Section 16). See Section 9 for the corrected WDF model.

4. **SW-2 (D6+D7) is confirmed as a true antiparallel 1S1588 pair** (D6 points one way,
   D7 the other), shunting node_HC to BIAS, gated by SW-2 — this part of the previous
   documentation was correct in substance. Only its series resistor was misidentified
   (R12, not R11 — see point 2).

5. **The TONE stage is a 3-terminal potentiometer tap, not two parallel 2-terminal
   branches.** node_HC connects to the TONE pot's top terminal. The pot's bottom terminal
   goes to C8(10nF)→BIAS. The **wiper** (not a separate node from node_HC) feeds R13(6.8k)
   into the presence/volume network. See Section 11 for the corrected topology and WDF
   model (R-type adaptor at the wiper).

6. **FOOT SW2-1 / FOOT SW2-2** (footswitch contacts visible in the SW-2 and
   Trim/VOL crops) are the physical true-bypass footswitch wiring — **not part of the WDF
   signal model**. Per `architecture.md`, bypass is implemented via `bypassedA`/`bypassedB`
   atomics, not by modelling the footswitch contacts.

**Decision:** Sections 9, 10, 11, 13, 14 corrected accordingly. These are topology
corrections found via direct schematic re-trace (primary source).

> **CROSS-REFERENCE COMPLETE 2026-06-16** — poppler installed; both reference PDFs now
> fully extracted and reviewed (Theseus page-28 schematic rendered at 400 dpi and traced
> region-by-region; CCRMA paper text extracted). Results, matched **by topology/function,
> not by reference designator** (the two sources renumber the same parts — see the
> label-equivalence note below):
>
> - **Soft clip (SW-1) — CONFIRMED.** Theseus page 28 shows the diode network as
>   `[D2+D3] ∥ [D5+D4]` — two 2-diode series strings of opposite polarity (BAS33 in
>   Theseus / MA856 in the real KOT) — in series with **Theseus R8 = 6k8** (≡ matsumin
>   **R11**), the branch in parallel with **Theseus R7 = 220k** (≡ matsumin **R10**),
>   gated by **SW1A**. Exactly matches Section 9 and the `n_eff = 2×n` derivation.
> - **Hard clip (SW-2) — CONFIRMED.** **Theseus R9 = 1k** (≡ matsumin **R12**) is the
>   always-present series R from IC1B pin 7 to node_HC; D6/D7 (1N914) true-antiparallel
>   pair shunt node_HC to VB, gated by SW1C. Matches Section 10.
> - **Tone stage — CONFIRMED.** TONE (25kB) is a 3-terminal pot: top = node_HC, bottom →
>   **Theseus C6 = 10n** (≡ matsumin **C8**) → VB, wiper → **Theseus R10 = 6k8**
>   (≡ matsumin **R13**) → node_T_out; then PRESENCE (50kB ≡ Trim) → **Theseus C7 = 10n**
>   (≡ matsumin **C9**) → VB, and VOLUME (100kA) top = node_T_out, wiper → **Theseus C8 =
>   1µF** (≡ matsumin **C11**) → out; **Theseus R11 = 1M** (≡ matsumin **R14**) bleed.
>   Matches Section 11–12 (3-terminal pot tap, R-type adaptor at the wiper).
> - **Label-equivalence note (topology-over-labels):** matsumin **R10/R11/R12/R13/R14**
>   ≡ Theseus **R7/R8/R9/R10/R11** — a clean −3 designator shift across the entire
>   back-end, identical function/location. This is the canonical reason this document uses
>   functional names internally.
> - **Hi-Gain (SW-3) — DISCREPANCY FOUND.** circuit.md's prior "R29(22k) ∥ R8(27k) in
>   Z_lower" model is wrong: Theseus R29/R30(22k) are bypass-LED current-limit resistors
>   and R27/R28(47R) are VCC supply-filter resistors (both power-supply). The real element
>   is **SW1B switching R3(1k)** in the Stage-1 DRIVE feedback; exact wiring is not yet
>   pinned (matsumin lacks the mod; the dense render leaves the DRIVE pot's wiper
>   destination ambiguous). See Section 6. **Open item — confirm before implementing.**
> - **Stage-1 DRIVE-floor value — source difference.** Theseus floor R = 100k (R2);
>   matsumin floor R = 10k (R6). circuit.md follows matsumin (10k) for the 1:1 Ver2 clone.
> - **CCRMA paper** corroborates the general gain-stage / negative-feedback-clipper
>   structure (4th-order gain-stage transfer function; diode clipper in the feedback) but
>   does **not** cover the Hi-Gain mod or specific floor values — it analyzes the stock
>   circuit only.

---

## 3. Circuit Overview

The King of Tone is two independent modified Bluesbreaker circuits in series, each with
its own footswitch. Per channel, the verified signal path is:

```
IN → [Input HPF: C3(10nF) + R_network] → [Stage 1: non-inverting amp, IC_A]
   → [DRIVE pot (100kB): cross-stage gain control]
   → [Stage 2: inverting amp, IC_B, gain=–22, HPF f_c=159Hz
       feedback: R10(220k) ∥ (SW-1: R11(6.8k) + MA856 back-to-back pairs, optional)]
   → [R12(1k): always-present Stage 2 output series R]
   → [SW-2: 1S1588×2 true-antiparallel shunt to BIAS at node_HC (optional)]
   → [Tone: TONE(25kB) 3-terminal tap + C8(10nF) + Presence R13/Trim/C9]
   → [Volume: VOL(100kA) + C11(1µF)] → OUT
```

BIAS = VCC/2 = 4.5V = DSP signal ground (0V in model).

---

## 4. Power and Bias

- VCC = 9V; BIAS = 4.5V from 47k/47k voltage divider + 100µF filter caps
- D1 (1N4001 in matsumin; 1N5817 in Theseus): reverse polarity protection on DC jack
- **DSP model:** BIAS = 0V. Exclude all power supply and bias components.

### Verified 2026-06-16 — single 9V supply, NO charge pump / voltage doubler

Both schematics confirm a plain single-rail 9V supply with a 4.5V bias divider — there is
**no voltage doubler, charge pump, or any voltage-multiplication stage** anywhere in the KOT.

- **matsumin:** 006P (9V) battery → D1 (1N4001) → VCC 9V rail → R1/R2 (47k/47k) divider →
  BIAS 4.5V, filtered by C1/C2 (100µF). LEDs (D8/D9) via R15 (2.2k) for bypass indication.
- **Theseus measured pin voltages** (prototype, 9.60V supply — authoritative for operating
  points): op-amp **pin 8 (V+) = 9.15V**, **pin 4 (V−) = 0V**, all input/output/bias pins
  **≈ 4.5–4.66V**. The "charge pumps and delay chips" line in the Theseus doc is a generic
  8-pin-IC identification glossary, **not** a KOT feature.

### Op-amp output headroom — matters ONLY for Boost mode

- Op-amps (JRC4580D) run **V+ ≈ 9.15V, V− = 0V, bias ≈ 4.5V**. Realistic JRC4580 output
  swing saturates ~1.3–1.5V from each rail, giving a usable swing of roughly **±3.3V around
  bias** (≈ +13.3 dBu peak). The saturation knee is **gradual/soft**, not a hard comparator
  clip.
- **In bipolar model terms: op-amp output clip ceiling ≈ ±3.3V.**
- **Clipping-mode interaction (the key point):**
  - SW-1 soft (MA856 back-to-back, n_eff): effective feedback-node threshold ≈ ±1.64V.
  - SW-2 hard (1S1588): clamps node_HC to ≈ ±0.584V.
  - Both diode clamps are **far below the ±3.3V op-amp rails**, so in **Overdrive /
    Distortion / Both** the diodes always clip first and the op-amp rails are never reached
    → the ideal-op-amp model is exact for tone in those modes.
  - **Boost mode (SW-1 OFF, SW-2 OFF) has no diodes — the op-amp rails ARE the only
    clipping.** An ideal (infinite-swing) op-amp would make Boost an impossibly clean boost.
    The Theseus manual confirms the hardware clips: *"not a perfectly clean boost… the
    op-amp itself will eventually clip at higher gain settings."*
- **Required correction (tone-safe):** model JRC4580 output saturation at ≈ ±3.3V so Boost
  mode clips like the hardware. Because it only engages above ±3.3V — a level the diode
  modes never reach — **adding it does not alter the tone of any diode-clipping mode.** See
  dsp.md ("Op-amp rail saturation") for the implementation requirement.

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

**Hi Gain mod (SW-3) — ⚠️ UNDER REVISION, see Theseus cross-check below:**

> **DISCREPANCY FOUND 2026-06-16 (Theseus schematic, page 28 hi-res trace).** The model
> below (R29 ∥ R8 in Z_lower Branch2) is **NOT what the Theseus schematic actually shows**
> and is retained here only as the superseded prior analysis. Two errors:
>
> 1. **R29 (22k) and R27 (47R) are NOT gain-stage components.** In the Theseus schematic
>    they sit in the **power-supply / bias region**: R29/R30 (22k) are the **bypass-LED
>    current-limit resistors** (R30 feeds "LED B" from +9V; R29 the channel-A LED), and
>    R27/R28 (47R) are **VCC supply-filter resistors** feeding the VA/VC rails together
>    with C21/C22 (100µF). Neither connects to R8 or any Stage-1 gain element. circuit.md
>    Section 1's "Hi Gain R = R29" / "Hi Gain protection R = R27" rows are therefore wrong.
> 2. **The actual Hi-Gain element is DIP switch SW1B switching R3 (1k)** into the Stage-1
>    DRIVE/feedback (Z_upper) network — consistent with the Theseus manual's description:
>    "Hi Gain… shifts the gain range of the **drive knob**" (a Stage-1 feedback effect, not
>    a Z_lower Branch2 change). Exact wiring (which sub-net R3 modifies, and the resulting
>    gain direction/magnitude) is **not yet pinned** — the page-28 render is too dense to
>    resolve the DRIVE pot's wiper destination with confidence, and matsumin (primary
>    source) does **not** contain the Hi-Gain mod at all. **Re-confirm from a higher-res
>    Theseus schematic / AionFX gain analysis / KOT community Hi-Gain mod notes before
>    implementing Stage-1 Hi Gain.**
>
> Note: Theseus also uses a **100k** Stage-1 DRIVE-floor resistor (R2) where matsumin uses
> **10k** (R6) — a genuine Theseus-vs-matsumin design difference (cf. the C3 10nF/22nF
> difference, Section 2). circuit.md follows matsumin (R6=10k) for the 1:1 Ver2 clone.

**Superseded prior analysis (DO NOT IMPLEMENT — see discrepancy above):**
Switches R29 (22k) in parallel with R8 (27k), via R27 (47R protection) in series, within
Branch 2 (the R8+C6 series branch):

    R8_eff = (R8 ∥ R29) + R27 = (27k ∥ 22k) + 47 ≈ **12.17k**
    Branch2_hi(s) = R8_eff + 1/(sC6)   → new corner f2_hi = 1/(2π·R8_eff·C6) ≈ 1308 Hz

A smaller R8_eff makes Branch2 more resistive at lower frequencies, reducing Z_lower in the
mid/high band and therefore **increasing** Av(s).

**WDF model:** R-type adaptor at NodeF (IC_A pin 2(–)). Ports: Branch1 = R7 series C5,
Branch2 = R8 series C6, Z_upper = C4 ∥ (R6 + R_drive). NodeG (pin 1, op-amp output) feeds
R9/C7 toward Stage 2. The Hi-Gain scattering-matrix switch (whatever the corrected element
turns out to be) remains a `setSMatrixData()` swap with no tree reconstruction.

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

> **CORRECTED 2026-06-16** — re-traced from `king_of_tone_schematic.png` (R10/R11/diode
> region crops). The diode network is two back-to-back 2-diode series strings, in series
> with R11, forming a branch in parallel with R10 — not "two antiparallel pairs in
> parallel with R10" as previously documented.

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| D4+D5 | MA856, two in series, same orientation | One leg of the back-to-back pair |
| D2+D3 | MA856, two in series, same orientation, **opposite** to D4+D5 | Other leg, in parallel with D4+D5 |
| R11 | 6.8k | In series with the `[D4+D5] ∥ [D2+D3]` diode network |
| DIP SW-1 | — | Gates the R11 + diode-network branch |
| R10 | 220k | Always-present feedback resistor, IC_B pin 6(–) ↔ pin 7 |

**Topology:**

```
IC_B pin 6(–) ──┬── R10 (220k) ──────────────────────────── IC_B pin 7 (output)
                │                                                    │
                └── R11 (6.8k) ── [D4─D5] ∥ [D2─D3] ── SW-1 ────────┘
                                   (back-to-back, opposite polarity)
```

R10 is always present. The R11 + diode branch is gated by SW-1 and sits in **parallel**
with R10 when SW-1 is ON; when SW-1 is OFF, only R10 forms the feedback path (linear,
matching circuit.md's Stage 2 gain of –22 with no clipping).

**Diode network — two diodes in series ≡ one diode with doubled `n`:**

For an ideal Shockley diode, `V = n·Vt·ln(I/Is + 1)`. Two identical diodes carrying the
same current `I` in series sum their voltages: `V_total = 2·(n·Vt·ln(I/Is+1)) =
(2n)·Vt·ln(I/Is+1)` — i.e. electrically identical to a **single diode with `Is` unchanged
and `n_eff = 2×n`**. Therefore `[D4 series D5]` ≡ one diode with `n_eff = 2×n_MA856 ≈
3.024`, `Is = Is_MA856`; likewise `[D2 series D3]`. Since these two equivalent diodes are
in parallel with opposite orientation, the whole network is exactly one symmetric
`DiodePairT(Is_MA856, n_eff=3.024)`. (If `Rs_MA856` is modelled, `Rs_eff = 2×Rs_MA856 ≈
1.7Ω`, negligible.)

**How it clips:**
Effective per-leg forward voltage ≈ **2 × Vf_MA856 ≈ 1.64V** (vs 0.82V for a single MA856).
Once the voltage across the R11+diode branch exceeds ≈1.64V, the diode network's dynamic
resistance drops sharply and the branch (R11 ≈ 6.8k in series with the now-low diode
impedance) begins to dominate over R10 (220k), pulling Stage 2's feedback impedance down
and clamping the output. Symmetric — pure odd-harmonic content from this stage. Exact
clamped output level depends on the R11/R10 divider and should be measured from the
implemented model.

**WDF model:** One `DiodePairT<double, ..., DiodeQuality::Best>(Is_MA856, Vt, n_eff=2×n_MA856)`
in series with `ResistorT(R11=6.8k)`; this combination in `WDFParallelT` with `R10=220k`,
forming the feedback impedance at IC_B's R-type adaptor (pin 6–). SW-1 OFF: precomputed
matrix with R10 only (R11+diode branch removed). SW-1 ON: precomputed matrix with both
branches in parallel.

---

## 10. SW-2 — Hard-Clip Shunt Diodes

> **CORRECTED 2026-06-16** — re-traced from `king_of_tone_schematic.png` (SW-2/D6D7 region
> crops). The always-present series resistor from IC_B's output is **R12 (1k)**, not R11
> (R11 belongs to the SW-1 feedback branch, Section 9). D6/D7 topology and ~0.584V
> threshold are confirmed correct as previously documented.

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| R12 | 1k | IC_B pin 7 (output) to node_HC (always in signal path) |
| D6+D7 | 1S1588 true antiparallel pair (D6 one orientation, D7 the other) | node_HC shunted to BIAS |
| DIP SW-2 | — | Gates D6/D7 only, not R12 |

R12 (1k) is **permanently in the signal path** regardless of SW-2 state — it is the only
connection between IC_B's output and node_HC (which feeds the TONE pot's top terminal,
Section 11). SW-2 connects the 1S1588 antiparallel pair at node_HC, shunting to BIAS.

**How it clips:**
When the signal at node_HC exceeds ±Vf_1S1588 ≈ ±0.584V, D6/D7 conduct and shunt node_HC
toward BIAS. R12 (1k) limits the current Stage 2's output must supply into this shunt,
controlling the hardness of the clipping knee.

**SW-2 OFF effect on level:** With D6/D7 disconnected, node_HC is simply the far side of
R12, feeding directly into the TONE pot's top terminal (Section 11). R12 (1k) is small
relative to the TONE pot's resistance (0–25k) and the impedances downstream, so loading
is minor in all SW-2 states — the previous ~–17.8dB divider calculation (based on the
incorrect R11/R12 assignment) is removed. Measure actual loading from the implemented
model.

**WDF model:** R12 (1k) always as series resistor from IC_B's output (post `PolarityInverterT`)
to node_HC. `DiodePairT` (1S1588: Is=2.52e-9, n=1.752) at node_HC shunting to BIAS.
SW-2 OFF: precomputed open-circuit matrix at the diode port.

---

## 11. Tone Control Stage

> **RESOLVED 2026-06-16** — node-by-node trace from `king_of_tone_schematic.png`
> (TONE/Presence/Trim/VOL region crops). The TONE pot is a **3-terminal tap**, not two
> parallel 2-terminal branches as previously documented. This was the project's last
> "Open Item" (Section 19); it is now resolved.

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| TONE | 25kB | 3-terminal pot: top=node_HC, bottom→C8→BIAS, wiper→R13 |
| C8 | 10nF | TONE pot bottom terminal → BIAS (treble-cut cap) |
| R13 | 6.8k | TONE pot wiper → node_T_out |
| Trim | 50kB | 2-terminal rheostat (like DRIVE): node_T_out → Trim → C9 |
| C9 | 10nF | Trim → C9 → BIAS (presence bypass cap) |

**This stage is entirely passive and linear — no diodes.**

**Topology (for WDF):**

```
node_HC (from R12, Section 10) ── TONE pot top terminal
                                        │
                              ┌─────────┴─────────┐
                       R_a (top→wiper)       (pot body, full R=25k,
                              │                R_a + R_b = 25k always)
                            wiper ─────────────┐
                              │                 │
                       R_b (wiper→bottom)   R13 (6.8k)
                              │                 │
                             C8 (10nF)     node_T_out ──┬── Trim (0–50k) → C9 (10nF) → BIAS
                              │                          └── VOL (100kA) top terminal
                             BIAS
```

- node_HC connects to the TONE pot's **top terminal** (not a separate "node_T1" feeding
  two independent branches).
- The pot's **bottom terminal** goes to C8 (10nF) → BIAS.
- The **wiper** taps the pot at a variable point, splitting its 25k body into
  `R_a` (top↔wiper) and `R_b` (wiper↔bottom), with `R_a + R_b = 25k` always. As TONE
  sweeps from one end to the other, `R_a: 0→25k` and `R_b: 25k→0` (or vice versa —
  determine the CW/CCW mapping from the implemented frequency response: max TONE should
  give the documented heavy-treble-cut behaviour).
- The wiper feeds **R13 (6.8k)** into `node_T_out`.
- From `node_T_out`: the **Trim (0–50k) + C9 (10nF) → BIAS** presence-bypass path (Trim
  used as a 2-terminal rheostat, same pattern as DRIVE — Section 7), and the **VOL pot's
  top terminal** (Section 12).

**Presence trimpot (Trim 50k):**
From Theseus documentation: "Each channel's Presence trimmer fades the hi-cut capacitor
out of the circuit, reducing the treble cut as you turn it up." "Default position is full
counter-clockwise — equivalent to the stock Bluesbreaker." Functionally unchanged from the
previous documentation — only its attachment point (`node_T_out`, downstream of R13,
rather than a parallel branch from `node_T1`) has been corrected.

**Treble-cut behaviour (qualitative):** When the wiper sits at the C8 end (`R_b≈0`), the
wiper node is directly at the C8/BIAS junction — node_HC's signal reaches `node_T_out`
through the full 25k pot body (`R_a≈25k`) in series with whatever C8 doesn't shunt to
BIAS, i.e. maximum treble cut. When the wiper sits at the node_HC end (`R_a≈0`), the wiper
**is** node_HC — full-bandwidth signal passes to R13/node_T_out with C8 only loading
through the full 25k body to BIAS, i.e. minimal treble cut. The exact corner frequency
vs. TONE position must be measured from the implemented WDF model — the previous
`f_c = 1/(2π·R_tone·C8)` formula (637 Hz at TONE=25k) assumed the old two-branch topology
and is no longer directly applicable; re-derive from the R-type adaptor's transfer function.

**WDF model:** R-type adaptor (3-port) at the TONE pot's wiper node:
- Port 0: `R_a` (variable, toward node_HC — the "up" port carrying the incident wave)
- Port 1: `R_b` in series with `CapacitorT(C8=10nF)`, toward BIAS
- Port 2: `ResistorT(R13=6.8k)`, toward node_T_out (presence + volume load)

`R_a` and `R_b` are updated per block (`R_a + R_b = 25k`, linear taper) via
`setResistanceValue` under `ScopedDeferImpedancePropagation`. node_T_out then feeds the
Trim/C9 presence branch (Trim as a 2-terminal rheostat, same pattern as Stage 1's DRIVE)
and the VOL pot (Section 12) as parallel loads — both linear, no further R-type adaptor
needed there (simple series/parallel tree).

---

## 12. Volume and Output Stage

> **Confirmed 2026-06-16** — VOL/C11 wiring re-traced from the schematic (Trim/VOL region
> crop): VOL pot top terminal = `node_T_out` (Section 11), bottom terminal = BIAS, wiper →
> C11 → output rail. A footswitch (FOOT SW2-2) sits between `node_T_out` and the VOL pot's
> top terminal in the physical schematic — this is true-bypass wiring, **excluded from the
> WDF model** per `architecture.md` (bypass handled via `bypassedA`/`bypassedB` atomics).

**Components (from matsumin schematic):**

| Component | Value | Connection |
|-----------|-------|-----------|
| VOL | 100kA | Audio taper; top terminal = node_T_out, bottom terminal = BIAS, wiper → C11 |
| C11 | 1µF | VOL wiper → output rail (AC block) |
| C_out_elec | 1µF electrolytic | Output coupling (from Theseus; may not be in all KOT units) |
| R14 | 1M | Output to GND (bleed resistor) |

VOL pot (100kA audio taper) attenuates the tone stage output. C11 (1µF) blocks DC.
Output HPF: f_c = 1/(2π × R14 × C11) ≈ 0.16 Hz — DC blocking only.

**WDF model:** Parallel divider for VOL (top=node_T_out, bottom=BIAS, wiper output);
series C11; shunt R14. All linear. Audio taper: `R_lower = 100k × pow(10, 2×x - 2)`
where x ∈ [0,1].

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
  ├─ R10 (220k) → IC_B pin 7 (output)   [feedback, always present]
  └─ [SW-1 ON: R11(6.8k) → DiodePairT(MA856, n_eff=2×n_MA856≈3.024) → IC_B pin 7,
      branch in WDFParallelT with R10]

IC_B (inverting, PolarityInverterT REQUIRED)
  pin 5(+) → BIAS, pin 6(–) = feedback node, pin 7 = output
  │
IC_B pin 7 (output)
  │
  R12 (1k)                               [always present; Stage 2 output series R]
  │
node_HC ──────────────────────────────────────────────────────────
  └─ [SW-2 ON: 1S1588 DiodePairT (D6 ∥ D7, true antiparallel) shunt to BIAS]
  │
TONE pot (25kB) — top terminal = node_HC; R-type adaptor at wiper:
  ├─ R_a (top→wiper, variable, R_a+R_b=25k)
  ├─ R_b (wiper→bottom) + C8 (10nF) → BIAS    [treble-cut path]
  └─ wiper → R13 (6.8k) → node_T_out
  │
node_T_out ──────────────────────────────────────────────────────
  ├─ Trim (0–50k, rheostat) → C9 (10nF) → BIAS  [presence bypass]
  └─ VOL (100kA, audio taper)
       ├─ top terminal: node_T_out
       ├─ bottom terminal: BIAS
       └─ wiper → C11 (1µF) → output rail
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
| Soft clip diodes | D2–D5 | MA856 ×4 | SW-1 | `[D4+D5]∥[D2+D3]` back-to-back series strings; modeled as 1× DiodePairT, n_eff=2×n_MA856≈3.024, Vf≈1.64V |
| SW-1 feedback branch R | R11 | 6.8k | SW-1 | In series w/ diode network; branch ∥ R10, gated by SW-1 |
| Hard clip diodes | D6/D7 | 1S1588 ×2 | SW-2 | True antiparallel pair; Vf≈0.584V; 1× DiodePairT |
| Stage 2 output / SW-2 series R | R12 | 1k | SW-2/Tone | Always in path; IC_B pin7 → R12 → node_HC → TONE pot top terminal |
| Tone pot | TONE | 25kB | Tone | 3-terminal tap (R-type adaptor); top=node_HC, bottom→C8→BIAS, wiper→R13→node_T_out |
| Tone cut cap | C8 | 10nF | Tone | TONE pot bottom terminal → BIAS |
| Presence R | R13 | 6.8k | Tone | TONE wiper → node_T_out |
| Presence trim | Trim | 50kB | Tone | node_T_out → Trim → C9 → BIAS; 2-terminal rheostat (like DRIVE); default CCW |
| Presence cap | C9 | 10nF | Tone | Trim → C9 → BIAS |
| Output coupling | C11 | 1µF | Volume | HPF f_c ≈ 0.16 Hz w/ R14 |
| Volume pot | VOL | 100kA | Volume | Audio taper |
| Output bleed | R14 | 1M | Volume | DC reference |
| Hi Gain element (actual) | SW1B + R3 (Theseus) | 1k | Stage 1 | **CORRECTED 2026-06-16:** SW1B switches R3(1k) in Stage-1 DRIVE feedback; exact wiring TBC — see Section 6 |
| ~~Hi Gain R~~ (was mis-ID'd) | R29/R30 (Theseus) | 22k | — | **NOT gain stage** — bypass-LED current-limit R (power supply) |
| ~~Hi Gain protect~~ (was mis-ID'd) | R27/R28 (Theseus) | 47R | — | **NOT gain stage** — VCC supply-filter R feeding VA/VC rails (w/ C21/C22) |

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
| SW-1 effective diode threshold | ≈1.64V (2×Vf_MA856) | `[D4+D5]∥[D2+D3]` back-to-back series strings, n_eff=2×n_MA856 |
| Tone treble-cut corner | depends on TONE wiper position (R_a/R_b split of 25k pot) | **Measure from implemented R-type WDF model** — previous `1/(2π·R_tone·C8)` formula assumed the old two-branch topology and no longer applies directly |
| Gain peak (two-stage system) | ~4194 Hz (CCRMA, mid-DRIVE) | **Re-measure from implemented WDF model** — not yet validated against corrected topology |
| Output HPF (C11 + R14) | 0.16 Hz | DC block only |

---

## 16. Diode Parameters

### MA856 — SW-1 Soft Clip (D2–D5) — VALIDATED, topology corrected 2026-06-16
```cpp
constexpr double Is_MA856 = 7.74e-13;   // Vf ≈ 0.820V @ 1mA
constexpr double n_MA856  = 1.512;
constexpr double Vt       = 25.85e-3;
constexpr double Rs_MA856 = 0.85;       // optional; model as series ResistorT

// SW-1 network is [D4 series D5] ∥ [D2 series D3] (back-to-back, opposite polarity).
// Two identical diodes in series ≡ one diode with n doubled, same Is:
constexpr double n_eff_MA856  = 2.0 * n_MA856;   // ≈ 3.024
constexpr double Rs_eff_MA856 = 2.0 * Rs_MA856;  // ≈ 1.7 Ω (negligible; optional)
```
**One** `DiodePairT<double, ..., DiodeQuality::Best>(Is_MA856, Vt, n_eff_MA856)`, in series
with `ResistorT(R11=6.8k)`, this combination in `WDFParallelT` with `R10=220k`. Do **not**
instantiate two `DiodePairT`s for SW-1 — the back-to-back series strings collapse to one
symmetric pair with doubled `n`.

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

The previously-open Tone stage topology item (exact node-level wiring of
TONE/C8/R13/Trim/C9) was resolved by direct schematic re-trace — see Section 11 for the
corrected 3-terminal-pot-tap topology and R-type WDF model. The 2026-06-16 Theseus PDF
cross-reference (Section 2) **confirmed** the SW-1 soft-clip, SW-2 hard-clip, tone,
presence, volume, and output topology by function-matching.

**OPEN ITEM (reopened 2026-06-16): Stage-1 Hi-Gain (SW-3) wiring.** The Theseus
cross-reference invalidated the prior "R29(22k) ∥ R8(27k) in Z_lower Branch2" model —
R29/R27 are power-supply parts, and the real Hi-Gain element is SW1B switching R3(1k) in
the Stage-1 DRIVE feedback, but the exact wiring (which sub-net R3 modifies; gain
direction/magnitude) is not yet pinned. matsumin (primary source) does not contain the mod.
**Resolve before implementing Stage-1 Hi-Gain** via one of: a higher-resolution Theseus
schematic, the AionFX Theseus gain-stage analysis, or KOT community Hi-Gain mod notes.
See Section 6.

**Implementation order** (Step 4): Stage1 → **Stage1 Hi Gain (BLOCKED — see open item)** →
Stage2 → SW1SoftClip → SW2HardClip → ToneStage. The Stage1 (stock), Stage2, SW-1, SW-2,
and Tone sections are finalized and ready; only Stage-1 Hi-Gain is blocked. Stages can be
implemented in this order with Hi-Gain deferred until its topology is confirmed (it is an
isolated scattering-matrix swap on the otherwise-finalized Stage 1).
