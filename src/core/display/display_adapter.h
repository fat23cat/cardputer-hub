#pragma once

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
    virtual void clear(RgbColor color) = 0;
    virtual void drawText(PixelPosition position, const char* text, TextStyle style) = 0;
};

} // namespace cardputer_hub::core
