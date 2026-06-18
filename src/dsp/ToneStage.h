#pragma once

#include <algorithm>

#include <chowdsp_wdf/chowdsp_wdf.h>

namespace monarch
{
namespace wdft = chowdsp::wdft;

/**
 * Tone stage — passive RC tone/presence network (linear WDF).
 *
 * Topology (circuit.md Section 11, matsumin primary). Entirely passive, no diodes. The TONE
 * pot is a 3-terminal tap: top = node_HC (input), bottom → C8 → BIAS, wiper → R13 → node_T_out.
 *
 *   node_HC ──R_a── wiper ──R13── node_T_out ──┬── VOL body (100k) ── BIAS
 *                     │                          └── Trim(0-50k) + C9(10n) ── BIAS  (presence)
 *                     └── R_b ── C8(10n) ── BIAS
 *
 *   - TONE pot body (25k) is split by the wiper into R_a (top↔wiper) and R_b (wiper↔bottom),
 *     R_a + R_b = 25k always (25kB linear). tone01=1 → R_a=0 (brightest, wiper at node_HC);
 *     tone01=0 → R_b=0 (darkest, wiper at the C8 end → maximum treble cut).
 *   - The wiper is a single electrical node where three branches meet → a 3-port WDF PARALLEL
 *     adaptor (KCL). R_a is folded as a series resistor between the node_HC source and the
 *     wiper adaptor (no R-type matrix needed — there is no op-amp here, unlike Stage 1/2).
 *   - Presence trim (50kB, 2-terminal rheostat, default fully CCW = 0): node_T_out → Trim →
 *     C9(10n) → BIAS. At presence=0 the hi-cut cap C9 shunts node_T_out directly (stock
 *     Bluesbreaker hi-cut); raising presence series-isolates C9 → less treble cut = "presence".
 *   - VOL pot body (100kA, top=node_T_out, bottom=BIAS) loads node_T_out with ~100k regardless
 *     of the wiper position; it is included here so node_T_out is correctly loaded. The VOL
 *     wiper attenuation (audio taper) + output cap C11 live in the separate VolumePot stage —
 *     this stage's output IS node_T_out, already loaded by the VOL body (do not re-load it).
 *
 * Output = V(node_T_out), read off the passive VOL-body resistor (across node_T_out → BIAS).
 */
class ToneStage
{
public:
    static constexpr double TONE_max = 25.0e3;  // 25kB linear (pot body)
    static constexpr double R13 = 6.8e3;        // wiper → node_T_out
    static constexpr double C8 = 10.0e-9;        // TONE bottom terminal → BIAS (treble cut)
    static constexpr double TRIM_max = 50.0e3;  // 50kB linear (presence rheostat)
    static constexpr double C9 = 10.0e-9;        // Trim → BIAS (presence hi-cut)
    static constexpr double VOL_body = 100.0e3; // VOL pot body load at node_T_out

    ToneStage()
    {
        setTone (0.5);
        setPresence (0.0); // default fully CCW (stock Bluesbreaker hi-cut, no presence boost)
    }

    void prepare (double sampleRate)
    {
        c8.prepare (sampleRate);
        c9.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        c8.reset();
        c9.reset();
    }

    /** TONE in [0,1], linear (25kB). 1 = brightest (R_a=0), 0 = darkest (R_b=0, max treble cut). */
    void setTone (double tone01)
    {
        const auto t = std::min (1.0, std::max (0.0, tone01));
        rA.setResistanceValue (TONE_max * (1.0 - t)); // top → wiper
        rB.setResistanceValue (TONE_max * t);          // wiper → bottom (→ C8)
    }

    /** PRESENCE in [0,1], linear (50kB). 0 = fully CCW (C9 hi-cut full); up = less treble cut. */
    void setPresence (double presence01)
    {
        const auto p = std::min (1.0, std::max (0.0, presence01));
        trimR.setResistanceValue (TRIM_max * p);
    }

    /** Process one sample. x = V(node_HC); returns V(node_T_out). Linear, passive. */
    inline double processSample (double x) noexcept
    {
        source.setVoltage (x);
        source.incident (topHalf.reflected());
        topHalf.incident (source.reflected());
        // V(node_T_out) = voltage across the passive VOL-body resistor (node_T_out → BIAS).
        return chowdsp::wdft::voltage<double> (volBody);
    }

private:
    // ---- node_T_out loads: VOL body ∥ [Trim + C9] (presence) ----
    wdft::ResistorT<double> volBody { VOL_body };
    wdft::ResistorT<double> trimR { 0.0 };
    wdft::CapacitorT<double> c9 { C9 };
    wdft::WDFSeriesT<double, decltype (trimR), decltype (c9)> presence { trimR, c9 };
    wdft::WDFParallelT<double, decltype (volBody), decltype (presence)> nodeTout { volBody, presence };

    // ---- wiper → R13 → node_T_out ----
    wdft::ResistorT<double> r13 { R13 };
    wdft::WDFSeriesT<double, decltype (r13), decltype (nodeTout)> r13Branch { r13, nodeTout };

    // ---- wiper → R_b → C8 → BIAS (treble-cut branch) ----
    wdft::ResistorT<double> rB { 0.5 * TONE_max };
    wdft::CapacitorT<double> c8 { C8 };
    wdft::WDFSeriesT<double, decltype (rB), decltype (c8)> rbC8 { rB, c8 };

    // ---- wiper node = parallel(rbC8, r13Branch); R_a in series up to the source ----
    wdft::WDFParallelT<double, decltype (rbC8), decltype (r13Branch)> wiper { rbC8, r13Branch };
    wdft::ResistorT<double> rA { 0.5 * TONE_max };
    wdft::WDFSeriesT<double, decltype (rA), decltype (wiper)> topHalf { rA, wiper };

    // ---- node_HC input source (root) ----
    wdft::IdealVoltageSourceT<double, decltype (topHalf)> source { topHalf };
};

} // namespace monarch
