#include "core/lifecycle/build_info.h"
#include "hardware/cardputer/cardputer_platform.h"

namespace {

cardputer_hub::hardware::CardputerPlatform platform;

} // namespace

void setup() {
    platform.begin();
    platform.printBootMessage(
        cardputer_hub::core::firmwareName(), cardputer_hub::core::firmwareVersion(),
        cardputer_hub::core::firmwareCommit(), cardputer_hub::core::firmwareBuildType());
}

void loop() { platform.update(); }
