#include "apps/led_gallery/led_gallery_app.h"

#include "core/display/palette.h"

#include <cstdio>

namespace cardputer_hub::apps {
namespace {
constexpr std::chrono::milliseconds ledFrameInterval{50};

bool plain(const core::InputEvent& event) {
    return !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
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
    claim_.setFrame(engine_.frame());
    redraw_ = true;
}
void LedGalleryApp::onDeactivate() {
    claim_.release();
    outputElapsed_ = {};
    redraw_ = true;
}
void LedGalleryApp::select(LedGalleryEffect effect) {
    if (effect == effect_)
        return;
    effect_ = effect;
    engine_.reset(effect_, seed_);
    outputElapsed_ = {};
    redraw_ = true;
}
void LedGalleryApp::draw() {
    display_.clear(core::palette::bone);
    const core::TextStyle ink{core::palette::ink, core::palette::bone, 1};
    const core::TextStyle muted{core::palette::ordinal, core::palette::bone, 1};
    display_.drawText({6, 6}, "LED GALLERY", ink);
    char number[4];
    const auto index = static_cast<unsigned>(effect_);
    std::snprintf(number, sizeof(number), "%02u", index + 1);
    display_.drawText({20, 35}, number, {core::palette::blue, core::palette::bone, 2});
    display_.drawText({70, 42}, ledGalleryEffects[index].name, ink);
    display_.fillRectangle({6, 111}, 10, 1, core::palette::ordinal);
    for (int offset = 0; offset < 4; ++offset) {
        display_.fillRectangle({12 + offset, 107 + offset}, 1, 1, core::palette::ordinal);
        display_.fillRectangle({12 + offset, 115 - offset}, 1, 1, core::palette::ordinal);
    }
    display_.drawText({22, 107}, "NEXT EFFECT", muted);
    display_.drawText({6, 121}, "1-0 DIRECT", muted);
    if (effect_ == LedGalleryEffect::Ripple)
        display_.drawText({120, 121}, "SPACE RIPPLE", muted);
    else if (effect_ == LedGalleryEffect::ParticleStorm)
        display_.drawText({120, 121}, "SPACE BURST", muted);
}
void LedGalleryApp::update(const core::InputEvents& input, std::chrono::milliseconds elapsed) {
    bool changedFrame = false;
    for (const auto& event : input) {
        if (!plain(event))
            continue;
        if (event.type == core::InputEventType::NamedKey) {
            if (event.modifiers.shift)
                continue;
            if (event.namedKey == core::NamedKey::Left || event.namedKey == core::NamedKey::Right) {
                const auto index = static_cast<unsigned>(effect_);
                select(static_cast<LedGalleryEffect>(event.namedKey == core::NamedKey::Left
                                                         ? (index + 9U) % 10U
                                                         : (index + 1U) % 10U));
                changedFrame = true;
            }
            continue;
        }
        if (event.modifiers.fn)
            continue;
        const char key = event.character;
        if (!event.modifiers.shift && (key == ',' || key == '/')) {
            const auto index = static_cast<unsigned>(effect_);
            select(static_cast<LedGalleryEffect>(key == ',' ? (index + 9U) % 10U
                                                            : (index + 1U) % 10U));
            changedFrame = true;
        } else if (!event.modifiers.shift && key >= '1' && key <= '9') {
            select(static_cast<LedGalleryEffect>(key - '1'));
            changedFrame = true;
        } else if (!event.modifiers.shift && key == '0') {
            select(LedGalleryEffect::ParticleStorm);
            changedFrame = true;
        } else if (key == ' ' ||
                   (effect_ == LedGalleryEffect::ParticleStorm && key >= 33 && key <= 126)) {
            engine_.interact(key);
            changedFrame = true;
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
