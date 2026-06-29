#pragma once

#include <chowdsp_wdf/chowdsp_wdf.h>

#include "DiodeClipper.h"

namespace monarch
{
namespace wdft = chowdsp::wdft;

/**
 * SW-1 soft clip — Stage 2 (IC_B inverting amp) with the MA856 soft-clip diodes active
 * (Overdrive mode). Nonlinear WDF.
 *
 * Topology (circuit.md Sections 8–9, 16):
 *   - Inverting op-amp, pin6(−) = virtual ground (BIAS = 0 V). Input C7(100nF)+R9(10k).
 *   - Feedback = R10(220k) ∥ [R11(6.8k) + diode network], pin6(−) ↔ pin7.
 *   - Diode network = MA856 `[D4+D5] ∥ [D2+D3]` back-to-back 2-diode series strings ≡ ONE
 *     symmetric `DiodePairT` with Is = Is_MA856 and n_eff = 2×n_MA856 ≈ 3.024 (two series
 *     diodes ≡ one with doubled ideality). Effective threshold ≈ 2×Vf_MA856 ≈ 1.64 V →
 *     SOFT clipping.
 *
 * WDF formulation (current-source / diode-root):
 *   The ideal op-amp pins node pin6(−) at 0 V, so the input forces a known current
 *   i_in = Vin / Z_in (Z_in = R9 + 1/sC7) into the feedback — independent of the
 *   (nonlinear) feedback itself. That current drives R10 ∥ [R11 + diode], with the
 *   `DiodePairT` as the nonlinear root:
 *       i_in  →  ResistiveCurrentSource(R10)  →[series]→  R11  →  DiodePairT(root)
 *   Output V(pin7) = voltage across the i_in‖R10 node (passive read; never the source).
 *   SW-1 OFF (diode non-conducting) reduces to V(pin7) = −i_in·R10 = −22·Vin (passband),
 *   i.e. the stock Stage 2 response.
 *
 * Inverting (the −22 sign rides on i_in and the output read). Validated by symmetric soft
 * clipping with onset ≈ 1.64 V in tests/SW1SoftClip_Sine.cpp.
 *
 * The diode root is a `RuntimeDiodePairT` (HQ / Eco lever): `setHighQuality(true)` selects the
 * "Best" eqn-39 solve (production, byte-for-byte identical to the validated voicing); `false`
 * selects the "Good" eqn-18 solve (~half the diode transcendental cost). The class is also
 * templated on `wdft::DiodeQuality` so FeatureProfile can A/B a pure compile-time choice — the
 * template only sets the INITIAL highQ state; production uses the default (`Best`).
 */
template <wdft::DiodeQuality Quality = wdft::DiodeQuality::Best>
class SW1SoftClipT
{
public:
    static constexpr double R9 = 10.0e3;
    static constexpr double R10 = 220.0e3;
    static constexpr double R11 = 6.8e3;
    static constexpr double C7 = 100.0e-9;

    // MA856 (Panasonic), validated — circuit.md Section 16 / dsp.md.
    static constexpr double Is_MA856 = 7.74e-13;
    static constexpr double n_MA856 = 1.512;
    static constexpr double Vt = 25.85e-3;
    static constexpr double n_eff = 2.0 * n_MA856; // ≈ 3.024 (back-to-back 2-diode series)

    SW1SoftClipT() { dp.setHighQuality (Quality == wdft::DiodeQuality::Best); }

    /** HQ / Eco lever: true = Best eqn-39 solve (production), false = Good eqn-18 (~half the cost). */
    void setHighQuality (bool shouldBeHighQ) noexcept { dp.setHighQuality (shouldBeHighQ); }

    void prepare (double sampleRate)
    {
        c7f.prepare (sampleRate);
        reset();
    }

    void reset() { c7f.reset(); }

    /** Process one sample (Volts in → Volts out at pin7). */
    inline double processSample (double x) noexcept
    {
        // 1. Input current into the virtual ground: i_in = Vin / (R9 + 1/sC7).
        vinSrc.setVoltage (x);
        vinSrc.incident (serF.reflected());
        serF.incident (vinSrc.reflected());
        const double iIn = chowdsp::wdft::current<double> (r9f);

        // 2. Drive the nonlinear feedback (R10 ∥ [R11 + diode]); solve the diode root.
        iSrc.setCurrent (iIn);
        dp.incident (fbSeries.reflected());
        fbSeries.incident (dp.reflected());

        // 3. Output V(pin7) = voltage across the i_in‖R10 node (passive read).
        return chowdsp::wdft::voltage<double> (iSrc);
    }

private:
    // ---- Input-current filter: Vin → C7 → R9 → virtual ground; i_in = current(R9) ----
    wdft::ResistorT<double> r9f { R9 };
    wdft::CapacitorT<double> c7f { C7 };
    wdft::WDFSeriesT<double, decltype (c7f), decltype (r9f)> serF { c7f, r9f };
    wdft::IdealVoltageSourceT<double, decltype (serF)> vinSrc { serF };

    // ---- Nonlinear feedback: i_in ‖ R10, series R11, DiodePair root ----
    wdft::ResistiveCurrentSourceT<double> iSrc { R10 }; // current source ‖ R10
    wdft::ResistorT<double> r11 { R11 };
    wdft::WDFSeriesT<double, decltype (r11), decltype (iSrc)> fbSeries { r11, iSrc };
    RuntimeDiodePairT<double, decltype (fbSeries)> dp { fbSeries, Is_MA856, Vt, n_eff };
};

/** Production soft-clip stage: Best-quality diode by default (byte-for-byte unchanged). */
using SW1SoftClip = SW1SoftClipT<>;

} // namespace monarch
