#pragma once

#include "core/power/backlight_adapter.h"

namespace cardputer_hub::hardware {

// The only component that knows the M5Unified backlight API. Timing policy
// belongs to DisplayPowerController.
class CardputerBacklightAdapter final : public core::IBacklightAdapter {
  public:
    std::uint8_t level() const override;
    void setLevel(std::uint8_t level) override;
};

} // namespace cardputer_hub::hardware
