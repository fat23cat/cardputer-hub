#pragma once

#include <cstddef>
#include <cstdint>

namespace cardputer_hub::core {

// Unpredictable bytes for identifiers. The ESP32 implementation uses the
// hardware random number generator; tests inject a deterministic source.
class IRandomSource {
  public:
    virtual ~IRandomSource() = default;
    virtual void fill(std::uint8_t* destination, std::size_t size) = 0;
};

} // namespace cardputer_hub::core
