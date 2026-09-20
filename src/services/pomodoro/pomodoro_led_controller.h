#pragma once

#include "services/indicator/indicator_service.h"
#include "services/pomodoro/pomodoro_service.h"

#include <chrono>
#include <cstdint>

namespace cardputer_hub::core {
class DisplayPowerController;
}

namespace cardputer_hub::services {

class AudioService;

inline constexpr std::chrono::milliseconds pomodoroTransitionFeedback{400};
// Muted steel / sage. Chosen so 3% quantization still leaves opposite hues:
// Work ≈ (2,2,4), Break ≈ (2,4,1). Near-gray pairs collapse on WS2812.
inline constexpr core::RgbColor pomodoroWorkLed{0x38, 0x4C, 0x7A};
inline constexpr core::RgbColor pomodoroBreakLed{0x4C, 0x7A, 0x30};

[[nodiscard]] std::uint8_t pomodoroLitPixels(const PomodoroSnapshot& snapshot) noexcept;
[[nodiscard]] core::RgbColor pomodoroPhaseColor(PomodoroPhase phase) noexcept;
[[nodiscard]] IndicatorFrame pomodoroProgressFrame(core::RgbColor color,
                                                   std::uint8_t litPixels) noexcept;

class PomodoroLedController {
  public:
    PomodoroLedController(PomodoroService& pomodoro, IndicatorService& indicator,
                          AudioService* audio = nullptr,
                          core::DisplayPowerController* displayPower = nullptr);

    void update(std::chrono::milliseconds elapsed);

  private:
    void acquireIfNeeded();
    void playPendingCues();
    void idle();

    PomodoroService& pomodoro_;
    IndicatorService& indicator_;
    AudioService* audio_ = nullptr;
    core::DisplayPowerController* displayPower_ = nullptr;
    IndicatorClaim claim_;
    IndicatorFrame previousFrame_{};
    bool havePreviousFrame_ = false;
    std::chrono::milliseconds transitionRemaining_{0};
    std::uint8_t pendingCues_ = 0;
};

} // namespace cardputer_hub::services
