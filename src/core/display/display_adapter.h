#pragma once

#include "core/display/slide_transition.h"
#include <cstdint>

namespace cardputer_hub::core {

struct RgbColor {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

struct PixelPosition {
    std::int32_t x;
    std::int32_t y;
};

struct TextStyle {
    RgbColor foreground;
    RgbColor background;
    std::uint8_t scale;
};

class IDisplayAdapter {
  public:
    virtual ~IDisplayAdapter() = default;
    // A frame groups drawing into one presentation where buffering is supported.
    // Simple adapters may draw immediately; settled views issue no drawing calls.
    virtual void beginFrame() {}
    virtual void endFrame() {}
    // Advance at frame start, then request a transition before replacing a page.
    // Adapters without snapshot support present the destination immediately.
    virtual void advanceTransition(std::chrono::milliseconds) {}
    virtual void beginTransition(SlideDirection) {}
    virtual bool transitionActive() const { return false; }
    virtual void clear(RgbColor color) = 0;
    virtual void fillRectangle(PixelPosition position, std::int32_t width, std::int32_t height,
                               RgbColor color) = 0;
    virtual void drawText(PixelPosition position, const char* text, TextStyle style) = 0;
};

} // namespace cardputer_hub::core
