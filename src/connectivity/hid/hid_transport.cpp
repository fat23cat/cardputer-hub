#include "connectivity/hid/hid_transport.h"

#include <algorithm>

namespace cardputer_hub::connectivity {

bool isValidHidReport(const HidReport& report) noexcept {
    const auto* keyboard = std::get_if<HidKeyboardReport>(&report);
    if (keyboard == nullptr) {
        return true;
    }

    for (std::size_t index = 0; index < keyboard->usages.size(); ++index) {
        const auto usage = keyboard->usages[index];
        if (usage >= 0x01 && usage <= 0x03) {
            return false;
        }
        if (usage != 0 &&
            std::find(keyboard->usages.begin() + static_cast<std::ptrdiff_t>(index + 1),
                      keyboard->usages.end(), usage) != keyboard->usages.end()) {
            return false;
        }
    }
    return true;
}

} // namespace cardputer_hub::connectivity
