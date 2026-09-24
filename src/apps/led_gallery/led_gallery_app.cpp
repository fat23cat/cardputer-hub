#include "apps/led_gallery/led_gallery_app.h"

#include "core/display/palette.h"

#include <cctype>
#include <cstdio>

namespace cardputer_hub::apps {
namespace {
constexpr std::chrono::milliseconds ledFrameInterval{50};

bool plain(const core::InputEvent& event) {
    return !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
}
bool actionKey(LedGalleryEffect effect, char key) {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    if (effect == LedGalleryEffect::ParticleStorm)
        return key >= 32 && key <= 126;
    if (effect == LedGalleryEffect::Ripple || effect == LedGalleryEffect::LangtonsAnt)
        return key == ' ';
    if (effect == LedGalleryEffect::GameOfLife)
        return key == ' ' || lower == 'g';
    if (effect == LedGalleryEffect::FallingSand || effect == LedGalleryEffect::RuleMachine)
        return key == ' ' || lower == 'a' || lower == 'd' ||
               (effect == LedGalleryEffect::RuleMachine && (lower == 'w' || lower == 's'));
    if (effect == LedGalleryEffect::TetrisDream)
        return key == ' ' || lower == 'a' || lower == 'd' || lower == 'w' || lower == 's';
    if (effect == LedGalleryEffect::ReactionDiffusion || effect == LedGalleryEffect::Fire ||
        effect == LedGalleryEffect::GravityWell || effect == LedGalleryEffect::Swarm ||
        effect == LedGalleryEffect::ElectricStorm)
        return key == ' ' || lower == 'w' || lower == 'a' || lower == 's' || lower == 'd';
    return false;
}
} // namespace
LedGalleryApp::LedGalleryApp(services::IndicatorService& indicator, core::IDisplayAdapter& display,
                             std::uint32_t seed)
    : indicator_(indicator), display_(display), engine_(seed), seed_(seed ? seed : 1) {}

void LedGalleryApp::onActivate() {
    claim_ = indicator_.acquire(services::ledGalleryIndicatorOwner,
                                services::IndicatorPriority::ForegroundApplication);
    engine_.reset(effect_, seed_);
    outputElapsed_ = {};
    feedback_[0] = 0;
    claim_.setFrame(engine_.frame());
    redraw_ = true;
}
void LedGalleryApp::onDeactivate() {
    claim_.release();
    outputElapsed_ = {};
    feedback_[0] = 0;
    redraw_ = true;
}
void LedGalleryApp::select(LedGalleryEffect effect) {
    if (effect == effect_)
        return;
    effect_ = effect;
    engine_.reset(effect_, seed_);
    outputElapsed_ = {};
    feedback_[0] = 0;
    redraw_ = true;
}
void LedGalleryApp::draw() {
    display_.clear(core::palette::bone);
    const core::TextStyle ink{core::palette::ink, core::palette::bone, 1};
    const core::TextStyle muted{core::palette::ordinal, core::palette::bone, 1};
    display_.drawText({6, 6}, "LED GALLERY", ink);
    char number[8];
    const auto index = static_cast<unsigned>(effect_);
    std::snprintf(number, sizeof(number), "%02u/20", index + 1);
    display_.drawText({20, 35}, number, {core::palette::blue, core::palette::bone, 2});
    display_.drawText({105, 42}, ledGalleryEffects[index].name, ink);
    display_.drawText({6, 121}, "< > EFFECT", muted);
    display_.drawText({94, 121}, "1-0 / FN+1-0", muted);
    if (ledGalleryEffects[index].actions)
        display_.drawText({6, 107}, ledGalleryEffects[index].actions, muted);
    if (feedback_[0])
        display_.drawText({6, 76}, feedback_, ink);
}
void LedGalleryApp::update(const core::InputEvents& input, std::chrono::milliseconds elapsed) {
    bool changedFrame = false;
    if (elapsed.count() > 0 && feedback_[0]) {
        feedbackElapsed_ += elapsed;
        if (feedbackElapsed_ >= std::chrono::milliseconds{1000}) {
            feedback_[0] = 0;
            redraw_ = true;
        }
    }
    for (const auto& event : input) {
        if (!plain(event))
            continue;
        if (event.type == core::InputEventType::NamedKey) {
            if (event.modifiers.shift)
                continue;
            if (event.modifiers.fn && event.namedKey >= core::NamedKey::F1 &&
                event.namedKey <= core::NamedKey::F10) {
                select(static_cast<LedGalleryEffect>(10 + static_cast<unsigned>(event.namedKey) -
                                                     static_cast<unsigned>(core::NamedKey::F1)));
                changedFrame = true;
                continue;
            }
            if (event.modifiers.fn)
                continue;
            if (event.namedKey == core::NamedKey::Left || event.namedKey == core::NamedKey::Right) {
                const auto index = static_cast<unsigned>(effect_);
                select(static_cast<LedGalleryEffect>(
                    event.namedKey == core::NamedKey::Left
                        ? (index + ledGalleryEffects.size() - 1U) % ledGalleryEffects.size()
                        : (index + 1U) % ledGalleryEffects.size()));
                changedFrame = true;
            }
            continue;
        }
        const char key = event.character;
        if (event.modifiers.fn && key >= '0' && key <= '9' && !event.modifiers.shift) {
            select(static_cast<LedGalleryEffect>(10 + (key == '0' ? 9 : key - '1')));
            changedFrame = true;
        } else if (event.modifiers.fn) {
            continue;
        } else if (!event.modifiers.shift && (key == ',' || key == '/')) {
            const auto index = static_cast<unsigned>(effect_);
            select(static_cast<LedGalleryEffect>(
                key == ',' ? (index + ledGalleryEffects.size() - 1U) % ledGalleryEffects.size()
                           : (index + 1U) % ledGalleryEffects.size()));
            changedFrame = true;
        } else if (!event.modifiers.shift && key >= '1' && key <= '9') {
            select(static_cast<LedGalleryEffect>(key - '1'));
            changedFrame = true;
        } else if (!event.modifiers.shift && key == '0') {
            select(LedGalleryEffect::ParticleStorm);
            changedFrame = true;
        } else if (actionKey(effect_, key)) {
            engine_.interact(key);
            changedFrame = true;
            if (engine_.status()[0]) {
                std::snprintf(feedback_, sizeof(feedback_), "%s", engine_.status());
                feedbackElapsed_ = {};
                redraw_ = true;
            }
        }
    }
    if (elapsed.count() > 0) {
        engine_.advance(elapsed);
        // Cap backlog before adding it; only one current frame is ever published.
        outputElapsed_ +=
            elapsed >= ledFrameInterval ? ledFrameInterval + elapsed % ledFrameInterval : elapsed;
    }
    if (changedFrame || outputElapsed_ >= ledFrameInterval) {
        claim_.setFrame(engine_.frame());
        outputElapsed_ =
            changedFrame ? std::chrono::milliseconds{0} : outputElapsed_ % ledFrameInterval;
    }
    if (redraw_) {
        draw();
        redraw_ = false;
    }
}
} // namespace cardputer_hub::apps
