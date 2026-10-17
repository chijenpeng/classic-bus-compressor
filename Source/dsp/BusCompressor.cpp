#include "BusCompressor.h"

#include <algorithm>
#include <cmath>

namespace sslbus
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float vcaDbPerVolt = 20.0f;
constexpr float vcaControlLoad = 83.2f;    // dbx 202X data sheet
constexpr float controlSeries = 100.0f;     // R52; R72 + RV3 trim set to 100R equivalent
constexpr float driverDbPerVolt = vcaDbPerVolt * vcaControlLoad / (controlSeries + vcaControlLoad);
constexpr float sidechainIvGain = 33000.0f / (91000.0f + 1250.0f); // R32 / (R11 + dbx 202X internal input R)
constexpr float audioVcaGain = 4530.0f / (3320.0f + 1250.0f); // R16 / (R12 + dbx 202X internal input R)
constexpr float timingCap = 0.47e-6f;        // 82E27 C3-C6
constexpr float inputResistance = 3.3e6f;   // 82E27 R13, op-amp input to ground
constexpr std::array<float, 6> attackOhms { 820.0f, 2700.0f, 8200.0f, 27000.0f, 82000.0f, 270000.0f };
constexpr std::array<float, 4> releaseOhms { 180000.0f, 270000.0f, 560000.0f, 1200000.0f };
constexpr float autoUpperCap = 0.47e-6f;     // C1, between the two Auto nodes
constexpr float autoLowerCap = 6.8e-6f;      // C2, lower node to ground
constexpr float autoUpperResistance = 91000.0f;  // R7, parallel with C1
constexpr float autoLowerResistance = 750000.0f; // R8, parallel with C2

template <typename T, std::size_t N>
T indexed (const std::array<T, N>& values, int index) noexcept
{
    return values[static_cast<std::size_t> (std::clamp (index, 0, static_cast<int> (N) - 1))];
}

float thresholdWiperVolts (float position) noexcept
{
    // RV threshold 50k LIN, R17/R18 3.9k to +/-12V, R22 360k loading.
    const float upper = 3900.0f + (1.0f - position) * 25000.0f;
    const float lower = 3900.0f + (1.0f + position) * 25000.0f;
    return (12.0f / upper - 12.0f / lower)
        / (1.0f / upper + 1.0f / lower + 1.0f / 360000.0f);
}

float makeupWiperVolts (float position) noexcept
{
    // RV make-up 25k LIN, R16 4.7k to +12V, R20 620k loading.
    if (position <= 0.0f) return 0.0f;
    const float upper = 4700.0f + (1.0f - position) * 25000.0f;
    const float lower = position * 25000.0f;
    return (12.0f / upper)
        / (1.0f / upper + 1.0f / lower + 1.0f / 620000.0f);
}
} // namespace

float BusCompressor::dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

BusCompressor::BusCompressor() noexcept
{
    sidechainHighPassHzValue = indexed (sidechainHighPassHz, controls.sidechainHighPass);
    analogPeakAtFullScale = 0.775f * std::sqrt (2.0f) * dbToGain (controls.alignmentDbu);
    updateRateCoefficients();
}

void BusCompressor::updateRateCoefficients() noexcept
{
    sidechainHighPassA = std::exp (-2.0f * pi * sidechainHighPassHzValue / rate);
    parameterA = std::exp (-1.0f / (0.01f * rate));
    // C11/C12 and their Thevenin resistance set the VCA control smoothing.
    constexpr float controlTau = 100.0e-9f * controlSeries * vcaControlLoad
        / (controlSeries + vcaControlLoad);
    controlA = std::exp (-1.0f / (rate * controlTau));
    audioInputAcA = std::exp (-2.0f * pi * (1.0f / (2.0f * pi * 5.0e-6f * 30100.0f)) / rate);
    rectifierAcA = std::exp (-2.0f * pi * (1.0f / (2.0f * pi * 6.8e-6f * 20000.0f)) / rate);
    audioVcaAcA = std::exp (-2.0f * pi * (1.0f / (2.0f * pi * 10.0e-6f * 4570.0f)) / rate);
}

void BusCompressor::prepare (double sampleRate) noexcept
{
    rate = static_cast<float> (std::max (sampleRate, 8000.0));
    updateRateCoefficients();
    reset();
}

void BusCompressor::reset() noexcept
{
    lowPassState.fill (0.0f);
    sidechainAcState.fill (0.0f);
    audioInputAcState.fill (0.0f);
    audioVcaAcState.fill (0.0f);
    audioOutputAcState.fill (0.0f);
    timingVolts = autoReservoirVolts = reductionDb = 0.0f;
    audioControlVolts = sidechainControlVolts = 0.0f;
    smoothThresholdPosition = controls.thresholdPosition;
    smoothMakeupPosition = controls.makeupPosition;
    smoothMix = controls.mix;
}

void BusCompressor::setControls (const Controls& next) noexcept
{
    const float oldHighPassHz = indexed (sidechainHighPassHz, controls.sidechainHighPass);
    const float oldAlignmentDbu = controls.alignmentDbu;
    controls = next;
    controls.thresholdPosition = std::clamp (controls.thresholdPosition, -1.0f, 1.0f);
    controls.makeupPosition = std::clamp (controls.makeupPosition, 0.0f, 1.0f);
    controls.alignmentDbu = std::clamp (controls.alignmentDbu, 12.0f, 30.0f);
    controls.mix = std::clamp (controls.mix, 0.0f, 1.0f);
    const float newHighPassHz = indexed (sidechainHighPassHz, controls.sidechainHighPass);
    if (newHighPassHz != oldHighPassHz)
    {
        sidechainHighPassHzValue = newHighPassHz;
        sidechainHighPassA = std::exp (-2.0f * pi * sidechainHighPassHzValue / rate);
    }
    if (controls.alignmentDbu != oldAlignmentDbu)
        analogPeakAtFullScale = 0.775f * std::sqrt (2.0f) * dbToGain (controls.alignmentDbu);
}

float BusCompressor::highPass (float input, int channel) noexcept
{
    if (sidechainHighPassHzValue <= 0.0f) return input;
    auto& state = lowPassState[static_cast<std::size_t> (channel)];
    state = sidechainHighPassA * state + (1.0f - sidechainHighPassA) * input;
    return input - state;
}

float BusCompressor::acCouple (float input, int channel,
    std::array<float, 2>& state, float a) noexcept
{
    auto& low = state[static_cast<std::size_t> (channel)];
    low = a * low + (1.0f - a) * input;
    return input - low;
}

float BusCompressor::rectifier (float sample, int channel, float sidechainGain) noexcept
{
    // 82E26: R11 / J112 R30 / dbx202X / T5, then T6/T7 precision rectifier. The
    // R39-R45 network is solved at T7's virtual-ground input. D2/D3 make
    // the current from R36/R37 equal to |T5 output| / 20k on either half.
    const float ivOutput = acCouple (sample * analogPeakAtFullScale
        * sidechainIvGain * sidechainGain, channel, sidechainAcState,
        rectifierAcA); // C18/R34 after T5
    const float ivMagnitude = std::abs (ivOutput);
    const float inputCurrent = ivMagnitude / 20000.0f; // R37; R34/R35/R36 full-wave balance
    constexpr float r38 = 1.0e6f;
    float ratioNode;
    switch (std::clamp (controls.ratio, 0, 2))
    {
        case 2: // switch 36-37: R42 to -12 V, nominal 10:1
            ratioNode = (12.0f / 1.2e6f - inputCurrent) * r38;
            break;
        case 1: // switch 36-38: R39 and R43, nominal 4:1
            ratioNode = (12.0f / 1.8e6f - inputCurrent)
                / (1.0f / r38 + 1.0f / 510000.0f);
            break;
        default: // switch 36-39: R40 and R44, with D7/R41/R45 knee
        {
            ratioNode = (12.0f / 3.9e6f - inputCurrent)
                / (1.0f / r38 + 1.0f / 270000.0f);
            const float divider = (ratioNode / 68000.0f + 12.0f / 620000.0f)
                / (1.0f / 68000.0f + 1.0f / 620000.0f);
            if (divider < -0.60f) // D7 forward biased; 0.6 V silicon diode
                ratioNode = (12.0f / 3.9e6f - 0.60f / 68000.0f
                    - 12.60f / 620000.0f - inputCurrent)
                    / (1.0f / r38 + 1.0f / 270000.0f + 1.0f / 68000.0f);
            break;
        }
    }
    // D4 at T7 output and D6 into timing have opposing ~0.6 V drops.
    // Their drops cancel while conducting, so the timing source is -N.
    return std::clamp (-ratioNode, 0.0f, 15.0f);
}

void BusCompressor::integrateTiming (float sourceVolts) noexcept
{
    const float attackG = 1.0f / indexed (attackOhms, controls.attack);
    const float inputG = 1.0f / inputResistance;
    const float step = 1.0f / rate;
    const float chargeG = sourceVolts > timingVolts ? attackG : 0.0f;
    if (controls.release != 4)
    {
        const float releaseG = 1.0f / indexed (releaseOhms, controls.release);
        const float capG = timingCap / step;
        timingVolts = (capG * timingVolts + chargeG * sourceVolts)
            / (capG + chargeG + releaseG + inputG);
        return;
    }

    // C1 || R7 between nodes; C2 || R8 from lower node to ground.
    // Solve the coupled nodal equations with backward Euler.
    const float upperG = autoUpperCap / step + 1.0f / autoUpperResistance;
    const float lowerG = autoLowerCap / step + 1.0f / autoLowerResistance;
    const float previousDifference = timingVolts - autoReservoirVolts;
    const float a = upperG + inputG + chargeG;
    const float d = upperG + lowerG;
    const float rhsTop = autoUpperCap / step * previousDifference + chargeG * sourceVolts;
    const float rhsBottom = autoLowerCap / step * autoReservoirVolts
        - autoUpperCap / step * previousDifference;
    const float determinant = a * d - upperG * upperG;
    const float top = (rhsTop * d + upperG * rhsBottom) / determinant;
    const float bottom = (a * rhsBottom + upperG * rhsTop) / determinant;
    timingVolts = std::max (0.0f, top);
    autoReservoirVolts = std::max (0.0f, bottom);
}

Frame BusCompressor::process (Frame input, Frame sidechain) noexcept
{
    if (!controls.enabled) return input;

    smoothThresholdPosition = parameterA * smoothThresholdPosition + (1.0f - parameterA) * controls.thresholdPosition;
    smoothMakeupPosition = parameterA * smoothMakeupPosition + (1.0f - parameterA) * controls.makeupPosition;
    smoothMix = parameterA * smoothMix + (1.0f - parameterA) * controls.mix;

    // T3: R21 is the 100k timing input; R27 is its 100k feedback. Their
    // wires cross without a junction. R22 sums the physical threshold wiper.
    // The 50k linear pot sits between +/-12 V through R17/R18 = 3.9k.
    // T2 similarly uses R19/R25 and the R20 make-up input.
    const float thresholdPotVolts = thresholdWiperVolts (smoothThresholdPosition);
    const float sidechainDriverVolts = timingVolts + thresholdPotVolts * (100.0f / 360.0f);
    constexpr float divider = vcaControlLoad / (controlSeries + vcaControlLoad);
    sidechainControlVolts = controlA * sidechainControlVolts
        + (1.0f - controlA) * sidechainDriverVolts * divider;
    const float sidechainGain = dbToGain (-sidechainControlVolts * vcaDbPerVolt);
    // 82E26 C3/C4 are back-to-back 10uF in the input path. T2's R7=30.1k
    // is the load seen by their effective 5uF capacitance. The internal
    // detector taps T2's output, so it receives this coupling stage too.
    const Frame preAudio {
        acCouple (input.left, 0, audioInputAcState, audioInputAcA),
        acCouple (input.right, 1, audioInputAcState, audioInputAcA)
    };
    const Frame key = controls.externalSidechain ? sidechain : preAudio;
    const float left = rectifier (highPass (key.left, 0), 0, sidechainGain);
    const float right = rectifier (highPass (key.right, 1), 1, sidechainGain);
    integrateTiming (std::max (left, right)); // 82E26 diode OR at node D
    reductionDb = std::clamp (timingVolts * driverDbPerVolt, 0.0f, 40.0f);

    // D3 clamps the panel supply to 12 V; R16=4.7k feeds its 25k LIN pot.
    // Positive wiper voltage makes T2's inverted output negative, increasing
    // the audio dbx VCA gain. The magnitudes below use a positive-gain sign.
    const float makeupPotVolts = -makeupWiperVolts (smoothMakeupPosition);
    const float audioDriverVolts = timingVolts + makeupPotVolts * (100.0f / 620.0f);
    audioControlVolts = controlA * audioControlVolts
        + (1.0f - controlA) * audioDriverVolts * divider;
    const float audioGain = dbToGain (-audioControlVolts * vcaDbPerVolt);
    // C9=10uF drives R12=3.32k plus the 202X's 1.25k internal resistor;
    // C11/C12 are two 10uF parts in series ahead of T4's R19=30.1k.
    const float wetLeft = acCouple (acCouple (preAudio.left, 0, audioVcaAcState,
        audioVcaAcA) * audioGain * audioVcaGain,
        0, audioOutputAcState, audioInputAcA);
    const float wetRight = acCouple (acCouple (preAudio.right, 1, audioVcaAcState,
        audioVcaAcA) * audioGain * audioVcaGain,
        1, audioOutputAcState, audioInputAcA);
    return { input.left * (1.0f - smoothMix) + wetLeft * smoothMix,
             input.right * (1.0f - smoothMix) + wetRight * smoothMix };
}
} // namespace sslbus
