#include "hardware/cardputer/cardputer_platform.h"

#include <Arduino.h>
#include <M5Cardputer.h>

namespace cardputer_hub::hardware {

void CardputerPlatform::begin() {
    const auto config = M5.config();
    M5Cardputer.begin(config);
    Serial.begin(115200);
}

void CardputerPlatform::printBootMessage(const char* name, const char* version, const char* commit,
                                         const char* buildType) {
    Serial.printf("%s %s (%s, %s)\n", name, version, commit, buildType);
}

void CardputerPlatform::update() { M5Cardputer.update(); }

} // namespace cardputer_hub::hardware
