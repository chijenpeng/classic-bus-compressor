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

/** 82E26 / 82E27 stereo detector and timing topology. */
class BusCompressor
{
public:
    BusCompressor() noexcept;
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setControls (const Controls&) noexcept;
    Frame process (Frame input, Frame sidechain) noexcept;
    float gainReductionDb() const noexcept { return reductionDb; }

private:
    static float dbToGain (float db) noexcept;
    float highPass (float input, int channel) noexcept;
    float acCouple (float input, int channel, std::array<float, 2>& state, float coefficient) noexcept;
    float rectifier (float sample, int channel, float sidechainGain) noexcept;
    void integrateTiming (float sourceVolts) noexcept;
    void updateRateCoefficients() noexcept;

    Controls controls;
    float rate = 48000.0f;
    float sidechainHighPassHzValue = 0.0f, sidechainHighPassA = 1.0f;
    float parameterA = 0.0f, controlA = 0.0f, analogPeakAtFullScale = 0.0f;
    float audioInputAcA = 0.0f, rectifierAcA = 0.0f, audioVcaAcA = 0.0f;
    std::array<float, 2> lowPassState {};
    std::array<float, 2> sidechainAcState {}, audioInputAcState {}, audioVcaAcState {}, audioOutputAcState {};
    float timingVolts = 0.0f;
    float autoReservoirVolts = 0.0f;
    float audioControlVolts = 0.0f, sidechainControlVolts = 0.0f;
    float reductionDb = 0.0f;
    float smoothThresholdPosition = 0.5f;
    float smoothMakeupPosition = 0.0f;
    float smoothMix = 1.0f;
};
} // namespace sslbus
