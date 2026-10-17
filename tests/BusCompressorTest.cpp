#include "dsp/BusCompressor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float rate = 48000.0f;

void check (bool condition, const char* what)
{
    if (!condition) { std::cerr << "FAIL: " << what << '\n'; std::exit (1); }
}

float runSine (sslbus::BusCompressor& comp, float amplitude, float hz,
               int samples = 96000, bool external = false)
{
    double sum = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        const float wave = std::sin (2.0f * pi * hz * static_cast<float> (i) / rate);
        const auto output = comp.process (external ? sslbus::Frame { 0.01f * wave, 0.01f * wave }
                                                    : sslbus::Frame { amplitude * wave, amplitude * wave },
                                          { amplitude * wave, amplitude * wave });
        if (i >= samples / 2) sum += static_cast<double> (output.left) * output.left;
    }
    return static_cast<float> (std::sqrt (sum / (samples - samples / 2)));
}
}

int main()
{
    sslbus::BusCompressor comp;
    sslbus::Controls c;
    c.alignmentDbu = 22.0f;
    c.thresholdPosition = 0.5f;
    c.release = 0;
    c.attack = 0;
    comp.setControls (c);
    comp.prepare (rate);

    const float quiet = runSine (comp, 0.01f, 1000.0f);
    check (std::abs (quiet - 0.01f / std::sqrt (2.0f)) < 0.0002f,
           "quiet signal passes at unity");
    check (comp.gainReductionDb() < 0.01f, "no gain reduction below threshold");

    c.thresholdPosition = -1.0f;
    comp.setControls (c);
    comp.reset();
    const float lowThreshold = runSine (comp, 0.1f, 1000.0f);
    c.thresholdPosition = 1.0f;
    comp.setControls (c);
    comp.reset();
    const float highThreshold = runSine (comp, 0.1f, 1000.0f);
    check (highThreshold > lowThreshold * 1.5f,
           "physical threshold pot changes detector VCA gain");

    c.makeupPosition = 1.0f;
    comp.setControls (c);
    comp.reset();
    const float fullMakeup = runSine (comp, 0.01f, 1000.0f);
    check (fullMakeup / quiet > 5.0f && fullMakeup / quiet < 6.0f,
           "25k make-up pot and 4.7k feed set the full-travel gain");
    c.makeupPosition = 0.0f;
    c.thresholdPosition = 0.5f;
    comp.setControls (c);

    // The ratio switch changes the knee as well as the slope: compare the
    // settled response to a 6 dB input rise instead of one shared point.
    float previousRise = 100.0f;
    for (int ratio = 0; ratio < 3; ++ratio)
    {
        c.ratio = ratio;
        comp.setControls (c);
        comp.reset();
        const float low = runSine (comp, 0.25f, 1000.0f);
        comp.reset();
        const float high = runSine (comp, 0.5f, 1000.0f);
        const float riseDb = 20.0f * std::log10 (high / low);
        check (riseDb < previousRise - 0.4f, "ratio network slope ordering");
        previousRise = riseDb;
    }

    c.ratio = 1;
    comp.setControls (c);
    comp.reset();
    double leftEnergy = 0.0, rightEnergy = 0.0;
    for (int i = 0; i < 96000; ++i)
    {
        const float x = std::sin (2.0f * pi * 1000.0f * i / rate);
        const auto y = comp.process ({ 0.5f * x, 0.05f * x }, {});
        if (i > 48000) { leftEnergy += y.left * y.left; rightEnergy += y.right * y.right; }
    }
    check (std::abs (std::sqrt (leftEnergy / rightEnergy) - 10.0) < 0.02,
           "left and right share the exact VCA control");
    check (comp.gainReductionDb() > 5.0f, "dominant channel drives linked detector");

    auto sineReduction = [&] (int filter, float hz)
    {
        c.sidechainHighPass = filter;
        comp.setControls (c);
        comp.reset();
        runSine (comp, 0.25f, hz);
        return comp.gainReductionDb();
    };
    const float lowUnfiltered = sineReduction (0, 40.0f);
    const float lowFiltered = sineReduction (5, 40.0f);
    check (lowUnfiltered - lowFiltered > 1.0f, "optional sidechain HPF reduces LF detection");

    c.sidechainHighPass = 0;
    c.externalSidechain = true;
    comp.setControls (c);
    comp.reset();
    const float externallyKeyed = runSine (comp, 0.5f, 1000.0f, 96000, true);
    check (externallyKeyed < 0.005f, "external key drives compression");

    c.enabled = false;
    comp.setControls (c);
    const auto bypassed = comp.process ({ 0.37f, -0.22f }, { 0.9f, 0.9f });
    check (bypassed.left == 0.37f && bypassed.right == -0.22f, "bypass is bit exact");

    c.enabled = true;
    c.externalSidechain = false;
    c.release = 4;
    comp.setControls (c);
    auto autoAfterSilence = [&] (int activeSamples)
    {
        comp.reset();
        runSine (comp, 0.5f, 1000.0f, activeSamples);
        for (int i = 0; i < 7200; ++i) comp.process ({}, {});
        return comp.gainReductionDb();
    };
    check (autoAfterSilence (48000) > autoAfterSilence (480) + 0.5f,
           "coupled Auto capacitors remember sustained compression");

    c.release = 0;
    comp.setControls (c);
    comp.reset();
    for (int i = 0; i < 48000; ++i) comp.process ({ 0.5f, 0.5f }, {});
    const auto dcOutput = comp.process ({ 0.5f, 0.5f }, {});
    check (std::abs (dcOutput.left) < 0.01f, "82E26 audio coupling rejects DC");

    for (int ratio = 0; ratio < 3; ++ratio)
        for (int attack = 0; attack < 6; ++attack)
            for (int release = 0; release < 5; ++release)
            {
                c.ratio = ratio; c.attack = attack; c.release = release;
                comp.setControls (c);
                const auto y = comp.process ({ 1.0f, -1.0f }, {});
                check (std::isfinite (y.left) && std::isfinite (y.right)
                    && std::isfinite (comp.gainReductionDb()), "finite at all switch detents");
            }
    std::cout << "82E26 / 82E27 DSP checks passed\n";
}
