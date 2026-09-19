#include "hardware/cardputer/cardputer_backlight_adapter.h"

#include <M5Unified.hpp>

namespace cardputer_hub::hardware {

// M5GFX already uses the 0-255 range, so the normalized level maps straight through.
std::uint8_t CardputerBacklightAdapter::level() const { return M5.Display.getBrightness(); }

void CardputerBacklightAdapter::setLevel(std::uint8_t level) { M5.Display.setBrightness(level); }

} // namespace cardputer_hub::hardware
