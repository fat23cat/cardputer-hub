#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace cardputer_hub::core {

enum class SlideDirection { Forward, Backward };

struct RgbColor {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

struct PixelPosition {
    std::int32_t x;
    std::int32_t y;
};

struct PixelRectangle {
    PixelPosition origin;
    std::int32_t width;
    std::int32_t height;
};

struct TextStyle {
    RgbColor foreground;
    RgbColor background;
    float scale;
    // Optional screen-coordinate scissor, including the opaque glyph background.
    std::optional<PixelRectangle> clip = std::nullopt;
};

class IDisplayAdapter {
  public:
    virtual ~IDisplayAdapter() = default;
    // A frame groups drawing into one presentation where buffering is supported.
    // Simple adapters may draw immediately; settled views issue no drawing calls.
    virtual void beginFrame() {}
    virtual void endFrame() {}
    // Page-change hooks. Views request a transition before replacing a page;
    // the Cardputer adapter presents the destination at once (a slide needed a
    // second full-screen buffer the heap cannot spare next to Wi-Fi and BLE).
    virtual void advanceTransition(std::chrono::milliseconds) {}
    virtual void beginTransition(SlideDirection) {}
    virtual bool transitionActive() const { return false; }
    virtual void clear(RgbColor color) = 0;
    virtual void fillRectangle(PixelPosition position, std::int32_t width, std::int32_t height,
                               RgbColor color) = 0;
    virtual void drawText(PixelPosition position, const char* text, TextStyle style) = 0;
};

} // namespace cardputer_hub::core
