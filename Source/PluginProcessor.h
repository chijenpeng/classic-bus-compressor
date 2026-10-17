#pragma once

#include "dsp/BusCompressor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace ParamID
{
inline constexpr auto in = "in";
inline constexpr auto threshold = "threshold";
inline constexpr auto ratio = "ratio";
inline constexpr auto attack = "attack";
inline constexpr auto release = "release";
inline constexpr auto makeup = "makeup";
inline constexpr auto sidechainFilter = "sidechainFilter";
inline constexpr auto externalSidechain = "externalSidechain";
inline constexpr auto mix = "mix";
inline constexpr auto alignment = "alignment";
}

class SslBusProcessor final : public juce::AudioProcessor
{
public:
    SslBusProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    float getGainReductionDb() const noexcept { return gainReduction.load (std::memory_order_relaxed); }
    float getInputPeak() const noexcept { return inputPeak.load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak.load (std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    sslbus::Controls readControls() const noexcept;

    sslbus::BusCompressor compressor;
    std::atomic<float> gainReduction { 0.0f }, inputPeak { 0.0f }, outputPeak { 0.0f };
    bool wasEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SslBusProcessor)
};
