#include "hardware/esp32/wifi/esp32_wifi_adapter.h"

#include <algorithm>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

namespace cardputer_hub::hardware {
namespace {

#if defined(CORE_DEBUG_LEVEL)
constexpr int configuredFrameworkDebugLevel = CORE_DEBUG_LEVEL;
#else
constexpr int configuredFrameworkDebugLevel = 4;
#endif
static_assert(configuredFrameworkDebugLevel < 4,
              "ESP32 framework debug logging can expose Wi-Fi network identity");

constexpr const char* stationInterfaceKey = "WIFI_STA_DEF";

bool initializeNetworkStack() {
    const auto netifResult = esp_netif_init();
    if (netifResult != ESP_OK && netifResult != ESP_ERR_INVALID_STATE) {
        return false;
    }

    const auto eventLoopResult = esp_event_loop_create_default();
    return eventLoopResult == ESP_OK || eventLoopResult == ESP_ERR_INVALID_STATE;
}

bool wifiDriverIsUninitialized() {
    wifi_mode_t mode{};
    return esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT;
}

bool initializeWifiDriver() {
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&config) == ESP_OK;
}

esp_netif_t* stationInterface() { return esp_netif_get_handle_from_ifkey(stationInterfaceKey); }

bool copyStationConfig(const connectivity::WifiNetworkConfig& config, wifi_config_t& result) {
    if (config.ssid.empty() || config.ssid.size() > sizeof(result.sta.ssid) ||
        config.passphrase.size() > sizeof(result.sta.password) ||
        config.ssid.find('\0') != std::string::npos ||
        config.passphrase.find('\0') != std::string::npos) {
        return false;
    }

    std::copy(config.ssid.begin(), config.ssid.end(), result.sta.ssid);
    std::copy(config.passphrase.begin(), config.passphrase.end(), result.sta.password);
    result.sta.scan_method = WIFI_FAST_SCAN;
    result.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    result.sta.threshold.rssi = -127;
    result.sta.threshold.authmode = config.passphrase.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    result.sta.pmf_cfg.capable = true;
    result.sta.pmf_cfg.required = false;
    return true;
}

} // namespace

connectivity::WifiAdapterResult Esp32WifiAdapter::initializeStation() {
    if (!initializeNetworkStack()) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (!wifiDriverIsUninitialized() || stationInterface() != nullptr) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (!initializeWifiDriver()) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (esp_wifi_start() != ESP_OK) {
        return connectivity::WifiAdapterResult::Error;
    }
    return connectivity::WifiAdapterResult::Success;
}

connectivity::WifiAdapterResult
Esp32WifiAdapter::connect(const connectivity::WifiNetworkConfig& config) {
    wifi_config_t driverConfig{};
    if (!copyStationConfig(config, driverConfig)) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &driverConfig) != ESP_OK) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (esp_wifi_connect() != ESP_OK) {
        return connectivity::WifiAdapterResult::Error;
    }
    return connectivity::WifiAdapterResult::Success;
}

connectivity::WifiAdapterResult Esp32WifiAdapter::disconnect() {
    return esp_wifi_disconnect() == ESP_OK ? connectivity::WifiAdapterResult::Success
                                           : connectivity::WifiAdapterResult::Error;
}

connectivity::WifiAdapterState Esp32WifiAdapter::state() const {
    wifi_mode_t mode{};
    if (stationInterface() == nullptr || esp_wifi_get_mode(&mode) != ESP_OK ||
        mode != WIFI_MODE_STA) {
        return connectivity::WifiAdapterState::Error;
    }

    wifi_ap_record_t accessPoint{};
    if (esp_wifi_sta_get_ap_info(&accessPoint) != ESP_OK) {
        return connectivity::WifiAdapterState::Connecting;
    }

    esp_netif_ip_info_t ipInfo{};
    if (esp_netif_get_ip_info(stationInterface(), &ipInfo) != ESP_OK) {
        return connectivity::WifiAdapterState::Error;
    }
    return ipInfo.ip.addr == 0 ? connectivity::WifiAdapterState::Connecting
                               : connectivity::WifiAdapterState::Connected;
}

std::int32_t Esp32WifiAdapter::signalStrengthDbm() const {
    wifi_ap_record_t accessPoint{};
    return esp_wifi_sta_get_ap_info(&accessPoint) == ESP_OK ? accessPoint.rssi : 0;
}

} // namespace cardputer_hub::hardware
