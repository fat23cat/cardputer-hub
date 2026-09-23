#pragma once

#include "core/audio/microphone_adapter.h"
#include "services/audio/audio_service.h"

#include <array>
#include <chrono>

namespace cardputer_hub::services {

enum class MicrophoneState { Idle, Capturing, Failed };

struct MicrophoneSnapshot {
    MicrophoneState state = MicrophoneState::Idle;
    float rawLevel = 0;
    float normalizedLevel = 0;
    float peakLevel = 0;
    float ambientLevel = 0;
    float dbfs = -96.0f;
    std::uint32_t windowCount = 0;
    std::int16_t sampleMin = 0;
    std::int16_t sampleMax = 0;
};

class MicrophoneService {
  public:
    static constexpr std::uint32_t sampleRate = 16000;
    static constexpr std::size_t windowSamples = 256;

    MicrophoneService(core::IMicrophoneAdapter& adapter, AudioService& audio) noexcept
        : adapter_(adapter), audio_(audio) {}

    bool start();
    void stop();
    void update(std::chrono::milliseconds elapsed);
    [[nodiscard]] MicrophoneSnapshot snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] static float windowDbfs(const std::int16_t* samples,
                                          std::size_t sampleCount) noexcept;

  private:
    core::IMicrophoneAdapter& adapter_;
    AudioService& audio_;
    std::array<std::int16_t, windowSamples> samples_{};
    MicrophoneSnapshot snapshot_{};
    bool baselineReady_ = false;
    float peak_ = 0;
    std::chrono::milliseconds sinceWindow_{0};
};

} // namespace cardputer_hub::services
