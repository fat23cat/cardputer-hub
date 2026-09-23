#include "apps/sound_reactive/sound_reactive_app.h"

#include "apps/sound_reactive/sound_reactive_graphics.h"
#include "services/indicator/indicator_service.h"

#include <algorithm>
#include <cmath>

namespace cardputer_hub::apps {

void SoundReactiveApp::onActivate() {
    phase_ = 0;
    colorLevel_ = 0;
    lcdElapsed_ = std::chrono::milliseconds{0};
    ledElapsed_ = std::chrono::milliseconds{0};
    firstFrame_ = true;
    failureDisplayed_ = false;
    diagnosticsVisible_ = false;
    if (microphone_.start())
        claim_ = indicator_.acquire(services::soundReactiveIndicatorOwner,
                                    services::IndicatorPriority::ForegroundApplication);
}

void SoundReactiveApp::onDeactivate() {
    claim_.release();
    microphone_.stop();
    phase_ = 0;
    colorLevel_ = 0;
    firstFrame_ = true;
    failureDisplayed_ = false;
    diagnosticsVisible_ = false;
}

void SoundReactiveApp::update(const core::InputEvents& input, std::chrono::milliseconds elapsed) {
    for (const auto& event : input) {
        if (event.type == core::InputEventType::PrintableCharacter &&
            (event.character == 'd' || event.character == 'D') && !event.modifiers.ctrl &&
            !event.modifiers.alt && !event.modifiers.option) {
            diagnosticsVisible_ = !diagnosticsVisible_;
            firstFrame_ = true;
        }
    }
    const auto snapshot = microphone_.snapshot();
    if (snapshot.state != services::MicrophoneState::Capturing) {
        claim_.release();
        if (!failureDisplayed_) {
            drawSoundReactiveFailure(display_);
            failureDisplayed_ = true;
        }
        return;
    }
    const auto dt = std::max(elapsed, std::chrono::milliseconds{0});
    const float seconds = static_cast<float>(dt.count()) / 1000.0f;
    phase_ = std::fmod(phase_ + seconds * soundReactiveSpeed(snapshot.normalizedLevel), 1.0f);
    const float colorTau = snapshot.normalizedLevel > colorLevel_ ? 120.0f : 700.0f;
    colorLevel_ += (snapshot.normalizedLevel - colorLevel_) *
                   (1.0f - std::exp(-static_cast<float>(dt.count()) / colorTau));
    constexpr auto lcdInterval = std::chrono::milliseconds{34};
    constexpr auto ledInterval = std::chrono::milliseconds{55};
    const bool redrawLcd = firstFrame_ || dt >= lcdInterval || lcdElapsed_ >= lcdInterval - dt;
    const bool redrawLed = firstFrame_ || dt >= ledInterval || ledElapsed_ >= ledInterval - dt;
    lcdElapsed_ = (lcdElapsed_ + dt % lcdInterval) % lcdInterval;
    ledElapsed_ = (ledElapsed_ + dt % ledInterval) % ledInterval;
    const auto color = soundReactiveColor(colorLevel_);
    if (redrawLcd) {
        drawSoundReactiveScreen(display_, phase_, snapshot.normalizedLevel, color);
        if (diagnosticsVisible_)
            drawSoundReactiveDiagnostics(display_, snapshot);
    }
    if (claim_.valid() && redrawLed) {
        claim_.setFrame(soundReactiveLedFrame(phase_, snapshot.normalizedLevel, color));
    }
    firstFrame_ = false;
}

} // namespace cardputer_hub::apps
