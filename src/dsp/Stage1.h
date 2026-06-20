#pragma once

#include <algorithm>
#include <cmath>

#include <chowdsp_wdf/chowdsp_wdf.h>

namespace monarch
{
namespace wdft = chowdsp::wdft;

/**
 * Stage 1 — IC_A, non-inverting gain stage (linear WDF).
 *
 * Topology (circuit.md §6, **Theseus schematic + parts list, 2026-06-20**):
 *   - Input network: Vin → C1(22n) → pin3(+); R1(1M) pin3(+)→BIAS — a first-order high-pass
 *     producing V(pin3+). pin3(+) is the high-impedance non-inverting input, decoupled from
 *     the feedback. (RPD1 1M input pulldown sits across the ideal source → no effect; omitted.)
 *   - Gain stage: ideal op-amp, Av(s) = 1 + Z_upper(s)/Z_lower(s), DC gain = 1.
 *       Z_lower (NodeF→GND) = **C4(10n) in series with [ R4(27k) ∥ (R5(33k) + C3(10n)) ]**.
 *         (C4 is a SINGLE shared series cap to ground — C3 reaches ground only through C4. This
 *         is the corrected real-KOT topology, NOT the old matsumin two-branch (R+C)∥(R+C).)
 *       Z_upper (NodeF→NodeG) = (floor + DRIVE) ∥ C2(100pF), floor = R2∥R3 (Yellow/stock) or
 *         R2 (Red/Hi-Gain) — the SW1B Hi-Gain switch.
 *
 * WDF formulation (exact for an ideal op-amp; no R-type scattering matrix needed):
 *   The op-amp pins NodeF at V(pin3+), so the current Z_lower draws is i = V(pin3+)/Z_lower —
 *   independent of Z_upper. That SAME current flows through Z_upper (no current into the ideal
 *   input), so V(NodeG) = V(pin3+) + i·Z_upper. Hence two decoupled one-port solves:
 *     (1) drive Z_lower with an ideal VOLTAGE source = V(pin3+) → read its current i;
 *     (2) drive Z_upper with an ideal CURRENT source = i      → read its voltage = i·Z_upper.
 *   Output = V(pin3+) + i·Z_upper. Both readouts are off PASSIVE elements (never a source port,
 *   which would 2-point-average → a spurious HF droop). Non-inverting → no PolarityInverterT.
 */
class Stage1
{
public:
    // Stage-1 feedback floor (Z_upper series R). Theseus R2(100k) with SW1B switching R3(1k) in
    // parallel (the Hi-Gain switch). Stock (Yellow, SW1B closed) = R2∥R3 ≈ 0.99k; Hi-Gain (Red,
    // SW1B open) = R2 = 100k. Parts-list confirmed (analysis/theseus_kit_documentation.pdf p29).
    static constexpr double R2 = 100.0e3;
    static constexpr double R3 = 1.0e3;
    static constexpr double R6_floor = (R2 * R3) / (R2 + R3); // ≈ 990 Ω — Yellow (stock)
    static constexpr double HiGain_floor = R2;                // 100 k — Red (Hi-Gain)
    static constexpr double DRIVE_max = 100.0e3;              // DRIVE 100kB linear

    explicit Stage1 (bool hiGain = false) : floorR (hiGain ? HiGain_floor : R6_floor) { setDrive (0.5); }

    void prepare (double sampleRate)
    {
        cIn.prepare (sampleRate);
        c3.prepare (sampleRate);
        c4.prepare (sampleRate);
        c2.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        cIn.reset();
        c3.reset();
        c4.reset();
        c2.reset();
    }

    /** DRIVE in [0,1], linear (100kB taper applied here). */
    void setDrive (double drive01)
    {
        const auto rDrive = DRIVE_max * std::min (1.0, std::max (0.0, drive01));
        driveR.setResistanceValue (floorR + rDrive); // propagates to the Z_upper current-source root
    }

    /** Process one sample (Volts in → Volts out at NodeG). Stage 1 is linear. */
    inline double processSample (double x) noexcept
    {
        // 1. Input high-pass sub-filter → V(pin3+) = V across R1.
        vin.setVoltage (x);
        vin.incident (inputInv.reflected());
        inputInv.incident (vin.reflected());
        const double vPlus = chowdsp::wdft::voltage<double> (rBias);

        // 2. Z_lower driven by V(pin3+); i = current it draws (series-root cap carries the total).
        zlSrc.setVoltage (vPlus);
        zlSrc.incident (zlower.reflected());
        zlower.incident (zlSrc.reflected());
        // current() sign convention is opposite the physical NodeF→GND current; negate so the
        // same current drives Z_upper in phase → V(NodeG) = vPlus + i·Z_upper (not vPlus − …).
        const double iLower = -chowdsp::wdft::current<double> (c4);

        // 3. Same current through Z_upper → V(NodeG) = V(pin3+) + i·Z_upper (passive voltage read).
        zuSrc.setCurrent (iLower);
        zuSrc.incident (zupper.reflected());
        zupper.incident (zuSrc.reflected());
        const double vZupper = chowdsp::wdft::voltage<double> (driveR);

        return vPlus + vZupper;
    }

private:
    double floorR; // R6_floor (Yellow) or HiGain_floor (Red), set in ctor

    // ---- Input high-pass: Vin → C1(22n) → [R1(1M) → BIAS]; read V across R1 = V(pin3+) ----
    wdft::CapacitorT<double> cIn { 22.0e-9 };
    wdft::ResistorT<double> rBias { 1.0e6 };
    wdft::WDFSeriesT<double, decltype (cIn), decltype (rBias)> inputSeries { cIn, rBias };
    wdft::PolarityInverterT<double, decltype (inputSeries)> inputInv { inputSeries };
    wdft::IdealVoltageSourceT<double, decltype (inputInv)> vin { inputInv };

    // ---- Z_lower one-port: C4(10n) series with [ R4(27k) ∥ (R5(33k)+C3(10n)) ], NodeF→GND ----
    wdft::ResistorT<double> r5 { 33.0e3 };
    wdft::CapacitorT<double> c3 { 10.0e-9 };
    wdft::WDFSeriesT<double, decltype (r5), decltype (c3)> r5c3 { r5, c3 };
    wdft::ResistorT<double> r4 { 27.0e3 };
    wdft::WDFParallelT<double, decltype (r4), decltype (r5c3)> zlPar { r4, r5c3 };
    wdft::CapacitorT<double> c4 { 10.0e-9 };
    wdft::WDFSeriesT<double, decltype (c4), decltype (zlPar)> zlower { c4, zlPar };
    wdft::IdealVoltageSourceT<double, decltype (zlower)> zlSrc { zlower };

    // ---- Z_upper one-port: (floor + DRIVE) ∥ C2(100pF), NodeF→NodeG, driven by the i above ----
    wdft::ResistorT<double> driveR { R6_floor + 0.5 * DRIVE_max };
    wdft::CapacitorT<double> c2 { 100.0e-12 };
    wdft::WDFParallelT<double, decltype (driveR), decltype (c2)> zupper { driveR, c2 };
    wdft::IdealCurrentSourceT<double, decltype (zupper)> zuSrc { zupper };
};

} // namespace monarch
