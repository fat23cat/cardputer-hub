#include "core/power/display_power_controller.h"

#include <algorithm>

namespace cardputer_hub::core {

DisplayPowerController::DisplayPowerController(IBacklightAdapter& backlight) noexcept
    : backlight_(backlight) {}

void DisplayPowerController::captureNormalLevel() {
    const auto existing = backlight_.level();
    // Nothing sets brightness before this runs, and M5GFX initializes its panel
    // to a non-zero level, so a zero reading means the level is unavailable
    // rather than an intentional dark screen.
    normalLevel_ = existing > 0 ? existing : maximumLevel;
    state_ = DisplayPowerState::Awake;
    stateElapsed_ = {};
    rampElapsed_ = {};
    rampDuration_ = {};
    if (existing != normalLevel_)
        backlight_.setLevel(normalLevel_);
    writtenLevel_ = normalLevel_;
}

std::uint8_t DisplayPowerController::dimLevel() const noexcept {
    if (normalLevel_ == 0)
        return 0;
    const auto scaled = (static_cast<std::int32_t>(normalLevel_) * dimPercent + 50) / 100;
    // Quantization must not collapse a lit display to zero.
    return static_cast<std::uint8_t>(std::max<std::int32_t>(1, scaled));
}

std::uint8_t DisplayPowerController::currentLevel() const noexcept {
    switch (state_) {
    case DisplayPowerState::Awake:
        return normalLevel_;
    case DisplayPowerState::Dimmed:
        return dimLevel();
    case DisplayPowerState::Off:
        return 0;
    case DisplayPowerState::Dimming:
    case DisplayPowerState::TurningOff:
    case DisplayPowerState::Waking:
        break;
    }
    if (rampDuration_.count() <= 0)
        return rampTo_;
    const auto from = static_cast<std::int64_t>(rampFrom_);
    const auto to = static_cast<std::int64_t>(rampTo_);
    const auto travelled = (to - from) * rampElapsed_.count() / rampDuration_.count();
    return static_cast<std::uint8_t>(from + travelled);
}

void DisplayPowerController::beginRamp(std::uint8_t target, std::chrono::milliseconds duration) {
    rampFrom_ = currentLevel();
    rampTo_ = target;
    rampDuration_ = duration;
    rampElapsed_ = {};
}

void DisplayPowerController::startWake() {
    // beginRamp starts from the brightness on screen, not the cancelled fade's target.
    beginRamp(normalLevel_, wakeRampDuration);
    state_ = DisplayPowerState::Waking;
}

std::chrono::milliseconds DisplayPowerController::advance(std::chrono::milliseconds remaining) {
    switch (state_) {
    case DisplayPowerState::Awake: {
        const auto need = idleThreshold - stateElapsed_;
        if (remaining < need) {
            stateElapsed_ += remaining;
            return {};
        }
        stateElapsed_ = {};
        beginRamp(dimLevel(), dimRampDuration);
        state_ = DisplayPowerState::Dimming;
        return remaining - need;
    }
    case DisplayPowerState::Dimmed: {
        const auto need = dimHoldDuration - stateElapsed_;
        if (remaining < need) {
            stateElapsed_ += remaining;
            return {};
        }
        stateElapsed_ = {};
        beginRamp(0, offRampDuration);
        state_ = DisplayPowerState::TurningOff;
        return remaining - need;
    }
    case DisplayPowerState::Dimming:
    case DisplayPowerState::TurningOff:
    case DisplayPowerState::Waking: {
        const auto need = rampDuration_ - rampElapsed_;
        if (remaining < need) {
            rampElapsed_ += remaining;
            return {};
        }
        rampElapsed_ = rampDuration_;
        if (state_ == DisplayPowerState::Dimming) {
            // The hold begins only once the dim target is actually reached.
            state_ = DisplayPowerState::Dimmed;
            stateElapsed_ = {};
        } else if (state_ == DisplayPowerState::TurningOff) {
            state_ = DisplayPowerState::Off;
        } else {
            state_ = DisplayPowerState::Awake;
            stateElapsed_ = {};
        }
        return remaining - need;
    }
    case DisplayPowerState::Off:
        break;
    }
    return {};
}

bool DisplayPowerController::update(std::chrono::milliseconds elapsed, bool physicalPress) {
    // Input sampled on this frame belongs to the state the display was in when
    // the key was pressed, so resolve the press before advancing time.
    const bool wakeOnly = state_ != DisplayPowerState::Awake;
    if (physicalPress) {
        if (!wakeOnly) {
            // A press sampled in this frame proves the interval was not idle
            // throughout, so the same elapsed time cannot also start a dim ramp.
            stateElapsed_ = {};
            writeLevel();
            return false;
        }
        if (state_ != DisplayPowerState::Waking) {
            // The ramp exists only because of this press, so it cannot inherit
            // elapsed time that passed before the press was sampled.
            startWake();
            writeLevel();
            return true;
        }
        // Already Waking: consume the press but let elapsed advance the ramp
        // below, so repeated presses neither restart nor extend it.
    }

    auto remaining = std::max(elapsed, std::chrono::milliseconds::zero());
    while (remaining.count() > 0)
        remaining = advance(remaining);

    writeLevel();
    return wakeOnly;
}

void DisplayPowerController::requestWake() {
    if (state_ == DisplayPowerState::Awake) {
        stateElapsed_ = {};
        writeLevel();
        return;
    }
    if (state_ == DisplayPowerState::Waking)
        return;
    startWake();
    writeLevel();
}

void DisplayPowerController::writeLevel() {
    const auto level = currentLevel();
    if (level == writtenLevel_)
        return;
    writtenLevel_ = level;
    backlight_.setLevel(level);
}

} // namespace cardputer_hub::core
