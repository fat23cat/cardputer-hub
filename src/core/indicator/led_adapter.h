#pragma once

#include "core/display/display_adapter.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cardputer_hub::core {

inline constexpr std::size_t ledMatrixPixelCount = 64;
inline constexpr std::uint8_t ledMatrixWidth = 8;

struct LedHardwareFrame {
    std::array<RgbColor, ledMatrixPixelCount> pixels{};
};

class ILEDAdapter {
  public:
    virtual ~ILEDAdapter() = default;
    virtual void writeFrame(const LedHardwareFrame& frame) = 0;
};

} // namespace cardputer_hub::core
