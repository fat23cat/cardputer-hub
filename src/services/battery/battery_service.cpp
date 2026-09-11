#include "services/battery/battery_service.h"
#include <algorithm>
namespace cardputer_hub::services {
void BatteryService::update(std::chrono::milliseconds elapsed) {
    remaining_ -= std::min(remaining_, std::max(elapsed, std::chrono::milliseconds(0)));
    if (remaining_.count() != 0)
        return;
    const auto reading = adapter_.readPercent();
    percent_ = reading >= 0 && reading <= 100 ? std::optional<std::uint8_t>(reading) : std::nullopt;
    // A delayed loop samples once; it never catches up with a burst of ADC reads.
    remaining_ = std::chrono::milliseconds(5000);
}
} // namespace cardputer_hub::services
