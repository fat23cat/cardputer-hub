#include "hardware/esp32/wifi/esp32_wifi_adapter.h"

#include <algorithm>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_defaults.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <nvs_flash.h>

namespace cardputer_hub::hardware {
namespace {

#if defined(CORE_DEBUG_LEVEL)
constexpr int configuredFrameworkDebugLevel = CORE_DEBUG_LEVEL;
#else
constexpr int configuredFrameworkDebugLevel = 4;
#endif
static_assert(configuredFrameworkDebugLevel < 4,
              "ESP32 framework debug logging can expose Wi-Fi network identity");

void suppressIdentityBearingWifiLogTags() {
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_NONE);
    // The DHCP server logs every address it hands out.
    esp_log_level_set("esp_netif_lwip", ESP_LOG_WARN);
}

bool initializeNetworkStack() {
    const auto netifResult = esp_netif_init();
    if (netifResult != ESP_OK && netifResult != ESP_ERR_INVALID_STATE) {
        return false;
    }

    const auto eventLoopResult = esp_event_loop_create_default();
    return eventLoopResult == ESP_OK || eventLoopResult == ESP_ERR_INVALID_STATE;
}

void destroyInterface(esp_netif_t* interface) {
    (void)esp_wifi_clear_default_wifi_driver_and_handlers(interface);
    esp_netif_destroy(interface);
}

esp_netif_t* createStationInterface() {
    esp_netif_config_t config = ESP_NETIF_DEFAULT_WIFI_STA();
    auto* interface = esp_netif_new(&config);
    if (interface == nullptr) {
        return nullptr;
    }
    if (esp_netif_attach_wifi_station(interface) != ESP_OK ||
        esp_wifi_set_default_wifi_sta_handlers() != ESP_OK) {
        destroyInterface(interface);
        return nullptr;
    }
    return interface;
}

esp_netif_t* createAccessPointInterface() {
    // The default access-point interface: 192.168.4.1/24 with a DHCP server.
    esp_netif_config_t config = ESP_NETIF_DEFAULT_WIFI_AP();
    auto* interface = esp_netif_new(&config);
    if (interface == nullptr) {
        return nullptr;
    }
    if (esp_netif_attach_wifi_ap(interface) != ESP_OK ||
        esp_wifi_set_default_wifi_ap_handlers() != ESP_OK) {
        destroyInterface(interface);
        return nullptr;
    }
    return interface;
}

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
    result.sta.threshold.authmode = config.passphrase.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    result.sta.pmf_cfg.capable = true;
    result.sta.pmf_cfg.required = false;
    return true;
}

bool copyAccessPointConfig(const connectivity::WifiAccessPointConfig& config,
                           wifi_config_t& result) {
    if (!connectivity::validWifiAccessPointConfig(config) ||
        config.passphrase.size() >= sizeof(result.ap.password)) {
        return false;
    }
    std::copy(config.ssid.begin(), config.ssid.end(), result.ap.ssid);
    result.ap.ssid_len = static_cast<std::uint8_t>(config.ssid.size());
    std::copy(config.passphrase.begin(), config.passphrase.end(), result.ap.password);
    // Ignored in APSTA: the access point then uses the station's channel.
    result.ap.channel = 1;
    result.ap.authmode = WIFI_AUTH_WPA2_PSK;
    result.ap.max_connection = static_cast<std::uint8_t>(connectivity::maximumAccessPointClients);
    result.ap.pmf_cfg.capable = true;
    result.ap.pmf_cfg.required = false;
    return true;
}

} // namespace

bool Esp32WifiAdapter::ensureDriver() {
    if (driverInitialized_) {
        return true;
    }
    suppressIdentityBearingWifiLogTags();
    if (nvs_flash_init() != ESP_OK || !initializeNetworkStack()) {
        return false;
    }
    wifi_mode_t mode{};
    if (esp_wifi_get_mode(&mode) != ESP_ERR_WIFI_NOT_INIT) {
        // Someone else owns the driver.
        return false;
    }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&config) != ESP_OK) {
        return false;
    }
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        (void)esp_wifi_deinit();
        return false;
    }
    driverInitialized_ = true;
    return true;
}

bool Esp32WifiAdapter::applyMode() {
    const wifi_mode_t mode = stationActive_ && accessPointActive_ ? WIFI_MODE_APSTA
                             : stationActive_                     ? WIFI_MODE_STA
                             : accessPointActive_                 ? WIFI_MODE_AP
                                                                  : WIFI_MODE_NULL;
    if (mode == WIFI_MODE_NULL) {
        if (started_ && esp_wifi_stop() != ESP_OK) {
            return false;
        }
        started_ = false;
        return true;
    }
    if (esp_wifi_set_mode(mode) != ESP_OK) {
        return false;
    }
    if (!started_) {
        if (esp_wifi_start() != ESP_OK) {
            return false;
        }
        started_ = true;
    }
    return true;
}

connectivity::WifiAdapterResult Esp32WifiAdapter::initializeStation() {
    if (stationActive_ || !ensureDriver()) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (station_ == nullptr) {
        station_ = createStationInterface();
        if (station_ == nullptr) {
            return connectivity::WifiAdapterResult::Error;
        }
    }
    stationActive_ = true;
    if (!applyMode()) {
        stationActive_ = false;
        (void)applyMode();
        return connectivity::WifiAdapterResult::Error;
    }
    return connectivity::WifiAdapterResult::Success;
}

connectivity::WifiAdapterResult
Esp32WifiAdapter::connect(const connectivity::WifiNetworkConfig& config) {
    wifi_config_t driverConfig{};
    if (!stationActive_ || !copyStationConfig(config, driverConfig)) {
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
    if (!stationActive_ || station_ == nullptr || esp_wifi_get_mode(&mode) != ESP_OK ||
        (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA)) {
        return connectivity::WifiAdapterState::Error;
    }

    wifi_ap_record_t accessPoint{};
    const auto accessPointResult = esp_wifi_sta_get_ap_info(&accessPoint);
    if (accessPointResult == ESP_ERR_WIFI_NOT_CONNECT) {
        return connectivity::WifiAdapterState::Connecting;
    }
    if (accessPointResult != ESP_OK) {
        return connectivity::WifiAdapterState::Error;
    }

    esp_netif_ip_info_t ipInfo{};
    if (esp_netif_get_ip_info(station_, &ipInfo) != ESP_OK) {
        return connectivity::WifiAdapterState::Error;
    }
    return ipInfo.ip.addr == 0 ? connectivity::WifiAdapterState::Connecting
                               : connectivity::WifiAdapterState::Connected;
}

std::optional<std::int32_t> Esp32WifiAdapter::signalStrengthDbm() const {
    wifi_ap_record_t accessPoint{};
    if (esp_wifi_sta_get_ap_info(&accessPoint) != ESP_OK) {
        return std::nullopt;
    }
    return accessPoint.rssi;
}

connectivity::WifiAdapterResult
Esp32WifiAdapter::startAccessPoint(const connectivity::WifiAccessPointConfig& config) {
    wifi_config_t driverConfig{};
    if (accessPointActive_ || !copyAccessPointConfig(config, driverConfig) || !ensureDriver()) {
        return connectivity::WifiAdapterResult::Error;
    }
    if (accessPoint_ == nullptr) {
        accessPoint_ = createAccessPointInterface();
        if (accessPoint_ == nullptr) {
            return connectivity::WifiAdapterResult::Error;
        }
    }
    accessPointActive_ = true;
    const wifi_mode_t mode = stationActive_ ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    if (esp_wifi_set_mode(mode) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &driverConfig) != ESP_OK || !applyMode()) {
        accessPointActive_ = false;
        (void)applyMode();
        return connectivity::WifiAdapterResult::Error;
    }
    return connectivity::WifiAdapterResult::Success;
}

connectivity::WifiAdapterResult Esp32WifiAdapter::stopAccessPoint() {
    if (!accessPointActive_) {
        return connectivity::WifiAdapterResult::Success;
    }
    accessPointActive_ = false;
    return applyMode() ? connectivity::WifiAdapterResult::Success
                       : connectivity::WifiAdapterResult::Error;
}

std::size_t Esp32WifiAdapter::accessPointClientCount() const {
    if (!accessPointActive_) {
        return 0;
    }
    wifi_sta_list_t stations{};
    if (esp_wifi_ap_get_sta_list(&stations) != ESP_OK || stations.num < 0) {
        return 0;
    }
    return static_cast<std::size_t>(stations.num);
}

} // namespace cardputer_hub::hardware
