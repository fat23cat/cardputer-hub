#pragma once

#include <cstddef>
#include <string>

#include "connectivity/wifi/wifi_service.h"

namespace cardputer_hub::connectivity {

inline constexpr std::size_t minimumAccessPointPassphraseLength = 8;
inline constexpr std::size_t maximumAccessPointPassphraseLength = 63;
inline constexpr std::size_t maximumAccessPointClients = 4;

// A WPA2-PSK network the Cardputer provides. The interface address is the
// ESP-IDF default, 192.168.4.1, with DHCP for the clients.
struct WifiAccessPointConfig {
    std::string ssid;
    std::string passphrase;
};

// SSID: 1-32 printable ASCII bytes. Passphrase: 8-63 printable ASCII bytes.
[[nodiscard]] inline bool validWifiAccessPointConfig(const WifiAccessPointConfig& config) noexcept {
    const auto printable = [](const std::string& text) {
        for (const char character : text) {
            if (character < 0x20 || character > 0x7e)
                return false;
        }
        return true;
    };
    return !config.ssid.empty() && config.ssid.size() <= maximumWifiSsidLength &&
           config.passphrase.size() >= minimumAccessPointPassphraseLength &&
           config.passphrase.size() <= maximumAccessPointPassphraseLength &&
           printable(config.ssid) && printable(config.passphrase);
}

// The access-point half of the Wi-Fi driver. It runs beside the station
// (IWifiAdapter) without changing the station's connection. Calls are
// synchronous and made from the main loop only.
class IWifiAccessPointAdapter {
  public:
    virtual ~IWifiAccessPointAdapter() = default;

    virtual WifiAdapterResult startAccessPoint(const WifiAccessPointConfig& config) = 0;
    virtual WifiAdapterResult stopAccessPoint() = 0;
    [[nodiscard]] virtual std::size_t accessPointClientCount() const = 0;
};

} // namespace cardputer_hub::connectivity
