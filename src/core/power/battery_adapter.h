#pragma once
namespace cardputer_hub::core {
class IBatteryAdapter {
  public:
    virtual ~IBatteryAdapter() = default;
    // Estimated percentage; negative means unavailable.
    virtual int readPercent() = 0;
};
} // namespace cardputer_hub::core
