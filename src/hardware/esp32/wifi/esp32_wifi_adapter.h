#pragma once

#include "connectivity/wifi/wifi_service.h"

namespace cardputer_hub::hardware {

class Esp32WifiAdapter final : public connectivity::IWifiAdapter {
  public:
    connectivity::WifiAdapterResult initializeStation() override;
    connectivity::WifiAdapterResult connect(const connectivity::WifiNetworkConfig& config) override;
    connectivity::WifiAdapterResult disconnect() override;
    connectivity::WifiAdapterState state() const override;
    std::int32_t signalStrengthDbm() const override;
};

} // namespace cardputer_hub::hardware
