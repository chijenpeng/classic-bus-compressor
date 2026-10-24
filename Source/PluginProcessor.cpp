#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
juce::StringArray ratioNames() { return { "2:1", "4:1", "10:1" }; }
juce::StringArray attackNames() { return { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }; }
juce::StringArray releaseNames() { return { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "AUTO" }; }
juce::StringArray filterNames() { return { "OFF", "30 Hz", "60 Hz", "105 Hz", "125 Hz", "185 Hz" }; }
}

SslBusProcessor::SslBusProcessor()
    : juce::AudioProcessor (juce::AudioProcessor::BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SslBusState", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout SslBusProcessor::createParameterLayout()
{
    using ID = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add (std::make_unique<juce::AudioParameterBool> (ID { ParamID::in, 1 }, "Compressor In", true));
    p.add (std::make_unique<juce::AudioParameterFloat> (ID { ParamID::threshold, 1 }, "Threshold",
        juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f }, 50.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    p.add (std::make_unique<juce::AudioParameterChoice> (ID { ParamID::ratio, 1 }, "Ratio", ratioNames(), 1));
    p.add (std::make_unique<juce::AudioParameterChoice> (ID { ParamID::attack, 1 }, "Attack", attackNames(), 5));
    p.add (std::make_unique<juce::AudioParameterChoice> (ID { ParamID::release, 1 }, "Release", releaseNames(), 4));
    p.add (std::make_unique<juce::AudioParameterFloat> (ID { ParamID::makeup, 1 }, "Make Up",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    p.add (std::make_unique<juce::AudioParameterChoice> (ID { ParamID::sidechainFilter, 1 }, "S/C HPF", filterNames(), 0));
    p.add (std::make_unique<juce::AudioParameterBool> (ID { ParamID::externalSidechain, 1 }, "External S/C", false));
    p.add (std::make_unique<juce::AudioParameterFloat> (ID { ParamID::mix, 1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f }, 100.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    p.add (std::make_unique<juce::AudioParameterFloat> (ID { ParamID::alignment, 1 }, "0 dBFS Alignment",
        juce::NormalisableRange<float> { 12.0f, 30.0f, 0.5f }, 22.0f, juce::AudioParameterFloatAttributes().withLabel ("dBu")));
    return p;
}

void SslBusProcessor::prepareToPlay (double sampleRate, int)
{
    compressor.setControls (readControls());
    compressor.prepare (sampleRate);
    gainReduction.store (0.0f);
    inputPeak.store (0.0f);
    outputPeak.store (0.0f);
}

bool SslBusProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto main = layout.getMainInputChannelSet();
    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        return false;
    if (layout.getMainOutputChannelSet() != main)
        return false;
    const auto side = layout.inputBuses.size() > 1 ? layout.inputBuses[1] : juce::AudioChannelSet::disabled();
    return side.isDisabled() || side == juce::AudioChannelSet::mono() || side == juce::AudioChannelSet::stereo();
}

sslbus::Controls SslBusProcessor::readControls() const noexcept
{
    auto value = [this] (const char* id) { return parameters.getRawParameterValue (id)->load (std::memory_order_relaxed); };
    sslbus::Controls c;
    c.enabled = value (ParamID::in) >= 0.5f;
    c.thresholdPosition = value (ParamID::threshold) * 0.01f;
    c.ratio = static_cast<int> (value (ParamID::ratio));
    c.attack = static_cast<int> (value (ParamID::attack));
    c.release = static_cast<int> (value (ParamID::release));
    c.makeupPosition = value (ParamID::makeup) * 0.01f;
    c.sidechainHighPass = static_cast<int> (value (ParamID::sidechainFilter));
    c.externalSidechain = value (ParamID::externalSidechain) >= 0.5f;
    c.mix = value (ParamID::mix) * 0.01f;
    c.alignmentDbu = value (ParamID::alignment);
    return c;
}

void SslBusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const int channels = main.getNumChannels();
    const int samples = main.getNumSamples();
    if (channels == 0 || samples == 0)
        return;

    auto c = readControls();
    const bool sideEnabled = getBus (true, 1)->isEnabled();
    auto side = sideEnabled ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    c.externalSidechain = c.externalSidechain && sideEnabled && side.getNumChannels() > 0;
    if (wasEnabled != c.enabled)
    {
        compressor.reset();
        wasEnabled = c.enabled;
    }
    compressor.setControls (c);

    float inPeak = 0.0f, outPeak = 0.0f;
    auto* left = main.getWritePointer (0);
    auto* right = channels > 1 ? main.getWritePointer (1) : nullptr;
    const auto* sideLeft = c.externalSidechain ? side.getReadPointer (0) : nullptr;
    const auto* sideRight = c.externalSidechain && side.getNumChannels() > 1 ? side.getReadPointer (1) : sideLeft;
    for (int i = 0; i < samples; ++i)
        inPeak = std::max ({ inPeak, std::abs (left[i]), std::abs (right != nullptr ? right[i] : left[i]) });
    compressor.process (left, right, sideLeft, sideRight, samples);
    for (int i = 0; i < samples; ++i)
        outPeak = std::max ({ outPeak, std::abs (left[i]), std::abs (right != nullptr ? right[i] : left[i]) });
    inputPeak.store (inPeak, std::memory_order_relaxed);
    outputPeak.store (outPeak, std::memory_order_relaxed);
    gainReduction.store (c.enabled ? compressor.gainReductionDb() : 0.0f, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SslBusProcessor::createEditor()
{
    return new SslBusEditor (*this);
}

void SslBusProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto state = parameters.copyState().createXml())
        copyXmlToBinary (*state, data);
}

void SslBusProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SslBusProcessor();
}
