#pragma once

#include <algorithm>
#include <cmath>

#include "Stage1.h"
#include "Stage2.h"
#include "SW1SoftClip.h"
#include "SW2HardClip.h"
#include "ToneStage.h"
#include "VolumePot.h"

namespace monarch
{

/**
 * A single King of Tone channel (Yellow or Red), full DSP chain.
 *
 * Signal path (circuit.md Section 13):
 *   in → Stage1 (IC_A, non-inv; incl. input network; fixed Hi-Gain on Red) → V(NodeG)
 *      → Stage2 (IC_B, inv ×−22)   — SW-1 OFF: stock Stage2; SW-1 ON: SW1SoftClip (soft clip)
 *      → op-amp rail saturation (±3.3 V soft knee). Models the JRC4580 output ceiling. It is
 *        load-bearing wherever the op-amp swing would exceed the rails: ALWAYS in Boost (no
 *        diodes) and in Distortion (the linear Stage2 ×−22 path reaches ~13.9 V before the
 *        hard-clip shunt), and at extreme drive in OD/Both. Tone-safe at normal levels: the
 *        feedback soft-clip (OD/Both) holds pin7 well below ±3 V, so the knee passes it
 *        unchanged there; it only ever clamps a swing the real op-amp would also clamp.
 *      → R12/node_HC            — SW-2 OFF: pass-through (R12 loading minor, circuit.md §10);
 *                                 SW-2 ON: SW2HardClip (R12 + 1S1588 shunt, hard clip)
 *      → ToneStage (passive TONE/Presence) → VolumePot (audio taper + C11/R14) → out
 *
 * Clipping mode (architecture.md): 0 Boost(—/—), 1 Overdrive(SW1/—), 2 Distortion(—/SW2),
 * 3 Both(SW1/SW2). Hi Gain is fixed per channel (ctor flag → Stage1), not a runtime mode.
 */
class MonarchChannel
{
public:
    // ±3.3 V op-amp output ceiling around BIAS (JRC4580 on a 9 V rail) — circuit.md §4, dsp.md.
    static constexpr double railV = 3.3;
    static constexpr double railKnee = 3.0; // linear below the knee → diode modes untouched

    explicit MonarchChannel (bool hiGain = false) : stage1 (hiGain), hiGainStage1 (hiGain) {}

    // The linear stages run at the base rate; the nonlinear clip span (Stage2/SW1 + rail-sat
    // + SW2) runs at the OVERSAMPLED rate, so the OS factor changes only the anti-aliasing,
    // never the linear voicing. Prepare the two groups independently; re-prepare just the clip
    // group when the OS factor changes. For standalone/1x use, baseRate == clipRate.
    void prepareLinear (double baseRate)
    {
        stage1.prepare (baseRate);
        tone.prepare (baseRate);
        volume.prepare (baseRate);
    }

    void prepareClip (double clipRate)
    {
        stage2.prepare (clipRate);
        sw1.prepare (clipRate);
        sw2.prepare (clipRate);
    }

    void prepare (double sampleRate, int /*samplesPerBlock*/ = 0)
    {
        prepareLinear (sampleRate);
        prepareClip (sampleRate);
    }

    void reset()
    {
        stage1.reset();
        stage2.reset();
        sw1.reset();
        sw2.reset();
        tone.reset();
        volume.reset();
    }

    // ---- Parameter setters (call per block; tapers applied inside each stage) ----
    void setDrive (double d) { stage1.setDrive (d); }
    void setTone (double t) { tone.setTone (t); }
    void setPresence (double p) { tone.setPresence (p); }
    void setVolume (double v) { volume.setVolume (v); }

    /** Clipping mode 0..3 (Boost/Overdrive/Distortion/Both → SW-1/SW-2 on/off). */
    void setClippingMode (int mode)
    {
        sw1On = (mode == 1 || mode == 3);
        sw2On = (mode == 2 || mode == 3);
    }

    // Base-rate front: input network + Stage 1 → V(NodeG).
    inline double processPre (double x) noexcept { return stage1.processSample (x); }

    // Oversampled nonlinear span: Stage2 (or SW1 soft clip) → op-amp rail-sat → SW2 (or pass)
    // → V(node_HC). This is the ONLY part that should run at the oversampled rate.
    inline double processClip (double nodeG) noexcept
    {
        double pin7 = sw1On ? sw1.processSample (nodeG) : stage2.processSample (nodeG);
        pin7 = railSaturate (pin7); // op-amp output ceiling (Boost always; Distortion via linear Stage2)
        return sw2On ? sw2.processSample (pin7) : pin7;
    }

    // Base-rate back: Tone → Volume → output.
    inline double processPost (double nodeHC) noexcept
    {
        return volume.processSample (tone.processSample (nodeHC));
    }

    /** Full chain at a single rate (standalone / tests / 1x). */
    inline double processSample (double x) noexcept { return processPost (processClip (processPre (x))); }

    bool isHiGain() const { return hiGainStage1; }

private:
    // Soft op-amp rail saturation: linear below ±railKnee, gentle tanh knee approaching ±railV.
    // Below the knee the signal passes UNCHANGED, so it never colours the feedback soft-clip's
    // sub-3 V output at normal drive. It clamps only swings the real op-amp would also clamp:
    // Boost (no diodes) and Distortion's linear-Stage2 ×−22 path always, OD/Both at extreme drive.
    static inline double railSaturate (double v) noexcept
    {
        const double a = std::abs (v);
        if (a <= railKnee)
            return v;
        const double over = (a - railKnee) / (railV - railKnee);
        const double clamped = railKnee + (railV - railKnee) * std::tanh (over);
        return std::copysign (clamped, v);
    }

    Stage1 stage1;     // includes the fixed Hi-Gain selection for Red
    Stage2 stage2;     // stock inverting Stage 2 (SW-1 OFF path)
    SW1SoftClip sw1;   // Stage 2 with soft-clip diodes (SW-1 ON path)
    SW2HardClip sw2;   // R12 + 1S1588 hard-clip shunt (SW-2 ON path)
    ToneStage tone;
    VolumePot volume;

    bool sw1On { true };  // default Overdrive (SW-1 ON, SW-2 OFF)
    bool sw2On { false };
    bool hiGainStage1 { false };
};

} // namespace monarch
