// TrimLock — headless acceptance test for the input/output trim LOCK coupling.
//
// Drives the two trim parameters through APVTS with the editor alive, so the real path runs:
//   param->setValueNotifyingHost() → SliderParameterAttachment → slider onValueChange → mirrorTrim.
// Checks the delta-linked contract: the lock mirrors the CHANGE (preserving the pair's offset),
// never snaps on enable, and clamps at the ±18 dB rails.
//
//   cmake --build build --target TrimLock && ./build/TrimLock

#include <juce_gui_extra/juce_gui_extra.h>

#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

using namespace juce;

namespace
{
constexpr double kTol = 1.0e-3;
int failures = 0;

struct Rig
{
    MonarchAudioProcessor proc;
    std::unique_ptr<AudioProcessorEditor> editor { proc.createEditor() };

    RangedAudioParameter* param (const String& id) const
    {
        return dynamic_cast<RangedAudioParameter*> (proc.apvts.getParameter (id));
    }

    void set (const String& id, float dB) const
    {
        auto* p = param (id);
        p->setValueNotifyingHost (p->convertTo0to1 (dB));
    }

    double get (const String& id) const { return (double) param (id)->convertFrom0to1 (param (id)->getValue()); }

    void setLock (bool on) const { param ("trim_lock")->setValueNotifyingHost (on ? 1.0f : 0.0f); }

    // Positions both trims with the lock OFF — resetting a knob while locked would itself mirror
    // (correct behaviour, but it would corrupt the case's setup).
    void arm (float in, float out) const
    {
        setLock (false);
        set ("input_trim", in);
        set ("output_trim", out);
        setLock (true);
    }
};

void check (const String& what, double got, double want)
{
    const bool ok = std::abs (got - want) < kTol;
    if (! ok)
        ++failures;
    std::printf ("%-58s got %+7.3f  want %+7.3f   %s\n", what.toRawUTF8(), got, want, ok ? "PASS" : "FAIL");
}

void checkPair (const String& what, const Rig& r, double wantIn, double wantOut)
{
    check (what + " [in]", r.get ("input_trim"), wantIn);
    check (what + " [out]", r.get ("output_trim"), wantOut);
}
} // namespace

int main()
{
    const ScopedJuceInitialiser_GUI guiInit;

    Rig r;
    if (r.editor == nullptr)
    {
        std::fprintf (stderr, "no editor\n");
        return 1;
    }

    // Range must be ±18 dB on both trims.
    for (const char* id : { "input_trim", "output_trim" })
    {
        const auto range = r.param (id)->getNormalisableRange();
        check (String (id) + " range start", range.start, -18.0);
        check (String (id) + " range end", range.end, 18.0);
    }

    // A fresh instance defaults to locked.
    check ("trim_lock defaults ON", r.param ("trim_lock")->getValue(), 1.0);

    // Row 1 — 0/0, +3 on input → +3/−3.
    r.arm (0.0f, 0.0f);
    r.set ("input_trim", 3.0f);
    checkPair ("0/0  +3 input", r, 3.0, -3.0);

    // Row 2 — +3/0, a further +3 on input → +6/−3 (the existing offset is preserved).
    r.arm (3.0f, 0.0f);
    r.set ("input_trim", 6.0f);
    checkPair ("+3/0  +3 input", r, 6.0, -3.0);

    // Row 3 — 0/0, −4 on output → +4/−4 (mirroring works from either side).
    r.arm (0.0f, 0.0f);
    r.set ("output_trim", -4.0f);
    checkPair ("0/0  -4 output", r, 4.0, -4.0);

    // Row 4 — +5/+2, enable the lock with no knob move → no snap.
    r.arm (5.0f, 2.0f);
    checkPair ("+5/+2  enable lock, no move", r, 5.0, 2.0);

    // Row 5 — 0/0, +18 on input → +18/−18, the mirror clamped at the rail.
    r.arm (0.0f, 0.0f);
    r.set ("input_trim", 18.0f);
    checkPair ("0/0  +18 input (clamp)", r, 18.0, -18.0);

    // Lock OFF leaves the knobs fully independent.
    r.arm (0.0f, 0.0f);
    r.setLock (false);
    r.set ("input_trim", 7.0f);
    checkPair ("lock OFF  +7 input (independent)", r, 7.0, 0.0);

    std::printf ("\nTrimLock: %s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
