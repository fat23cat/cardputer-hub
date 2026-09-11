#include "hardware/cardputer/cardputer_display_adapter.h"
#include <M5Unified.hpp>
#include <algorithm>
#include <cstring>
#include <new>

namespace cardputer_hub::hardware {
namespace {
std::uint32_t toDeviceColor(core::RgbColor color) {
    return M5.Display.color888(color.red, color.green, color.blue);
}
} // namespace

struct CardputerDisplayAdapter::Frame {
    M5Canvas canvas{&M5.Display};
    M5Canvas previous{&M5.Display};
    core::SlideTransition slide;
    bool snapshotUsable = false;
    bool transitionRequested = false;
    int presentedOffset = -1;
    bool usable = false;
    bool active = false;
    std::int32_t left = 240, top = 135, right = 0, bottom = 0;
    Frame() {
        canvas.setColorDepth(16);
        usable = canvas.createSprite(240, 135) != nullptr;
    }
    void damage(core::PixelPosition position, std::int32_t width, std::int32_t height) {
        if (!active || width <= 0 || height <= 0)
            return;
        left = std::min(left, std::max<std::int32_t>(0, position.x));
        top = std::min(top, std::max<std::int32_t>(0, position.y));
        right = std::max(right, std::min<std::int32_t>(240, position.x + width));
        bottom = std::max(bottom, std::min<std::int32_t>(135, position.y + height));
    }
};

CardputerDisplayAdapter::CardputerDisplayAdapter() = default;
CardputerDisplayAdapter::~CardputerDisplayAdapter() = default;

void CardputerDisplayAdapter::beginFrame() {
    // Allocate only after the board/display is initialized. Keep a direct-draw
    // fallback if a canvas cannot be allocated; UI remains usable under pressure.
    if (!frame_)
        frame_.reset(new (std::nothrow) Frame());
    if (!frame_)
        return;
    frame_->active = frame_->usable;
    frame_->transitionRequested = false;
    frame_->left = 240;
    frame_->top = 135;
    frame_->right = 0;
    frame_->bottom = 0;
}

void CardputerDisplayAdapter::endFrame() {
    if (!frame_ || !frame_->active)
        return;
    if (frame_->snapshotUsable) {
        const auto offset = frame_->slide.offset();
        const bool dirty = frame_->left < frame_->right && frame_->top < frame_->bottom;
        if (dirty || offset != frame_->presentedOffset) {
            const bool forward = frame_->slide.direction() == core::SlideDirection::Forward;
            M5.Display.startWrite();
            M5.Display.setClipRect(0, 0, 240, 135);
            if (offset < 240)
                frame_->previous.pushSprite(forward ? -offset : offset, 0);
            if (offset > 0)
                frame_->canvas.pushSprite(forward ? 240 - offset : offset - 240, 0);
            M5.Display.clearClipRect();
            M5.Display.endWrite();
            frame_->presentedOffset = offset;
        }
        if (!frame_->slide.active()) {
            // The second framebuffer is transient, not a permanent cost to BLE.
            frame_->previous.deleteSprite();
            frame_->snapshotUsable = false;
        }
    } else if (frame_->left < frame_->right && frame_->top < frame_->bottom) {
        // Copy only the completed dirty region: the LCD never sees a blank
        // clear followed by separately appearing text during a page change.
        M5.Display.startWrite();
        M5.Display.setClipRect(frame_->left, frame_->top, frame_->right - frame_->left,
                               frame_->bottom - frame_->top);
        frame_->canvas.pushSprite(0, 0);
        M5.Display.clearClipRect();
        M5.Display.endWrite();
    }
    frame_->active = false;
}

void CardputerDisplayAdapter::advanceTransition(std::chrono::milliseconds elapsed) {
    if (frame_ && frame_->snapshotUsable)
        frame_->slide.advance(elapsed);
}

bool CardputerDisplayAdapter::transitionActive() const {
    return frame_ && frame_->snapshotUsable && frame_->slide.active();
}

void CardputerDisplayAdapter::beginTransition(core::SlideDirection direction) {
    if (!frame_ || !frame_->active)
        return;
    if (!frame_->transitionRequested) {
        if (frame_->snapshotUsable) {
            // Freeze what was actually presented, not the as-yet-hidden target.
            core::composeSlideSnapshot(
                static_cast<std::uint16_t*>(frame_->previous.getBuffer()),
                static_cast<const std::uint16_t*>(frame_->canvas.getBuffer()), 240, 135,
                std::max(0, frame_->presentedOffset), frame_->slide.direction());
        } else {
            frame_->previous.setColorDepth(16);
            if (!frame_->previous.createSprite(240, 135))
                return;
            std::memcpy(frame_->previous.getBuffer(), frame_->canvas.getBuffer(), 240 * 135 * 2);
            frame_->snapshotUsable = true;
        }
    }
    // Multiple navigation Actions in one poll retain the original source frame.
    frame_->transitionRequested = true;
    frame_->slide.start(direction);
    frame_->presentedOffset = -1;
}

void CardputerDisplayAdapter::clear(core::RgbColor color) {
    if (frame_ && frame_->active) {
        frame_->canvas.fillScreen(toDeviceColor(color));
        frame_->damage({0, 0}, 240, 135);
    } else
        M5.Display.fillScreen(toDeviceColor(color));
}

void CardputerDisplayAdapter::fillRectangle(core::PixelPosition position, std::int32_t width,
                                            std::int32_t height, core::RgbColor color) {
    if (frame_ && frame_->active) {
        frame_->canvas.fillRect(position.x, position.y, width, height, toDeviceColor(color));
        frame_->damage(position, width, height);
    } else
        M5.Display.fillRect(position.x, position.y, width, height, toDeviceColor(color));
}

void CardputerDisplayAdapter::drawText(core::PixelPosition position, const char* text,
                                       core::TextStyle style) {
    lgfx::LovyanGFX& target = frame_ && frame_->active
                                  ? static_cast<lgfx::LovyanGFX&>(frame_->canvas)
                                  : static_cast<lgfx::LovyanGFX&>(M5.Display);
    target.setFont(&fonts::Font0);
    target.setTextColor(toDeviceColor(style.foreground), toDeviceColor(style.background));
    target.setTextSize(style.scale);
    target.drawString(text, position.x, position.y);
    if (frame_ && frame_->active)
        frame_->damage(position, target.textWidth(text), target.fontHeight());
}
} // namespace cardputer_hub::hardware
