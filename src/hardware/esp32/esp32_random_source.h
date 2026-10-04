#pragma once

#include "core/platform/random_source.h"

namespace cardputer_hub::hardware {

// The ESP32-S3 hardware random number generator. Its output is true random
// while the radio runs, which Bluetooth keeps on in this firmware, and is
// seeded by the bootloader's entropy before that.
class Esp32RandomSource final : public core::IRandomSource {
  public:
    void fill(std::uint8_t* destination, std::size_t size) override;
};

} // namespace cardputer_hub::hardware
