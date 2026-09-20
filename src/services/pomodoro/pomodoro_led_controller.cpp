#include "services/pomodoro/pomodoro_led_controller.h"

#include "core/power/display_power_controller.h"
#include "services/audio/audio_service.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace cardputer_hub::services {
namespace {
bool sameFrame(const IndicatorFrame& left, const IndicatorFrame& right) noexcept {
    for (std::size_t i = 0; i < core::ledMatrixPixelCount; ++i) {
        const auto& a = left.pixels[i];
        const auto& b = right.pixels[i];
        if (a.red != b.red || a.green != b.green || a.blue != b.blue)
            return false;
    }
    return true;
}

void addPendingCues(std::uint8_t& pending, std::size_t count) noexcept {
    const auto room = static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max() - pending);
    pending = static_cast<std::uint8_t>(pending + std::min(count, room));
}

void advanceTransition(std::chrono::milliseconds& remaining, std::chrono::milliseconds elapsed) {
    elapsed = std::max(elapsed, std::chrono::milliseconds(0));
    if (elapsed >= remaining)
        remaining = {};
    else
        remaining -= elapsed;
}
} // namespace

std::uint8_t pomodoroLitPixels(const PomodoroSnapshot& snapshot) noexcept {
    if (snapshot.remaining.count() <= 0 || snapshot.duration.count() <= 0)
        return 0;
    const auto lit =
        (snapshot.remaining.count() * static_cast<std::int64_t>(core::ledMatrixPixelCount) +
         snapshot.duration.count() - 1) /
        snapshot.duration.count();
    if (lit <= 0)
        return 0;
    if (lit >= static_cast<std::int64_t>(core::ledMatrixPixelCount))
        return static_cast<std::uint8_t>(core::ledMatrixPixelCount);
    return static_cast<std::uint8_t>(lit);
}

core::RgbColor pomodoroPhaseColor(PomodoroPhase phase) noexcept {
    return phase == PomodoroPhase::Work ? pomodoroWorkLed : pomodoroBreakLed;
}

IndicatorFrame pomodoroProgressFrame(core::RgbColor color, std::uint8_t litPixels) noexcept {
    IndicatorFrame frame{};
    const auto lit = std::min<std::uint8_t>(litPixels, core::ledMatrixPixelCount);
    const auto offPixels = static_cast<std::uint8_t>(core::ledMatrixPixelCount - lit);
    for (std::uint8_t i = offPixels; i < core::ledMatrixPixelCount; ++i)
        frame.pixels[i] = color;
    return frame;
}

PomodoroLedController::PomodoroLedController(PomodoroService& pomodoro, IndicatorService& indicator,
                                             AudioService* audio,
                                             core::DisplayPowerController* displayPower)
    : pomodoro_(pomodoro), indicator_(indicator), audio_(audio), displayPower_(displayPower) {}

void PomodoroLedController::acquireIfNeeded() {
    if (!claim_.valid())
        claim_ =
            indicator_.acquire(pomodoroIndicatorOwner, IndicatorPriority::BackgroundApplication);
}

void PomodoroLedController::playPendingCues() {
    if (audio_ == nullptr)
        return;
    while (pendingCues_ > 0) {
        if (!audio_->play(AudioCue::StepRight))
            return;
        --pendingCues_;
    }
}

void PomodoroLedController::idle() {
    std::array<PomodoroTransition, 8> discarded{};
    (void)pomodoro_.takeTransitions(discarded.data(), discarded.size());
    claim_.release();
    havePreviousFrame_ = false;
    previousFrame_ = {};
    transitionRemaining_ = {};
    pendingCues_ = 0;
}

void PomodoroLedController::update(std::chrono::milliseconds elapsed) {
    const auto snapshot = pomodoro_.snapshot();
    if (snapshot.runState == PomodoroRunState::Idle) {
        idle();
        return;
    }

    acquireIfNeeded();
    std::array<PomodoroTransition, 8> transitions{};
    const auto transitionCount = pomodoro_.takeTransitions(transitions.data(), transitions.size());
    if (transitionCount > 0) {
        transitionRemaining_ = pomodoroTransitionFeedback;
        addPendingCues(pendingCues_, transitionCount);
        if (displayPower_ != nullptr)
            displayPower_->requestWake();
    } else if (snapshot.runState == PomodoroRunState::Running && transitionRemaining_.count() > 0) {
        advanceTransition(transitionRemaining_, elapsed);
    }

    const auto color = pomodoroPhaseColor(snapshot.phase);
    const auto lit = transitionRemaining_.count() > 0
                         ? static_cast<std::uint8_t>(core::ledMatrixPixelCount)
                         : pomodoroLitPixels(snapshot);
    const auto frame = pomodoroProgressFrame(color, lit);
    if (!havePreviousFrame_ || !sameFrame(previousFrame_, frame))
        claim_.setFrame(frame);
    previousFrame_ = frame;
    havePreviousFrame_ = true;

    playPendingCues();
}

} // namespace cardputer_hub::services
