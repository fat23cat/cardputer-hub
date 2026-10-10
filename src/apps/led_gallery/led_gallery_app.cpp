#include "apps/led_gallery/led_gallery_app.h"

#include "core/display/contextual_footer.h"
#include "core/display/palette.h"
#include "core/display/screen_header.h"
#include "core/display/text_layout.h"

#include <cctype>
#include <cstdio>

namespace cardputer_hub::apps {
namespace {
constexpr std::chrono::milliseconds ledFrameInterval{50};
constexpr float effectNameScale = 2.0f;
constexpr std::int32_t effectNameY = 42;
constexpr std::int32_t feedbackY = 74;
constexpr std::int32_t actionsY = 109;
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
    : indicator_(indicator), display_(display), seed_(seed ? seed : 1) {}

void LedGalleryApp::onActivate() {
    claim_ = indicator_.acquire(services::ledGalleryIndicatorOwner,
                                services::IndicatorPriority::ForegroundApplication);
    engine_ = std::make_unique<LedGalleryEngine>(seed_);
    engine_->reset(effect_, seed_);
    outputElapsed_ = {};
    feedback_[0] = 0;
    claim_.setFrame(engine_->frame());
    redraw_ = true;
}
void LedGalleryApp::onDeactivate() {
    claim_.release();
    engine_.reset();
    outputElapsed_ = {};
    feedback_[0] = 0;
    redraw_ = true;
}
void LedGalleryApp::select(LedGalleryEffect effect) {
    if (effect == effect_)
        return;
    effect_ = effect;
    engine_->reset(effect_, seed_);
    outputElapsed_ = {};
    feedback_[0] = 0;
    redraw_ = true;
}
void LedGalleryApp::draw() {
    using namespace core;
    display_.clear(palette::bone);
    const TextStyle ink{palette::ink, palette::bone, systemTextScale};
    const TextStyle muted{palette::ordinal, palette::bone, systemTextScale};
    char number[8];
    const auto index = static_cast<unsigned>(effect_);
    std::snprintf(number, sizeof(number), "%02u/20", index + 1);
    drawScreenHeader(display_, "LED GALLERY", number);
    const char* name = ledGalleryEffects[index].name;
    display_.drawText({centeredTextX(name, 0, 240, effectNameScale), effectNameY}, name,
                      {palette::ink, palette::bone, effectNameScale});
    if (feedback_[0])
        display_.drawText({centeredTextX(feedback_, 0, 240), feedbackY}, feedback_, ink);
    if (ledGalleryEffects[index].actions)
        display_.drawText({screenHeaderMarginX, actionsY}, ledGalleryEffects[index].actions, muted);
    drawContextualFooter(display_, "< > EFFECT", "1-0 / FN+1-0");
}

void LedGalleryApp::step(int delta) {
    const auto count = static_cast<unsigned>(ledGalleryEffects.size());
    const auto index = static_cast<unsigned>(effect_);
    select(static_cast<LedGalleryEffect>(delta < 0 ? (index + count - 1U) % count
                                                   : (index + 1U) % count));
}
void LedGalleryApp::update(const core::InputEvents& input, std::chrono::milliseconds elapsed) {
    if (!engine_)
        return;
    bool changedFrame = false;
    if (elapsed.count() > 0 && feedback_[0]) {
        feedbackElapsed_ += elapsed;
        if (feedbackElapsed_ >= std::chrono::milliseconds{1000}) {
            feedback_[0] = 0;
            redraw_ = true;
        }
    }
    for (const auto& event : input) {
        if (!core::isPlainInput(event))
            continue;
        if (event.type == core::InputEventType::NamedKey) {
            if (event.modifiers.shift)
                continue;
            if (event.modifiers.fn && event.namedKey >= core::NamedKey::F1 &&
                event.namedKey <= core::NamedKey::F10) {
                select(static_cast<LedGalleryEffect>(10 + static_cast<unsigned>(event.namedKey) -
                                                     static_cast<unsigned>(core::NamedKey::F1)));
                changedFrame = true;
            } else if (core::isPageLeft(event) || core::isPageRight(event)) {
                step(core::isPageLeft(event) ? -1 : 1);
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
        } else if (core::isPageLeft(event) || core::isPageRight(event)) {
            step(core::isPageLeft(event) ? -1 : 1);
            changedFrame = true;
        } else if (!event.modifiers.shift && key >= '1' && key <= '9') {
            select(static_cast<LedGalleryEffect>(key - '1'));
            changedFrame = true;
        } else if (!event.modifiers.shift && key == '0') {
            select(LedGalleryEffect::ParticleStorm);
            changedFrame = true;
        } else if (actionKey(effect_, key)) {
            engine_->interact(key);
            changedFrame = true;
            if (engine_->status()[0]) {
                std::snprintf(feedback_, sizeof(feedback_), "%s", engine_->status());
                feedbackElapsed_ = {};
                redraw_ = true;
            }
        }
    }
    if (elapsed.count() > 0) {
        engine_->advance(elapsed);
        // Cap backlog before adding it; only one current frame is ever published.
        outputElapsed_ +=
            elapsed >= ledFrameInterval ? ledFrameInterval + elapsed % ledFrameInterval : elapsed;
    }
    if (changedFrame || outputElapsed_ >= ledFrameInterval) {
        claim_.setFrame(engine_->frame());
        outputElapsed_ =
            changedFrame ? std::chrono::milliseconds{0} : outputElapsed_ % ledFrameInterval;
    }
    if (redraw_) {
        draw();
        redraw_ = false;
    }
}
} // namespace cardputer_hub::apps
