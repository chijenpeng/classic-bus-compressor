#pragma once

#include <array>

namespace sslbus
{
inline constexpr std::array<float, 3> ratios { 2.0f, 4.0f, 10.0f };
inline constexpr std::array<float, 6> attacksMs { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
inline constexpr std::array<float, 5> releasesSeconds { 0.1f, 0.3f, 0.6f, 1.2f, 0.0f };
inline constexpr std::array<float, 6> sidechainHighPassHz { 0.0f, 30.0f, 60.0f, 105.0f, 125.0f, 185.0f };

struct Controls
{
    float thresholdPosition = 0.5f; // physical 50k linear pot, -1 to +1
    float makeupPosition = 0.0f; // physical 25k linear pot, 0 to 1
    float alignmentDbu = 22.0f;
    float mix = 1.0f;
    int ratio = 1;
    int attack = 5;
    int release = 4;
    int sidechainHighPass = 0;
    bool externalSidechain = false;
    bool enabled = true;
};

struct Frame { float left = 0.0f, right = 0.0f; };

namespace detail
{
/** Natural logarithm of a positive normal float (mantissa atanh series); abs error below 4e-8. */
float fastLog (float x) noexcept;

/** Wright omega function, omega + ln(omega) = t, by cubic Hermite interpolation of a table.
    Grid step 1/16 over t in [-27, 120] (2354 nodes, built in double precision by the constructor),
    zero below the range and an asymptotic start plus two Newton steps above it. Worst relative
    error 2e-7 (about -134 dB) against the exact function; tests/BusCompressorTest.cpp proves it. */
class WrightOmegaTable
{
public:
    static constexpr float firstT = -27.0f, lastT = 120.0f, step = 1.0f / 16.0f;
    static constexpr int nodeCount = static_cast<int> ((lastT - firstT) * 16.0f) + 2;

    WrightOmegaTable() noexcept;
    float operator() (float t) const noexcept;

private:
    std::array<float, nodeCount> value {}, slope {};
};
} // namespace detail

/** 82E26 / 82E27 stereo detector and timing topology. */
class BusCompressor
{
public:
    BusCompressor() noexcept;
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setControls (const Controls&) noexcept;
    /** One frame; the sidechain frame is used when Controls::externalSidechain is set. */
    Frame process (Frame input, Frame sidechain) noexcept;
    /** In-place block. right == nullptr is a mono block (left feeds both detector channels and only
        left is written). keyLeft == nullptr uses the internal key; keyRight == nullptr reuses keyLeft.
        The external key is used only when Controls::externalSidechain is set. */
    void process (float* left, float* right, const float* keyLeft, const float* keyRight,
                  int numSamples) noexcept;
    float gainReductionDb() const noexcept { return reductionDb; }

private:
    /** Detector output of one 82E26 card: T7 summing-node volts and the D4 current behind them. */
    struct Detector { float volts, diodeAmps; };
    /** Per-ratio constants of the R38-R45 network (switch 36-37 and 36-38). */
    struct RatioNetwork { float sourceAmps, nodeOhms, loadSiemens, offsetAmps; };

    Frame processFrame (Frame input, Frame external, bool useExternal) noexcept;
    float highPass (float input, int channel) noexcept;
    float acCouple (float input, int channel, std::array<float, 2>& state, float coefficient) noexcept;
    Detector rectifier (float sample, int channel, float sidechainGain) noexcept;
    void integrateTiming (float sourceVolts) noexcept;
    void updateRateCoefficients() noexcept;
    void updateTimingCoefficients() noexcept;
    void updateRatioNetwork() noexcept;
    void refreshWipers() noexcept;

    Controls controls;
    const detail::WrightOmegaTable* omega = nullptr;
    float rate = 48000.0f;
    float sidechainHighPassHzValue = 0.0f, sidechainHighPassA = 1.0f;
    float parameterA = 0.0f, sidechainControlA = 0.0f, audioControlA = 0.0f, analogPeakAtFullScale = 0.0f;
    float audioInputAcA = 0.0f, rectifierAcA = 0.0f, audioVcaAcA = 0.0f;
    std::array<float, 2> lowPassState {};
    std::array<float, 2> sidechainAcState {}, audioInputAcState {}, audioVcaAcState {}, audioOutputAcState {};

    // Derived from the controls in setControls / prepare, never per sample.
    RatioNetwork ratioNetwork {};
    int attackIndex = -1, releaseIndex = -1;
    float d7LnA = 0.0f, d7NVtOverR = 0.0f;
    float chargeLnA = 0.0f, chargeNVtOverR = 0.0f, chargeInvR = 0.0f;
    float capSiemens = 0.0f, singleReleaseSiemens = 0.0f; // C3-C6 / T, and that plus release and input
    float autoUpperSiemens = 0.0f, autoUpperCapSiemens = 0.0f, autoLowerCapSiemens = 0.0f,
          autoSum = 0.0f, autoUpperSquared = 0.0f;

    float timingVolts = 0.0f;
    float autoReservoirVolts = 0.0f;
    float audioControlVolts = 0.0f, sidechainControlVolts = 0.0f;
    float reductionDb = 0.0f; // timing-node reduction through the audio control pole
    float smoothThresholdPosition = 0.5f, thresholdDriverVolts = 0.0f;
    float smoothMakeupPosition = 0.0f, makeupDriverVolts = 0.0f;
    float smoothMix = 1.0f;
};
} // namespace sslbus
