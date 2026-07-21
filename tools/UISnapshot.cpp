// Headless UI snapshot: renders the plugin editor to a PNG without a display, for visual
// verification during UI development. Build target `UISnapshot`; writes /tmp/monarch_ui.png.
//
//   cmake --build build --target UISnapshot && ./build/UISnapshot [scale]
//
// Uses JUCE's software image renderer (Graphics over an Image) + paintEntireComponent, so it
// needs no window server / screen-recording permission.

#include <juce_gui_extra/juce_gui_extra.h>

#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

using namespace juce;

int main (int argc, char* argv[])
{
    const ScopedJuceInitialiser_GUI guiInit;

    const float scale = (argc > 1) ? jlimit (0.5f, 2.5f, (float) String (argv[1]).getDoubleValue()) : 2.0f;

    MonarchAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<AudioProcessorEditor> editor (proc.createEditor());
    if (editor == nullptr)
    {
        std::fprintf (stderr, "no editor\n");
        return 1;
    }
    // Must match MonarchAudioProcessorEditor::kBaseW/kBaseH (they're private) — setSize bypasses
    // the aspect-lock constrainer, so a stale base here silently renders a distorted layout.
    editor->setSize (roundToInt (612 * scale), roundToInt (354 * scale));

    Image img (Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    {
        Graphics g (img);
        editor->paintEntireComponent (g, false);
    }

    File out ("/tmp/monarch_ui.png");
    out.deleteFile();
    if (auto os = out.createOutputStream())
    {
        PNGImageFormat png;
        png.writeImageToStream (img, *os);
        std::printf ("wrote %s (%dx%d @ %.2fx)\n", out.getFullPathName().toRawUTF8(),
                     img.getWidth(), img.getHeight(), scale);
        return 0;
    }
    std::fprintf (stderr, "could not write png\n");
    return 1;
}
