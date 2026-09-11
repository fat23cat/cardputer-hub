#pragma once
#include "core/power/battery_adapter.h"
namespace cardputer_hub::hardware {
class CardputerBatteryAdapter final : public core::IBatteryAdapter {
  public:
    int readPercent() override;
};
} // namespace cardputer_hub::hardware
