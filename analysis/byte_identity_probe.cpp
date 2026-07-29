// Full-precision channel dump — THE instrument for any claim of the form "this change is
// byte-identical in mode X". Renders the real channel (processPre → processClip → processPost) over a
// signal that exercises every branch, at every clip mode × drive × tone × channel, and writes raw
// doubles. Two builds are then compared with `cmp`, so "byte-identical" means byte-identical rather
// than "agrees to N dB" — which is what several audit items have historically asserted by inspection.
//
// USAGE — build the two arms against different headers and diff them:
//   mkdir -p /tmp/mot_ref/src/dsp && cp src/dsp/*.h /tmp/mot_ref/src/dsp/
//   git show HEAD:src/dsp/MonarchChannel.h > /tmp/mot_ref/src/dsp/MonarchChannel.h
//   clang++ -std=c++17 -O2 -I/tmp/mot_ref -I. -isystem libs/chowdsp_wdf/include \
//       analysis/byte_identity_probe.cpp -o /tmp/dump_ref && /tmp/dump_ref /tmp/ref.bin
//   clang++ -std=c++17 -O2 -I. -isystem libs/chowdsp_wdf/include \
//       analysis/byte_identity_probe.cpp -o /tmp/dump_new && /tmp/dump_new /tmp/new.bin
//   cmp /tmp/ref.bin /tmp/new.bin
// (~1 s per arm — header-only, no JUCE. The -I for the variant tree must come FIRST.)
//
// ⚠️ THE SECOND HALF OF THE FILE IS THE PART THAT EARNS ITS KEEP: mid-stream Boost→OD→Dist mode
// changes. v1.5 step 2 skipped a branch in Boost whose coefficient is a compile-time zero, which is
// byte-identical in every steady-state render — and the ONLY differing byte in the whole 38 MB dump
// was the first sample of the OD segment after a Boost→OD switch, because the skipped branch also
// maintained a 50 ms running mean that OD reads. "Multiplied by zero" is not the same as "dead" when
// the branch maintains state another mode consumes. Never verify byte-identity on per-mode renders
// alone.
//
// When `cmp` reports a differing byte, locate it: sample index = (byte − 1) / 8, then compare against
// the section sizes below (72 static configs × nSamples, then the mode-change runs) to find which
// configuration diverged. That is how the above was diagnosed rather than guessed at.
#include "src/dsp/MonarchChannel.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: dump_probe out.bin\n"); return 2; }
    constexpr double rate = 384000.0; // 8x of 48k — the channel is prepped at the OS rate
    constexpr int n = 1 << 16;

    // Swept, amplitude-modulated: crosses the diode clamps, the rail knees and the identity regions.
    std::vector<double> in ((size_t) n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / rate;
        const double f = 220.0 * std::pow (2.0, 1.5 * std::sin (2.0 * M_PI * 0.7 * t));
        ph += 2.0 * M_PI * f / rate;
        in[(size_t) i] = 0.45 * std::sin (ph) * (0.6 + 0.4 * std::sin (2.0 * M_PI * 1.3 * t));
    }

    std::FILE* f = std::fopen (argv[1], "wb");
    if (f == nullptr) return 2;

    for (int hiGain = 0; hiGain < 2; ++hiGain)
        for (int mode = 0; mode < 3; ++mode)
            for (double drive : { 0.2, 0.5, 0.7, 1.0 })
                for (double tone : { 0.2, 0.5, 0.8 })
                {
                    monarch::MonarchChannel ch { hiGain != 0 };
                    ch.prepareLinear (rate);
                    ch.prepareClip (rate);
                    ch.setDrive (drive);
                    ch.setTone (tone);
                    ch.setVolume (0.5);
                    ch.setPresence (0.0);
                    ch.setClippingMode (mode);
                    for (int i = 0; i < n; ++i)
                    {
                        const double y = ch.processSample (in[(size_t) i]);
                        std::fwrite (&y, sizeof (double), 1, f);
                    }
                }

    // ...and again with a mid-stream mode change Boost→OD→Dist, which is where the skipped
    // `meanSq` update could show up if it mattered.
    for (double drive : { 0.5, 0.8 })
    {
        monarch::MonarchChannel ch { false };
        ch.prepareLinear (rate);
        ch.prepareClip (rate);
        ch.setDrive (drive);
        ch.setTone (0.5);
        ch.setVolume (0.5);
        ch.setPresence (0.0);
        for (int seg = 0; seg < 3; ++seg)
        {
            ch.setClippingMode (seg == 0 ? 0 : (seg == 1 ? 1 : 2));
            for (int i = 0; i < n / 4; ++i)
            {
                const double y = ch.processSample (in[(size_t) (seg * (n / 4) + i)]);
                std::fwrite (&y, sizeof (double), 1, f);
            }
        }
    }

    std::fclose (f);
    return 0;
}
