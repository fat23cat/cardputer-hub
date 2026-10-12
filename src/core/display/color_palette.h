#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/display/display_adapter.h"

namespace cardputer_hub::core {

// The colours of an 8-bit palette frame buffer. Each colour gets the next free
// index on first use, so the interface's few colours stay exact; once all 256
// are taken, a new colour is drawn with the nearest one.
class ColorPalette {
  public:
    static constexpr std::size_t capacity = 256;

    struct Lookup {
        std::uint8_t index = 0;
        // The colour took a new entry: the frame buffer's palette must learn it.
        bool added = false;
    };

    [[nodiscard]] Lookup indexOf(RgbColor color) {
        for (std::size_t index = 0; index < size_; ++index) {
            if (same(colors_[index], color))
                return {static_cast<std::uint8_t>(index), false};
        }
        if (size_ < capacity) {
            colors_[size_] = color;
            return {static_cast<std::uint8_t>(size_++), true};
        }
        std::size_t best = 0;
        long bestDistance = -1;
        for (std::size_t index = 0; index < size_; ++index) {
            const long red = colors_[index].red - color.red;
            const long green = colors_[index].green - color.green;
            const long blue = colors_[index].blue - color.blue;
            const long distance = red * red + green * green + blue * blue;
            if (bestDistance < 0 || distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
        return {static_cast<std::uint8_t>(best), false};
    }

    [[nodiscard]] const RgbColor& color(std::uint8_t index) const noexcept {
        return colors_[index];
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

  private:
    static bool same(RgbColor left, RgbColor right) noexcept {
        return left.red == right.red && left.green == right.green && left.blue == right.blue;
    }

    std::array<RgbColor, capacity> colors_{};
    std::size_t size_ = 0;
};

} // namespace cardputer_hub::core
