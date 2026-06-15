#pragma once

namespace monarch
{

/** Stub for a single King of Tone channel's DSP chain. WDF implementation begins at Step 4. */
class MonarchChannel
{
public:
    MonarchChannel() = default;

    void prepare (double /*sampleRate*/, int /*samplesPerBlock*/) {}

    void reset() {}

    float process (float input)
    {
        return input;
    }
};

} // namespace monarch
