#include "hardware/cardputer/cardputer_battery_adapter.h"
#include <M5Unified.hpp>
namespace cardputer_hub::hardware {
int CardputerBatteryAdapter::readPercent() { return M5.Power.getBatteryLevel(); }
} // namespace cardputer_hub::hardware
