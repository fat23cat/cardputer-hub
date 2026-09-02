#include "hardware/esp32/wifi/esp32_wifi_adapter.h"

#include <algorithm>

#include <WiFi.h>
#include <esp_wifi.h>

namespace cardputer_hub::hardware {
namespace {

bool isStationAssociated() {
    wifi_ap_record_t accessPoint{};
    return esp_wifi_sta_get_ap_info(&accessPoint) == ESP_OK;
}

wifi_config_t stationConfig(const connectivity::WifiNetworkConfig& config) {
    wifi_config_t result{};
    std::copy(config.ssid.begin(), config.ssid.end(), result.sta.ssid);
    std::copy(config.passphrase.begin(), config.passphrase.end(), result.sta.password);
    result.sta.scan_method = WIFI_FAST_SCAN;
    result.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    result.sta.threshold.rssi = -127;
    result.sta.threshold.authmode = config.passphrase.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    result.sta.pmf_cfg.capable = true;
    result.sta.pmf_cfg.required = false;
    return result;
}

} // namespace

connectivity::WifiAdapterResult Esp32WifiAdapter::initializeStation() {
    WiFi.persistent(false);
    (void)WiFi.setAutoReconnect(false);
    if (!WiFi.mode(WIFI_STA)) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (!WiFi.config(IPAddress(), IPAddress(), IPAddress())) {
        return connectivity::WifiAdapterResult::Error;
    }
    return connectivity::WifiAdapterResult::Success;
}

connectivity::WifiAdapterResult
Esp32WifiAdapter::connect(const connectivity::WifiNetworkConfig& config) {
    auto driverConfig = stationConfig(config);
    if (esp_wifi_set_config(WIFI_IF_STA, &driverConfig) != ESP_OK) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (esp_wifi_connect() != ESP_OK) {
        return connectivity::WifiAdapterResult::Error;
    }
    return connectivity::WifiAdapterResult::Success;
}

void Esp32WifiAdapter::disconnect() { (void)WiFi.disconnect(false, false); }

connectivity::WifiAdapterState Esp32WifiAdapter::state() const {
    switch (WiFi.status()) {
    case WL_CONNECTED:
        return isStationAssociated() ? connectivity::WifiAdapterState::Connected
                                     : connectivity::WifiAdapterState::Disconnected;
    case WL_IDLE_STATUS:
    case WL_SCAN_COMPLETED:
    case WL_DISCONNECTED:
        return connectivity::WifiAdapterState::Connecting;
    case WL_NO_SHIELD:
        return connectivity::WifiAdapterState::Error;
    case WL_NO_SSID_AVAIL:
    case WL_CONNECT_FAILED:
    case WL_CONNECTION_LOST:
    default:
        return connectivity::WifiAdapterState::Disconnected;
    }
}

std::int32_t Esp32WifiAdapter::signalStrengthDbm() const { return WiFi.RSSI(); }

} // namespace cardputer_hub::hardware
