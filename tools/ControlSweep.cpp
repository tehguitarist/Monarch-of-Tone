// Step 10 — final full-range control sweep / stability gate.
//
// Drives the FULL stereo MonarchAudioProcessor (both channels active, Yellow -> Red in series)
// through every control across its entire range, every clipping-mode combo, every oversampling
// factor, live OS changes, bypass crossfades, and instantaneous knob jumps — while a continuous
// guitar-level signal plays. Asserts the output is always finite and bounded (no NaN/Inf, no
// runaway), and quantifies the worst sample-to-sample step during continuous knob sweeps vs a
// static reference (zipper/click check). No DAW or display needed.
//
//   ControlSweep            (build target: ControlSweep)

#include <juce_audio_utils/juce_audio_utils.h>

#include "../src/PluginProcessor.h"

#include <cmath>
#include <cstdio>

using namespace juce;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 64;          // small block => most frequent per-block param updates (worst case for zipper)
constexpr double inFreq = 220.0;   // A3, in the guitar range
constexpr double BOUND = 100.0;    // |out| ceiling in FS (Boost+max trim legitimately reaches ~15); >this = runaway

void setP (AudioProcessorValueTreeState& a, const char* id, float v)
{
    if (auto* p = a.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

struct Stats
{
    double maxAbs = 0.0, maxDelta = 0.0, firstHalfMax = 0.0, secondHalfMax = 0.0;
    long nanCount = 0;
    bool finiteOk() const { return nanCount == 0; }
    bool boundedOk() const { return maxAbs < BOUND; }
    // stable = energy not monotonically blowing up across the phase
    bool stableOk() const { return secondHalfMax < firstHalfMax * 4.0 + 1.0e-6; }
};

// A modulator the caller advances each block; returns a 0..1 ramp over `blocks` then holds at end.
struct Ramp
{
    int n = 0, total = 1;
    float next() { const float t = total > 1 ? (float) n / (float) (total - 1) : 1.0f; if (n < total - 1) ++n; return jlimit (0.0f, 1.0f, t); }
};
} // namespace

// Run `seconds` of audio through `proc`, calling `perBlock(blockIndex)` before each block to set
// params; accumulate stats on the stereo output. phaseHalf marks where the "second half" starts.
static Stats runPhase (MonarchAudioProcessor& proc, double seconds, std::function<void (int)> perBlock)
{
    const int totalSamples = (int) (seconds * fs);
    const int numBlocks = (totalSamples + block - 1) / block;
    Stats s;
    MidiBuffer midi;
    double phase = 0.0;
    const double dphi = 2.0 * MathConstants<double>::pi * inFreq / fs;
    static double prevOut = 0.0; // continuity across blocks within a phase
    prevOut = 0.0;
    for (int b = 0; b < numBlocks; ++b)
    {
        if (perBlock)
            perBlock (b);
        AudioBuffer<float> buf (2, block);
        for (int n = 0; n < block; ++n)
        {
            const float x = 0.6f * (float) std::sin (phase); // ~ hot single-coil level
            phase += dphi;
            buf.setSample (0, n, x);
            buf.setSample (1, n, x);
        }
        proc.processBlock (buf, midi);
        const bool secondHalf = b >= numBlocks / 2;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int n = 0; n < block; ++n)
            {
                const double y = d[n];
                if (! std::isfinite (y))
                {
                    ++s.nanCount;
                    continue;
                }
                const double a = std::abs (y);
                s.maxAbs = std::max (s.maxAbs, a);
                if (ch == 0)
                {
                    s.maxDelta = std::max (s.maxDelta, std::abs (y - prevOut));
                    prevOut = y;
                    (secondHalf ? s.secondHalfMax : s.firstHalfMax) =
                        std::max (secondHalf ? s.secondHalfMax : s.firstHalfMax, a);
                }
            }
        }
    }
    return s;
}

int main()
{
    const ScopedJuceInitialiser_GUI guiInit;
    MonarchAudioProcessor proc;
    auto& apvts = proc.apvts;

    auto prep = [&] (bool render)
    {
        proc.setPlayConfigDetails (2, 2, fs, block);
        proc.setNonRealtime (render);
        proc.prepareToPlay (fs, block);
    };

    // Knobs we sweep continuously (both channels mirrored unless noted).
    struct Knob { const char* y; const char* r; const char* label; float lo, hi; };
    const Knob knobs[] = {
        { "drive_yellow", "drive_red", "drive", 0.0f, 1.0f },
        { "tone_yellow", "tone_red", "tone", 0.0f, 1.0f },
        { "volume_yellow", "volume_red", "volume", 0.0f, 1.0f },
        { "presence_yellow", "presence_red", "presence", 0.0f, 1.0f },
        { "input_trim", nullptr, "input_trim", -12.0f, 12.0f },
        { "output_trim", nullptr, "output_trim", -12.0f, 12.0f },
    };

    bool allFinite = true, allBounded = true, allStable = true;
    double worstSweepDelta = 0.0, worstStaticDelta = 0.0, worstBypassDelta = 0.0, worstJumpDelta = 0.0;
    double worstAbs = 0.0;
    long totalNan = 0;

    // `steady` = the phase holds its level settings constant, so the within-phase first/second-half
    // energy ratio is a valid runaway check. Level SWEEPS (volume/trim ramps) legitimately grow
    // across the phase, so the stability heuristic is only applied when steady.
    auto note = [&] (const char* name, const Stats& s, double* deltaSink = nullptr, bool steady = false)
    {
        allFinite &= s.finiteOk();
        allBounded &= s.boundedOk();
        if (steady)
            allStable &= s.stableOk();
        totalNan += s.nanCount;
        worstAbs = std::max (worstAbs, s.maxAbs);
        if (deltaSink)
            *deltaSink = std::max (*deltaSink, s.maxDelta);
        std::printf ("  %-34s maxAbs %7.3f  maxDelta %7.4f  nan %ld  %s%s%s\n",
                     name, s.maxAbs, s.maxDelta, s.nanCount,
                     s.finiteOk() ? "" : "[NaN] ", s.boundedOk() ? "" : "[UNBOUNDED] ",
                     (steady && ! s.stableOk()) ? "[UNSTABLE]" : "");
    };

    // Baseline: all controls mid, OD/OD, 4x realtime. Reference for the zipper comparison.
    prep (false);
    setP (apvts, "bypass_yellow", 0.0f);
    setP (apvts, "bypass_red", 0.0f);
    setP (apvts, "clipping_mode_yellow", 1.0f);
    setP (apvts, "clipping_mode_red", 1.0f);
    setP (apvts, "oversampling_realtime", 2.0f); // 4x
    for (auto& k : knobs)
    {
        setP (apvts, k.y, k.label[0] == 'i' || k.label[0] == 'o' ? 0.0f : 0.5f);
        if (k.r) setP (apvts, k.r, 0.5f);
    }
    std::printf ("== Static baseline (mid knobs, OD/OD, 4x) ==\n");
    note ("static", runPhase (proc, 0.5, nullptr), &worstStaticDelta, true);

    // Phase 1: each knob swept 0->1 continuously, for every clipping-mode combo, with the OTHER
    // gain controls pinned HOT (drive=1, vol=1) to stress the worst case. Hot input throughout.
    std::printf ("\n== Continuous knob sweeps x all clip-mode combos (hot neighbours) ==\n");
    for (int cy = 0; cy < 3; ++cy)
        for (int cr = 0; cr < 3; ++cr)
        {
            setP (apvts, "clipping_mode_yellow", (float) cy);
            setP (apvts, "clipping_mode_red", (float) cr);
            for (auto& k : knobs)
            {
                // pin neighbours hot
                setP (apvts, "drive_yellow", 1.0f); setP (apvts, "drive_red", 1.0f);
                setP (apvts, "volume_yellow", 1.0f); setP (apvts, "volume_red", 1.0f);
                setP (apvts, "tone_yellow", 0.5f); setP (apvts, "tone_red", 0.5f);
                setP (apvts, "presence_yellow", 1.0f); setP (apvts, "presence_red", 1.0f);
                setP (apvts, "input_trim", 0.0f); setP (apvts, "output_trim", 0.0f);
                Ramp ramp; ramp.total = (int) (0.4 * fs / block);
                auto sweep = [&] (int)
                {
                    const float t = ramp.next();
                    const float v = k.lo + t * (k.hi - k.lo);
                    setP (apvts, k.y, v);
                    if (k.r) setP (apvts, k.r, v);
                };
                char nm[64]; std::snprintf (nm, sizeof nm, "%s sweep [cy%d cr%d]", k.label, cy, cr);
                note (nm, runPhase (proc, 0.4, sweep), &worstSweepDelta);
            }
        }

    // Phase 2: oversampling factors, static hot, plus a LIVE factor change every block.
    std::printf ("\n== Oversampling factors (static + live change) ==\n");
    setP (apvts, "clipping_mode_yellow", 2.0f); setP (apvts, "clipping_mode_red", 1.0f);
    setP (apvts, "drive_yellow", 1.0f); setP (apvts, "drive_red", 1.0f);
    setP (apvts, "volume_yellow", 0.6f); setP (apvts, "volume_red", 0.6f);
    for (int os = 0; os < 4; ++os)
    {
        setP (apvts, "oversampling_realtime", (float) os);
        char nm[32]; std::snprintf (nm, sizeof nm, "OS=%dx static", 1 << os);
        note (nm, runPhase (proc, 0.25, nullptr), &worstStaticDelta, true);
    }
    {
        auto flip = [&] (int b) { setP (apvts, "oversampling_realtime", (float) (b % 4)); };
        note ("OS live-change each block", runPhase (proc, 0.5, flip), &worstSweepDelta);
    }
    setP (apvts, "oversampling_realtime", 2.0f);

    // Phase 3: bypass crossfade clicks — toggle each channel's bypass every ~30 ms.
    std::printf ("\n== Bypass crossfade toggling ==\n");
    {
        const int togBlocks = (int) (0.03 * fs / block);
        auto tog = [&] (int b)
        {
            const bool on = (b / togBlocks) % 2 == 0;
            setP (apvts, "bypass_yellow", on ? 1.0f : 0.0f);
            setP (apvts, "bypass_red", on ? 0.0f : 1.0f);
        };
        note ("bypass toggle Y/R", runPhase (proc, 1.0, tog), &worstBypassDelta);
        setP (apvts, "bypass_yellow", 0.0f); setP (apvts, "bypass_red", 0.0f);
    }

    // Phase 4: instantaneous knob JUMPS (0->1->0 in single blocks) — worst-case zipper.
    std::printf ("\n== Instantaneous knob jumps (worst-case zipper) ==\n");
    setP (apvts, "clipping_mode_yellow", 1.0f); setP (apvts, "clipping_mode_red", 1.0f);
    for (auto& k : knobs)
    {
        int step = 0;
        auto jump = [&] (int b)
        {
            // jump every 8 blocks between lo and hi
            const float v = ((b / 8) % 2 == 0) ? k.lo : k.hi;
            setP (apvts, k.y, v);
            if (k.r) setP (apvts, k.r, v);
            (void) step;
        };
        char nm[64]; std::snprintf (nm, sizeof nm, "%s jump 0<->full", k.label);
        note (nm, runPhase (proc, 0.4, jump), &worstJumpDelta);
        setP (apvts, k.y, 0.5f); if (k.r) setP (apvts, k.r, 0.5f);
    }

    // Phase 5: render path (8x FIR) sanity — one hot pass per clip mode.
    std::printf ("\n== Render path (non-realtime, 8x FIR) ==\n");
    prep (true);
    setP (apvts, "bypass_yellow", 0.0f); setP (apvts, "bypass_red", 0.0f);
    setP (apvts, "drive_yellow", 0.9f); setP (apvts, "drive_red", 0.9f);
    for (int c = 0; c < 3; ++c)
    {
        setP (apvts, "clipping_mode_yellow", (float) c);
        setP (apvts, "clipping_mode_red", (float) c);
        char nm[32]; std::snprintf (nm, sizeof nm, "render clip=%d", c);
        note (nm, runPhase (proc, 0.25, nullptr), &worstStaticDelta, true);
    }

    // Verdict.
    std::printf ("\n== SUMMARY ==\n");
    std::printf ("  worst |out|              %.3f  (bound %.0f)\n", worstAbs, BOUND);
    std::printf ("  total non-finite samples %ld\n", totalNan);
    std::printf ("  maxDelta static ref      %.4f\n", worstStaticDelta);
    std::printf ("  maxDelta knob sweeps     %.4f  (continuous automation)\n", worstSweepDelta);
    std::printf ("  maxDelta bypass toggles  %.4f  (crossfade)\n", worstBypassDelta);
    std::printf ("  maxDelta instant jumps   %.4f  (worst-case step automation)\n", worstJumpDelta);

    // Note on the delta figures: continuous knob sweeps run with HOT neighbours through hard-clip
    // modes, so their maxDelta is dominated by the distorted signal's own fast edges, not zipper.
    // The real zipper signal is the INSTANTANEOUS single-block jump: that is bounded by the per-block
    // parameter step (volume is read raw per block — no smoothing), and is the worst case a DAW
    // automation step could produce. Reported for visibility; the hard gate is finite+bounded+stable.
    std::printf ("\n  finite: %s | bounded: %s | stable(steady): %s\n",
                 allFinite ? "ok" : "FAIL", allBounded ? "ok" : "FAIL", allStable ? "ok" : "FAIL");

    const bool pass = allFinite && allBounded && allStable;
    std::printf ("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
