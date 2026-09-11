#pragma once
#include "core/power/battery_adapter.h"
#include <chrono>
#include <cstdint>
#include <optional>
namespace cardputer_hub::services {
class BatteryService {
  public:
    explicit BatteryService(core::IBatteryAdapter& adapter) : adapter_(adapter) {}
    void update(std::chrono::milliseconds elapsed);
    std::optional<std::uint8_t> percent() const { return percent_; }

  private:
    core::IBatteryAdapter& adapter_;
    std::optional<std::uint8_t> percent_;
    std::chrono::milliseconds remaining_{0};
};
} // namespace cardputer_hub::services
