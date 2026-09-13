#include "core/display/slide_transition.h"
#include <algorithm>
#include <cstring>
namespace cardputer_hub::core {
void SlideTransition::start(SlideDirection direction) {
    direction_ = direction;
    elapsed_ = 0;
}
void SlideTransition::advance(std::chrono::milliseconds elapsed) {
    elapsed_ += static_cast<int>(std::clamp<std::int64_t>(elapsed.count(), 0, 220 - elapsed_));
}
int SlideTransition::offset() const {
    if (!active())
        return 240;
    // At most one new position per 16 ms; cubic ease-out, with no float runtime.
    const auto remaining = static_cast<std::uint64_t>(220 - (elapsed_ / 16) * 16);
    constexpr std::uint64_t cube = 220 * 220 * 220;
    return std::min(
        239, 240 - static_cast<int>((remaining * remaining * remaining * 240 + cube / 2) / cube));
}
void composeSlideSnapshot(std::uint16_t* outgoing, const std::uint16_t* incoming, int width,
                          int height, int offset, SlideDirection direction) {
    advanceSlideSnapshot(outgoing, incoming, width, height, 0, offset, direction);
}
void advanceSlideSnapshot(std::uint16_t* visible, const std::uint16_t* incoming, int width,
                          int height, int priorOffset, int nextOffset, SlideDirection direction) {
    if (!visible || !incoming || width <= 0 || height <= 0)
        return;
    priorOffset = std::clamp(priorOffset, 0, width);
    nextOffset = std::clamp(nextOffset, priorOffset, width);
    const auto advance = nextOffset - priorOffset;
    for (int y = 0; y < height; ++y) {
        auto* row = visible + y * width;
        const auto* next = incoming + y * width;
        const auto retainedBytes = static_cast<std::size_t>(width - advance) * sizeof(*row);
        const auto incomingBytes = static_cast<std::size_t>(nextOffset) * sizeof(*row);
        if (direction == SlideDirection::Forward) {
            std::memmove(row, row + advance, retainedBytes);
            std::memcpy(row + width - nextOffset, next, incomingBytes);
        } else {
            std::memmove(row + advance, row, retainedBytes);
            std::memcpy(row, next + width - nextOffset, incomingBytes);
        }
    }
}
} // namespace cardputer_hub::core
