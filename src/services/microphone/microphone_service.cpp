#include "services/microphone/microphone_service.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cardputer_hub::services {
namespace {
float approach(float current, float target, float elapsedMs, float timeMs) {
    const float coefficient = 1.0f - std::exp(-elapsedMs / timeMs);
    return current + (target - current) * coefficient;
}
} // namespace

bool MicrophoneService::start() {
    if (snapshot_.state == MicrophoneState::Capturing)
        return true;
    snapshot_ = {};
    baselineReady_ = false;
    peak_ = 0;
    sinceWindow_ = std::chrono::milliseconds{0};
    if (!audio_.suspend() || !adapter_.begin(sampleRate)) {
        adapter_.end();
        (void)audio_.resume();
        snapshot_.state = MicrophoneState::Failed;
        return false;
    }
    snapshot_.state = MicrophoneState::Capturing;
    return true;
}

void MicrophoneService::stop() {
    if (snapshot_.state == MicrophoneState::Capturing)
        adapter_.end();
    (void)audio_.resume();
    snapshot_ = {};
    baselineReady_ = false;
    peak_ = 0;
    sinceWindow_ = std::chrono::milliseconds{0};
}

float MicrophoneService::windowDbfs(const std::int16_t* samples, std::size_t sampleCount) noexcept {
    if (samples == nullptr || sampleCount == 0)
        return -96.0f;
    double sum = 0;
    for (std::size_t i = 0; i < sampleCount; ++i)
        sum += samples[i];
    const double mean = sum / static_cast<double>(sampleCount);
    double squared = 0;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const double centered = samples[i] - mean;
        squared += centered * centered;
    }
    const double rms = std::sqrt(squared / static_cast<double>(sampleCount));
    if (rms <= 0)
        return -96.0f;
    return static_cast<float>(std::max(-96.0, 20.0 * std::log10(rms / 32768.0)));
}

void MicrophoneService::update(std::chrono::milliseconds elapsed) {
    if (snapshot_.state != MicrophoneState::Capturing)
        return;
    sinceWindow_ += std::max(elapsed, std::chrono::milliseconds{0});
    const auto result = adapter_.read(samples_.data(), samples_.size());
    if (result == core::MicrophoneReadResult::Pending)
        return;
    if (result == core::MicrophoneReadResult::Error) {
        adapter_.end();
        (void)audio_.resume();
        snapshot_ = {};
        snapshot_.state = MicrophoneState::Failed;
        baselineReady_ = false;
        peak_ = 0;
        sinceWindow_ = std::chrono::milliseconds{0};
        return;
    }
    const float dt = static_cast<float>(std::max<std::int64_t>(1, sinceWindow_.count()));
    sinceWindow_ = std::chrono::milliseconds{0};
    const float dbfs = windowDbfs(samples_.data(), samples_.size());
    const auto [minimum, maximum] = std::minmax_element(samples_.begin(), samples_.end());
    snapshot_.sampleMin = *minimum;
    snapshot_.sampleMax = *maximum;
    snapshot_.dbfs = dbfs;
    if (snapshot_.windowCount != std::numeric_limits<std::uint32_t>::max())
        ++snapshot_.windowCount;
    // A 24 dB presentation range follows the slowly adapting room floor.
    const float level = std::clamp((dbfs + 96.0f) / 96.0f, 0.0f, 1.0f);
    if (!baselineReady_) {
        snapshot_.ambientLevel = std::min(level, 0.65f);
        baselineReady_ = true;
    }
    const float floor = std::max(0.0f, snapshot_.ambientLevel - 0.04f);
    const float target = std::clamp((level - floor) * 4.0f, 0.0f, 1.0f);
    snapshot_.ambientLevel = std::min(0.70f, approach(snapshot_.ambientLevel, level, dt, 20000.0f));
    snapshot_.rawLevel = level;
    snapshot_.normalizedLevel = approach(snapshot_.normalizedLevel, target, dt,
                                         target > snapshot_.normalizedLevel ? 65.0f : 550.0f);
    peak_ = std::max(snapshot_.normalizedLevel, approach(peak_, 0.0f, dt, 900.0f));
    snapshot_.peakLevel = peak_;
}

} // namespace cardputer_hub::services
