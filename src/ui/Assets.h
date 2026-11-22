#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

/** Cached accessors for the embedded UI art (assets/ui/, processed from ui/ originals via
 *  tools/process_ui_assets.sh). juce::ImageCache deduplicates repeated calls, so these are
 *  cheap to call from paint().
 */
namespace MonarchAssets
{
inline juce::Image bakeliteKnob() { return juce::ImageCache::getFromMemory (BinaryData::bakelite_knob_png, BinaryData::bakelite_knob_pngSize); }
inline juce::Image presenceKnob() { return juce::ImageCache::getFromMemory (BinaryData::trim_knob_png, BinaryData::trim_knob_pngSize); }
inline juce::Image trimKnob() { return juce::ImageCache::getFromMemory (BinaryData::vol_trim_png, BinaryData::vol_trim_pngSize); }
inline juce::Image ledOff() { return juce::ImageCache::getFromMemory (BinaryData::bezel_led_off_png, BinaryData::bezel_led_off_pngSize); }
inline juce::Image ledYellow() { return juce::ImageCache::getFromMemory (BinaryData::bezel_led_yellow_png, BinaryData::bezel_led_yellow_pngSize); }
inline juce::Image ledRed() { return juce::ImageCache::getFromMemory (BinaryData::bezel_led_red_png, BinaryData::bezel_led_red_pngSize); }
inline juce::Image footswitchUp() { return juce::ImageCache::getFromMemory (BinaryData::footswitch_up_png, BinaryData::footswitch_up_pngSize); }
inline juce::Image footswitchDown() { return juce::ImageCache::getFromMemory (BinaryData::footswitch_down_png, BinaryData::footswitch_down_pngSize); }
inline juce::Image switchUp() { return juce::ImageCache::getFromMemory (BinaryData::switch_up_png, BinaryData::switch_up_pngSize); }
inline juce::Image switchMid() { return juce::ImageCache::getFromMemory (BinaryData::switch_mid_png, BinaryData::switch_mid_pngSize); }
inline juce::Image switchDown() { return juce::ImageCache::getFromMemory (BinaryData::switch_down_png, BinaryData::switch_down_pngSize); }
inline juce::Image texture() { return juce::ImageCache::getFromMemory (BinaryData::mot_texture_jpg, BinaryData::mot_texture_jpgSize); }

/** Per-pixel brightness/contrast grade (straight-alpha maths; alpha untouched). `contrast` and
 *  `brightness` are multipliers around 1.0 (e.g. 1.1 = +10% contrast, 0.9 = -10% brightness).
 */
inline juce::Image adjustBrightnessContrast (const juce::Image& src, float contrast, float brightness)
{
    if (! src.isValid())
        return src;

    juce::Image result (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
    const juce::Image::BitmapData srcData (src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dstData (result, juce::Image::BitmapData::writeOnly);

    const auto grade = [contrast, brightness] (float c)
    {
        c = (c - 0.5f) * contrast + 0.5f;
        return juce::jlimit (0.0f, 1.0f, c * brightness);
    };

    for (int y = 0; y < src.getHeight(); ++y)
    {
        for (int x = 0; x < src.getWidth(); ++x)
        {
            const auto p = srcData.getPixelColour (x, y);
            dstData.setPixelColour (x, y,
                juce::Colour::fromFloatRGBA (grade (p.getFloatRed()), grade (p.getFloatGreen()),
                                             grade (p.getFloatBlue()), p.getFloatAlpha()));
        }
    }
    return result;
}

// Do NOT cache images in function-local or namespace-scope statics. A static outlives the editor and is destroyed when the
// plug-in DLL is unloaded, under the Windows loader lock. JUCE 9's Direct2D images release GPU resources in their destructor,
// and the graphics driver can wait on its own threads, which need that same lock: the host then hangs on closing the plug-in
// (REAPER "Unloading track", Gig Performer; see Tommy#2). The graded art is owned by GradedArt below, shared by the open
// editors through a juce::SharedResourcePointer that the look-and-feel holds, so it is freed when the last editor closes,
// while JUCE and the driver are still alive. The accessors return an Image by value (a cheap reference-counted handle).
// Graded variants — main knobs +10% contrast/-10% brightness, presence trim +15%/-15%, background texture +10% contrast/
// -15% brightness (tweaks 2026-06-22; texture also -12%,+5%,+3%,+5% brightness). Computed once per open-editor lifetime.
struct GradedArt
{
    GradedArt();
    juce::Image bakelite, presence, texture, switchUp, footUp, footDown;
};

inline GradedArt::GradedArt()
    :
      bakelite (adjustBrightnessContrast (MonarchAssets::bakeliteKnob (), 1.10f, 0.90f)),
      presence (adjustBrightnessContrast (MonarchAssets::presenceKnob (), 1.15f, 0.85f)),
      texture (adjustBrightnessContrast (MonarchAssets::texture (), 1.21f, 0.84940857f)),
      switchUp (adjustBrightnessContrast (MonarchAssets::switchUp (), 1.10f, 0.90f)),
      footUp (adjustBrightnessContrast (MonarchAssets::footswitchUp (), 1.0f, 0.90f)),
      footDown (adjustBrightnessContrast (MonarchAssets::footswitchDown (), 1.0f, 0.90f))
{
}

inline juce::Image bakeliteKnobGraded () { return juce::SharedResourcePointer<GradedArt>()->bakelite; }
inline juce::Image presenceKnobGraded () { return juce::SharedResourcePointer<GradedArt>()->presence; }
inline juce::Image textureGraded () { return juce::SharedResourcePointer<GradedArt>()->texture; }
inline juce::Image switchUpGraded () { return juce::SharedResourcePointer<GradedArt>()->switchUp; }
inline juce::Image footswitchUpGraded () { return juce::SharedResourcePointer<GradedArt>()->footUp; }
inline juce::Image footswitchDownGraded () { return juce::SharedResourcePointer<GradedArt>()->footDown; }
} // namespace MonarchAssets
