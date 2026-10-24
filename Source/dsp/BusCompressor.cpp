#include "BusCompressor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace sslbus
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float dbToNeper = 0.11512925464970229f; // ln (10) / 20
constexpr float vcaDbPerVolt = 20.0f;
constexpr float vcaControlPort = 832.0f;    // dbx 202X data sheet: control port 832R +/-2 %
constexpr float controlCap = 100.0e-9f;      // 82E27 C11 (sidechain) and C12 (audio), node to ground

// Sidechain VCA control, 82E27 T3 -> R52 -> C11 at node C. These are effective values, not the
// drawn ones: 100R into a single 83.2R load gives the sensitivity that matched the measured unit.
// The drawn network (R52 100R into two 832R ports) would double it; kept as an open question.
constexpr float sidechainControlLoad = 83.2f;
constexpr float sidechainControlSeries = 100.0f;
constexpr float sidechainControlDivider = sidechainControlLoad / (sidechainControlSeries + sidechainControlLoad);
constexpr float sidechainControlTau = controlCap * sidechainControlSeries * sidechainControlLoad
    / (sidechainControlSeries + sidechainControlLoad);

// Audio VCA control, 82E27 T2 -> RV3 + R72 (R51 not fitted) -> C12 at node B. The node feeds the
// audio dbx 202X of both 82E26 cards, two 832R ports in parallel. RV3 (1k) + R72 (47R) is trimmed
// by measurement ("SET 5.2 VOLTS ON VCA"); 500R (RV3 about 453R) gives the same divider as the
// sidechain's effective one, so the static sensitivity is unchanged and the control pole is drawn.
constexpr float audioControlLoad = vcaControlPort / 2.0f;
constexpr float audioControlSeries = 500.0f;
constexpr float audioControlDivider = audioControlLoad / (audioControlSeries + audioControlLoad);
constexpr float audioControlTau = controlCap * audioControlSeries * audioControlLoad
    / (audioControlSeries + audioControlLoad);
constexpr float driverDbPerVolt = vcaDbPerVolt * audioControlDivider;

constexpr float sidechainIvGain = 33000.0f / (91000.0f + 1250.0f); // R32 / (R11 + dbx 202X internal input R)
constexpr float audioVcaGain = 4530.0f / (3320.0f + 1250.0f); // R16 / (R12 + dbx 202X internal input R)
constexpr float inputResistance = 3.3e6f;   // 82E27 R13, op-amp input to ground

// Timing parts at the drawing's values times a calibration inside component tolerance (measured
// 1991 G384): electrolytics +20 % (tolerance +/-20 %), attack resistors +5 %.
constexpr float timingCap = 0.47e-6f * 1.2f;       // 82E27 C3-C6
constexpr std::array<float, 6> attackOhms { 820.0f * 1.05f, 2700.0f * 1.05f, 8200.0f * 1.05f,
    27000.0f * 1.05f, 82000.0f * 1.05f, 270000.0f * 1.05f };
constexpr std::array<float, 4> releaseOhms { 180000.0f, 270000.0f, 560000.0f, 1200000.0f };
constexpr float autoUpperCap = 0.47e-6f * 1.2f;    // C1, between the two Auto nodes
constexpr float autoLowerCap = 6.8e-6f * 1.2f;     // C2, lower node to ground
constexpr float autoUpperResistance = 91000.0f;  // R7, parallel with C1
constexpr float autoLowerResistance = 750000.0f; // R8, parallel with C2

// Diodes: I = Is (exp (V / nVt) - 1) in series with Rs. D4 and D6 are 1N4148-class silicon.
constexpr float thermalVolts = 0.025852f;   // kT/q at 300 K
constexpr float diodeIs = 2.5e-9f;
constexpr float diodeNVt = 1.84f * thermalVolts;
constexpr float diodeRs = 0.6f;
// D7 has no part type on the drawing: assumed low-drop (germanium or Schottky class), chosen to
// reproduce the measured 2:1 flattening. An assumption, not a reading of the drawing.
constexpr float diode7Is = 1.0e-6f;
constexpr float diode7NVt = 1.3f * thermalVolts;
constexpr float diodeCutoffVolts = -0.30f;  // below this forward drop the diode current is nil
constexpr float rectifierImbalance = 1.028f; // one half-wave 2.8 % larger (R36 about 1.4 % off in R34-R37)
constexpr float timingSourceLimit = 15.0f;  // T7 supply swing

// 2:1 network (switch 36-39): R38 and R40/R44 to node N, D7 into node X (R41/R45 junction).
constexpr float r38 = 1.0e6f;
constexpr float gNode2 = 1.0f / r38 + 1.0f / 270000.0f;
constexpr float gX2 = 1.0f / 68000.0f + 1.0f / 620000.0f;
constexpr float d7SeriesOhms = (1.0f + 1.0f / (gNode2 * 68000.0f)) / gX2 + diodeRs;

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

// T3: R21 is the 100k timing input; R27 is its 100k feedback. Their
// wires cross without a junction. R22 sums the physical threshold wiper.
// The 50k linear pot sits between +/-12 V through R17/R18 = 3.9k.
float thresholdDriverVoltsAt (float position) noexcept
{
    return thresholdWiperVolts (position) * (100.0f / 360.0f);
}

// T2 similarly uses R19/R25 and the R20 make-up input; positive wiper voltage
// makes T2's inverted output negative, increasing the audio dbx VCA gain.
float makeupDriverVoltsAt (float position) noexcept
{
    return -makeupWiperVolts (position) * (100.0f / 620.0f);
}

const detail::WrightOmegaTable& sharedOmegaTable()
{
    static const detail::WrightOmegaTable table; // first use is a BusCompressor constructor
    return table;
}

float diodeDrop (float current) noexcept
{
    return diodeNVt * detail::fastLog (std::max (current, 1.0e-12f) / diodeIs + 1.0f) + diodeRs * current;
}
} // namespace

namespace detail
{
float fastLog (float x) noexcept
{
    std::uint32_t bits;
    std::memcpy (&bits, &x, sizeof bits);
    int exponent = static_cast<int> ((bits >> 23) & 0xffu) - 127;
    bits = (bits & 0x007fffffu) | 0x3f800000u; // mantissa in [1, 2)
    float mantissa;
    std::memcpy (&mantissa, &bits, sizeof mantissa);
    if (mantissa > 1.41421356f) { mantissa *= 0.5f; ++exponent; }
    const float s = (mantissa - 1.0f) / (mantissa + 1.0f), s2 = s * s;
    return static_cast<float> (exponent) * 0.69314718056f
        + 2.0f * s * (1.0f + s2 * (1.0f / 3.0f + s2 * (0.2f + s2 * (1.0f / 7.0f + s2 * (1.0f / 9.0f)))));
}

WrightOmegaTable::WrightOmegaTable() noexcept
{
    for (int k = 0; k < nodeCount; ++k)
    {
        const double t = static_cast<double> (firstT) + k * static_cast<double> (step);
        double w = t > 1.0 ? t - std::log (t) + std::log (t) / t : std::exp (t);
        for (int it = 0; it < 80; ++it) // Newton on w + ln (w) - t, in the log-safe form
        {
            const double f = w + std::log (w) - t;
            const double next = w - f * w / (w + 1.0);
            const bool done = std::abs (next - w) <= 1.0e-16 * next;
            w = next;
            if (done) break;
        }
        value[static_cast<std::size_t> (k)] = static_cast<float> (w);
        slope[static_cast<std::size_t> (k)] = static_cast<float> (w / (1.0 + w) * static_cast<double> (step));
    }
}

float WrightOmegaTable::operator() (float t) const noexcept
{
    if (t < firstT) return 0.0f;
    if (t < lastT)
    {
        const float scaled = t * 16.0f; // exact scaling, so the fraction below is exact too
        const float whole = std::floor (scaled);
        const auto a = static_cast<std::size_t> (static_cast<int> (whole) - static_cast<int> (firstT * 16.0f));
        const auto b = a + 1;
        const float f = scaled - whole, f2 = f * f;
        return ((2.0f * f - 3.0f) * f2 + 1.0f) * value[a] + ((f - 2.0f) * f2 + f) * slope[a]
             + ((3.0f - 2.0f * f) * f2) * value[b] + ((f - 1.0f) * f2) * slope[b];
    }
    const float lnT = fastLog (t); // transient-only region (E above about 5.7 V)
    float w = t - lnT + lnT / t;
    w *= 1.0f + (t - w - fastLog (w)) / (1.0f + w);
    w *= 1.0f + (t - w - fastLog (w)) / (1.0f + w);
    return w;
}
} // namespace detail

BusCompressor::BusCompressor() noexcept
    : omega (&sharedOmegaTable())
{
    sidechainHighPassHzValue = indexed (sidechainHighPassHz, controls.sidechainHighPass);
    analogPeakAtFullScale = 0.775f * std::sqrt (2.0f) * std::exp (controls.alignmentDbu * dbToNeper);
    d7LnA = std::log (diode7Is * d7SeriesOhms / diode7NVt);
    d7NVtOverR = diode7NVt / d7SeriesOhms;
    updateRateCoefficients();
    updateRatioNetwork();
    refreshWipers();
}

void BusCompressor::updateRateCoefficients() noexcept
{
    sidechainHighPassA = std::exp (-2.0f * pi * sidechainHighPassHzValue / rate);
    parameterA = std::exp (-1.0f / (0.01f * rate));
    // C11/C12 and their Thevenin resistance set the VCA control smoothing.
    sidechainControlA = std::exp (-1.0f / (rate * sidechainControlTau));
    audioControlA = std::exp (-1.0f / (rate * audioControlTau));
    audioInputAcA = std::exp (-2.0f * pi * (1.0f / (2.0f * pi * 5.0e-6f * 30100.0f)) / rate);
    rectifierAcA = std::exp (-2.0f * pi * (1.0f / (2.0f * pi * 6.8e-6f * 20000.0f)) / rate);
    audioVcaAcA = std::exp (-2.0f * pi * (1.0f / (2.0f * pi * 10.0e-6f * 4570.0f)) / rate);

    // Capacitor conductances C / T. The Auto nodes add the 1.3 uS leak of R8 to a 0.39 S term, so
    // their float rounding matters; the division by the rounded step is the form that was calibrated.
    const float step = 1.0f / rate;
    capSiemens = timingCap / step;
    autoUpperCapSiemens = autoUpperCap / step;
    autoLowerCapSiemens = autoLowerCap / step;
    autoUpperSiemens = autoUpperCapSiemens + 1.0f / autoUpperResistance;
    const float autoLowerSiemens = autoLowerCapSiemens + 1.0f / autoLowerResistance;
    autoSum = autoUpperSiemens + autoLowerSiemens;
    autoUpperSquared = autoUpperSiemens * autoUpperSiemens;
    attackIndex = releaseIndex = -1;
    updateTimingCoefficients();
}

void BusCompressor::updateTimingCoefficients() noexcept
{
    if (attackIndex != controls.attack)
    {
        attackIndex = controls.attack;
        // D6 in series with the attack resistor: I = (nVt / R) omega (E / nVt + ln (Is R / nVt)).
        const float seriesOhms = indexed (attackOhms, controls.attack) + diodeRs;
        chargeLnA = std::log (diodeIs * seriesOhms / diodeNVt);
        chargeNVtOverR = diodeNVt / seriesOhms;
        chargeInvR = 1.0f / seriesOhms;
    }
    if (releaseIndex != controls.release)
    {
        releaseIndex = controls.release;
        const float releaseSiemens = controls.release != 4
            ? 1.0f / indexed (releaseOhms, controls.release) : 0.0f;
        singleReleaseSiemens = capSiemens + releaseSiemens + 1.0f / inputResistance;
    }
}

void BusCompressor::updateRatioNetwork() noexcept
{
    // Ratio node M = (sourceAmps - detector current) * nodeOhms. D4 carries the current the loads on M
    // demand: R38 to N, R39+R43 and R40+R44 to -12 V, R41+R45 to +12 V.
    switch (std::clamp (controls.ratio, 0, 2))
    {
        case 2: // switch 36-37: R42 to -12 V, nominal 10:1
            ratioNetwork = { 12.0f / 1.2e6f, r38,
                1.0f / r38 + 1.0f / 2.31e6f + 1.0f / 4.17e6f + 1.0f / 688000.0f,
                -12.0f / 2.31e6f - 12.0f / 4.17e6f + 12.0f / 688000.0f };
            break;
        case 1: // switch 36-38: R39 and R43, nominal 4:1
            ratioNetwork = { 12.0f / 1.8e6f, 1.0f / (1.0f / r38 + 1.0f / 510000.0f),
                1.0f / r38 + 1.0f / 510000.0f + 1.0f / 4.17e6f + 1.0f / 688000.0f,
                -12.0f / 4.17e6f + 12.0f / 688000.0f };
            break;
        default: // switch 36-39: R40 and R44, with D7/R41/R45 knee, solved in rectifier()
            ratioNetwork = { 12.0f / 3.9e6f, 1.0f / gNode2, 0.0f, 0.0f };
            break;
    }
}

void BusCompressor::refreshWipers() noexcept
{
    thresholdDriverVolts = thresholdDriverVoltsAt (smoothThresholdPosition);
    makeupDriverVolts = makeupDriverVoltsAt (smoothMakeupPosition);
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
    refreshWipers();
}

void BusCompressor::setControls (const Controls& next) noexcept
{
    const float oldHighPassHz = indexed (sidechainHighPassHz, controls.sidechainHighPass);
    const float oldAlignmentDbu = controls.alignmentDbu;
    const int oldRatio = controls.ratio;
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
        analogPeakAtFullScale = 0.775f * std::sqrt (2.0f) * std::exp (controls.alignmentDbu * dbToNeper);
    if (controls.ratio != oldRatio)
        updateRatioNetwork();
    updateTimingCoefficients();
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

BusCompressor::Detector BusCompressor::rectifier (float sample, int channel, float sidechainGain) noexcept
{
    // 82E26: R11 / J112 R30 / dbx202X / T5, then T6/T7 precision rectifier. The
    // R39-R45 network is solved at T7's virtual-ground input. D2/D3 make
    // the current from R36/R37 equal to |T5 output| / 20k on either half.
    const float ivOutput = acCouple (sample * analogPeakAtFullScale
        * sidechainIvGain * sidechainGain, channel, sidechainAcState,
        rectifierAcA); // C18/R34 after T5
    // R36 off by about 1.4 % unbalances the half-waves: the side carried by R36 sees 2.8 % more.
    const float ivMagnitude = ivOutput > 0.0f ? ivOutput * rectifierImbalance : -ivOutput;
    const float inputCurrent = ivMagnitude / 20000.0f; // R37; R34/R35/R36 full-wave balance
    const float sourceCurrent = ratioNetwork.sourceAmps - inputCurrent;
    float ratioNode, diodeAmps;
    if (controls.ratio >= 1) // 4:1 and 10:1 are linear in the detector current
    {
        ratioNode = sourceCurrent * ratioNetwork.nodeOhms;
        diodeAmps = -ratioNode * ratioNetwork.loadSiemens + ratioNetwork.offsetAmps;
    }
    else
    {
        // D7 as a Shockley diode: current u from N through D7 into node X (R41/R45 junction).
        const float openX = -((sourceCurrent * ratioNetwork.nodeOhms) / 68000.0f + 12.0f / 620000.0f) / gX2;
        float u = 0.0f;
        if (openX > diodeCutoffVolts)
            u = d7NVtOverR * (*omega) (openX / diode7NVt + d7LnA);
        ratioNode = (sourceCurrent + u) * ratioNetwork.nodeOhms;
        const float xNode = (u + ratioNode / 68000.0f + 12.0f / 620000.0f) / gX2;
        diodeAmps = -ratioNode * gNode2 - (ratioNode - xNode) / 68000.0f;
    }
    // D4 at T7's output and D6 into the timing node: their drops cancel only when the currents
    // match, so the timing source is -M plus D4's drop (added in processFrame) and D6's drop (in
    // the charge branch).
    return { std::min (-ratioNode, timingSourceLimit), std::max (diodeAmps, 1.0e-7f) };
}

void BusCompressor::integrateTiming (float sourceVolts) noexcept
{
    // D6 and the attack resistor charge the timing capacitor through a Shockley branch, taken as a
    // Norton source linearised at the previous timing voltage (one linearly-implicit Euler step).
    float chargeG = 0.0f, chargeI = 0.0f;
    const float forward = sourceVolts - timingVolts;
    if (forward > diodeCutoffVolts)
    {
        const float w = (*omega) (forward / diodeNVt + chargeLnA);
        chargeI = chargeNVtOverR * w;
        chargeG = w * chargeInvR / (1.0f + w);
    }
    const float chargeSource = chargeI + chargeG * timingVolts;
    if (controls.release != 4)
    {
        timingVolts = (capSiemens * timingVolts + chargeSource) / (singleReleaseSiemens + chargeG);
        return;
    }

    // C1 || R7 between nodes; C2 || R8 from lower node to ground.
    // Solve the coupled nodal equations with backward Euler.
    const float previousDifference = timingVolts - autoReservoirVolts;
    const float a = autoUpperSiemens + 1.0f / inputResistance + chargeG;
    const float rhsTop = autoUpperCapSiemens * previousDifference + chargeSource;
    const float rhsBottom = autoLowerCapSiemens * autoReservoirVolts
        - autoUpperCapSiemens * previousDifference;
    const float determinant = a * autoSum - autoUpperSquared;
    const float top = (rhsTop * autoSum + autoUpperSiemens * rhsBottom) / determinant;
    const float bottom = (a * rhsBottom + autoUpperSiemens * rhsTop) / determinant;
    timingVolts = std::max (0.0f, top);
    autoReservoirVolts = std::max (0.0f, bottom);
}

Frame BusCompressor::processFrame (Frame input, Frame external, bool useExternal) noexcept
{
    // Threshold and make-up are 10 ms smoothed; the wiper maths reruns only while a position moves.
    const float thresholdPosition = parameterA * smoothThresholdPosition + (1.0f - parameterA) * controls.thresholdPosition;
    if (thresholdPosition != smoothThresholdPosition)
    {
        smoothThresholdPosition = thresholdPosition;
        thresholdDriverVolts = thresholdDriverVoltsAt (thresholdPosition);
    }
    const float makeupPosition = parameterA * smoothMakeupPosition + (1.0f - parameterA) * controls.makeupPosition;
    if (makeupPosition != smoothMakeupPosition)
    {
        smoothMakeupPosition = makeupPosition;
        makeupDriverVolts = makeupDriverVoltsAt (makeupPosition);
    }
    smoothMix = parameterA * smoothMix + (1.0f - parameterA) * controls.mix;

    const float sidechainDriverVolts = timingVolts + thresholdDriverVolts;
    sidechainControlVolts = sidechainControlA * sidechainControlVolts
        + (1.0f - sidechainControlA) * sidechainDriverVolts * sidechainControlDivider;
    const float sidechainGain = std::exp (-sidechainControlVolts * (vcaDbPerVolt * dbToNeper));
    // 82E26 C3/C4 are back-to-back 10uF in the input path. T2's R7=30.1k
    // is the load seen by their effective 5uF capacitance. The internal
    // detector taps T2's output, so it receives this coupling stage too.
    const Frame preAudio {
        acCouple (input.left, 0, audioInputAcState, audioInputAcA),
        acCouple (input.right, 1, audioInputAcState, audioInputAcA)
    };
    const Frame key = useExternal ? external : preAudio;
    const Detector left = rectifier (highPass (key.left, 0), 0, sidechainGain);
    const Detector right = rectifier (highPass (key.right, 1), 1, sidechainGain);
    // 82E26 diode OR of the two D6 at node D: source = M + D4's drop, D6's drop is in the charge branch.
    const Detector& winner = left.volts >= right.volts ? left : right;
    integrateTiming (winner.volts + diodeDrop (winner.diodeAmps));
    const float timingReductionDb = std::clamp (timingVolts * driverDbPerVolt, 0.0f, 40.0f);
    reductionDb = audioControlA * reductionDb + (1.0f - audioControlA) * timingReductionDb;

    // D3 clamps the panel supply to 12 V; R16=4.7k feeds its 25k LIN pot.
    // The magnitudes below use a positive-gain sign.
    const float audioDriverVolts = timingVolts + makeupDriverVolts;
    audioControlVolts = audioControlA * audioControlVolts
        + (1.0f - audioControlA) * audioDriverVolts * audioControlDivider;
    const float audioGain = std::exp (-audioControlVolts * (vcaDbPerVolt * dbToNeper));
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

Frame BusCompressor::process (Frame input, Frame sidechain) noexcept
{
    if (!controls.enabled) return input;
    return processFrame (input, sidechain, controls.externalSidechain);
}

void BusCompressor::process (float* left, float* right, const float* keyLeft, const float* keyRight,
                             int numSamples) noexcept
{
    if (!controls.enabled) return;
    const bool useExternal = controls.externalSidechain && keyLeft != nullptr;
    if (keyRight == nullptr) keyRight = keyLeft;
    for (int i = 0; i < numSamples; ++i)
    {
        const Frame dry { left[i], right != nullptr ? right[i] : left[i] };
        const Frame external { useExternal ? keyLeft[i] : 0.0f, useExternal ? keyRight[i] : 0.0f };
        const Frame wet = processFrame (dry, external, useExternal);
        left[i] = wet.left;
        if (right != nullptr)
            right[i] = wet.right;
    }
}
} // namespace sslbus
