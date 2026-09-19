#pragma once

#include <cstdint>

namespace cardputer_hub::core {

// Level 0 is off; 255 is the maximum the concrete adapter can produce.
class IBacklightAdapter {
  public:
    virtual ~IBacklightAdapter() = default;

    virtual std::uint8_t level() const = 0;
    virtual void setLevel(std::uint8_t level) = 0;
};

} // namespace cardputer_hub::core
