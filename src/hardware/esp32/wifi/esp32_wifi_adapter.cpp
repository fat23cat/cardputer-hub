#include "hardware/esp32/wifi/esp32_wifi_adapter.h"

#include <WiFi.h>

namespace cardputer_hub::hardware {

connectivity::WifiAdapterResult Esp32WifiAdapter::initializeStation() {
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA)) {
        return connectivity::WifiAdapterResult::Error;
    }
    return connectivity::WifiAdapterResult::Success;
}

connectivity::WifiAdapterResult
Esp32WifiAdapter::connect(const connectivity::WifiNetworkConfig& config) {
    const auto result = WiFi.begin(config.ssid.c_str(), config.passphrase.c_str());
    if (result == WL_NO_SHIELD) {
        return connectivity::WifiAdapterResult::Error;
    }
    return connectivity::WifiAdapterResult::Success;
}

void Esp32WifiAdapter::disconnect() { (void)WiFi.disconnect(false, false); }

connectivity::WifiAdapterState Esp32WifiAdapter::state() const {
    switch (WiFi.status()) {
    case WL_CONNECTED:
        return connectivity::WifiAdapterState::Connected;
    case WL_IDLE_STATUS:
    case WL_SCAN_COMPLETED:
        return connectivity::WifiAdapterState::Connecting;
    case WL_NO_SHIELD:
        return connectivity::WifiAdapterState::Error;
    case WL_NO_SSID_AVAIL:
    case WL_CONNECT_FAILED:
    case WL_CONNECTION_LOST:
    case WL_DISCONNECTED:
    default:
        return connectivity::WifiAdapterState::Disconnected;
    }
}

std::int32_t Esp32WifiAdapter::signalStrengthDbm() const { return WiFi.RSSI(); }

} // namespace cardputer_hub::hardware
