#pragma once

#include "core/display/display_adapter.h"

#include <cstdint>

namespace cardputer_hub::core {

// Packed rows, most significant bit first. Preserve the mask while batching
// adjacent foreground pixels into one display operation.
inline void drawMonochromeBitmap(IDisplayAdapter& display, PixelPosition position,
                                 const std::uint8_t* bits, std::int32_t width, std::int32_t height,
                                 std::int32_t stride, RgbColor color) {
    for (std::int32_t y = 0; y < height; ++y) {
        const auto filled = [&](std::int32_t x) {
            return bits[y * stride + x / 8] & (0x80U >> (x % 8));
        };
        std::int32_t x = 0;
        while (x < width) {
            if (!filled(x)) {
                ++x;
                continue;
            }
            const auto start = x++;
            while (x < width && filled(x))
                ++x;
            display.fillRectangle({position.x + start, position.y + y}, x - start, 1, color);
        }
    }
}

} // namespace cardputer_hub::core
