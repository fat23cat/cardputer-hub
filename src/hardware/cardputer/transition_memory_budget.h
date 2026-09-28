#pragma once

#include <cstddef>

namespace cardputer_hub::hardware {

inline constexpr std::size_t transitionSnapshotBytes = 240 * 135 * 2 + 2;
inline constexpr std::size_t transitionInternalReserveBytes = 64 * 1024;

constexpr bool canAllocateTransitionSnapshot(std::size_t freeInternalBytes,
                                             std::size_t largestDmaBlockBytes) noexcept {
    return largestDmaBlockBytes >= transitionSnapshotBytes &&
           freeInternalBytes >= transitionSnapshotBytes + transitionInternalReserveBytes;
}

} // namespace cardputer_hub::hardware
