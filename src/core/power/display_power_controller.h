#pragma once

#include <chrono>
#include <cstdint>

#include "core/power/backlight_adapter.h"

namespace cardputer_hub::core {

enum class DisplayPowerState : std::uint8_t {
    Awake,
    Dimming,
    Dimmed,
    TurningOff,
    Off,
    Waking,
};

// Global backlight idle policy. Off means backlight zero; nothing here sleeps
// the ESP32 or touches UI, Actions or Services.
class DisplayPowerController {
  public:
    static constexpr auto idleThreshold = std::chrono::milliseconds(15000);
    static constexpr auto dimRampDuration = std::chrono::milliseconds(300);
    static constexpr auto dimHoldDuration = std::chrono::milliseconds(120000);
    static constexpr auto offRampDuration = std::chrono::milliseconds(400);
    static constexpr auto wakeRampDuration = std::chrono::milliseconds(200);
    static constexpr std::uint8_t dimPercent = 10;
    static constexpr std::uint8_t maximumLevel = 255;

    explicit DisplayPowerController(IBacklightAdapter& backlight) noexcept;

    // Adopts the brightness the firmware already uses. Requires an initialized
    // platform/display.
    void captureNormalLevel();

    // Returns true when this frame's input is wake-only and must be consumed.
    bool update(std::chrono::milliseconds elapsed, bool physicalPress);

    // Programmatic attention: restores visibility without synthesizing input.
    // Background features may call this for a meaningful user-visible event.
    void requestWake();

    DisplayPowerState state() const noexcept { return state_; }
    bool displayOff() const noexcept { return state_ == DisplayPowerState::Off; }
    std::uint8_t normalLevel() const noexcept { return normalLevel_; }
    std::uint8_t dimLevel() const noexcept;

  private:
    std::uint8_t currentLevel() const noexcept;
    void beginRamp(std::uint8_t target, std::chrono::milliseconds duration);
    void startWake();
    std::chrono::milliseconds advance(std::chrono::milliseconds remaining);
    void writeLevel();

    IBacklightAdapter& backlight_;
    DisplayPowerState state_ = DisplayPowerState::Awake;
    std::uint8_t normalLevel_ = maximumLevel;
    std::uint8_t writtenLevel_ = maximumLevel;
    std::uint8_t rampFrom_ = 0;
    std::uint8_t rampTo_ = 0;
    std::chrono::milliseconds rampDuration_{0};
    std::chrono::milliseconds rampElapsed_{0};
    // Idle time while Awake; dim-hold time while Dimmed.
    std::chrono::milliseconds stateElapsed_{0};
};

} // namespace cardputer_hub::core
