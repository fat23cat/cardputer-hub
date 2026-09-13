#include "hardware/cardputer/cardputer_platform.h"

#include <M5Unified.hpp>

namespace cardputer_hub::hardware {

void CardputerPlatform::begin() {
    auto config = M5.config();
    // CardputerAudioAdapter brings up the ADV codec only after silent I2S clocks
    // are running. M5Unified's board callback powers the codec before I2S setup.
    config.internal_spk = false;
    M5.begin(config);
}

void CardputerPlatform::update() { M5.update(); }

} // namespace cardputer_hub::hardware
