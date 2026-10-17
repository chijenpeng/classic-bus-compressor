#include "PluginEditor.h"

namespace
{
/** A 1 kHz tone at -6 dBFS through the plug-in, so the meters have something to show. */
void runTone (SslBusProcessor& processor, double sampleRate, int block, double seconds)
{
    double phase = 0.0;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (int done = 0; done < static_cast<int> (seconds * sampleRate); done += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const auto v = 0.5f * static_cast<float> (std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        processor.processBlock (buffer, midi);
    }
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    SslBusProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    runTone (processor, 48000.0, 512, 1.5);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    for (int i = 0; i < 60; ++i)
        static_cast<SslBusEditor&> (*editor).updateMeters();
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const juce::File out (argc > 1 ? juce::String::fromUTF8 (argv[1]) : "build/editor.png");
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::PNGImageFormat png;
    auto stream = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream());
    return stream != nullptr && png.writeImageToStream (image, *stream) ? 0 : 1;
}
