#pragma once

#include "connectivity/wifi/wifi_access_point.h"
#include "connectivity/wifi/wifi_service.h"

struct esp_netif_obj;

namespace cardputer_hub::hardware {

// The ESP32 Wi-Fi driver for both halves: the station (WiFiService) and the
// WEB ACCESS access point. One driver serves both; the mode is STA, AP or
// APSTA depending on which halves are active, and starting or stopping one
// half never changes the other's configuration. In APSTA the access point
// follows the station's channel.
class Esp32WifiAdapter final : public connectivity::IWifiAdapter,
                               public connectivity::IWifiAccessPointAdapter {
  public:
    connectivity::WifiAdapterResult initializeStation() override;
    connectivity::WifiAdapterResult connect(const connectivity::WifiNetworkConfig& config) override;
    connectivity::WifiAdapterResult disconnect() override;
    connectivity::WifiAdapterState state() const override;
    std::optional<std::int32_t> signalStrengthDbm() const override;

    connectivity::WifiAdapterResult
    startAccessPoint(const connectivity::WifiAccessPointConfig& config) override;
    connectivity::WifiAdapterResult stopAccessPoint() override;
    std::size_t accessPointClientCount() const override;

  private:
    bool ensureDriver();
    // Sets the mode for the active halves and starts or stops the driver.
    bool applyMode();

    esp_netif_obj* station_ = nullptr;
    esp_netif_obj* accessPoint_ = nullptr;
    bool driverInitialized_ = false;
    bool started_ = false;
    bool stationActive_ = false;
    bool accessPointActive_ = false;
};

} // namespace cardputer_hub::hardware
