#include "hardware/cardputer/cardputer_platform.h"

#include <M5Unified.hpp>

namespace cardputer_hub::hardware {

void CardputerPlatform::begin() {
    auto config = M5.config();
    M5.begin(config);
}

void CardputerPlatform::update() { M5.update(); }

} // namespace cardputer_hub::hardware
