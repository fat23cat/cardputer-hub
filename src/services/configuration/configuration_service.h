#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "connectivity/bluetooth/bluetooth_service.h"
#include "connectivity/wifi/wifi_service.h"
#include "core/power/display_power_controller.h"
#include "core/storage/storage.h"

namespace cardputer_hub::services {

using HostPlatformId = std::string;
using HostCapabilityId = std::string;
using HostMappingTemplateId = std::string;

struct HostProfile {
    std::uint32_t id = 0;
    std::string name;
    connectivity::BluetoothBondReference bond;
    std::optional<HostPlatformId> platform;
    std::vector<HostCapabilityId> capabilities;
    std::optional<HostMappingTemplateId> mappingTemplate;
};

struct HostConfiguration {
    std::vector<HostProfile> hosts;
    std::optional<std::uint32_t> activeHost;
    std::uint32_t nextHostId = 1;
    bool bluetoothEnabled = false;
};

struct WifiConfiguration {
    bool enabled = false;
    std::string ssid;
    std::string passphrase;
};

struct DeviceConfiguration {
    core::ScreenTimeoutMode screenTimeout = core::ScreenTimeoutMode::Normal;
    std::uint8_t screenBrightness = 100;
    std::uint8_t ledBrightness = 3;
};

struct SystemConfiguration {
    HostConfiguration host;
    WifiConfiguration wifi;
    std::uint8_t soundVolume = 60;
    DeviceConfiguration device;
};

enum class ConfigurationResult { Success, InvalidData, StorageError };

class ConfigurationService {
  public:
    static constexpr std::size_t maximumNameLength = 24;
    static constexpr std::size_t maximumMetadataIdentifierLength = 32;
    static constexpr std::size_t maximumHostCapabilityCount = 16;
    static constexpr std::size_t maximumSerializedSize =
        16 +
        connectivity::BluetoothService::maximumBondCount *
            (4 + 1 + maximumNameLength + connectivity::BluetoothBondReference{}.bytes.size() + 1 +
             maximumMetadataIdentifierLength + 1 +
             maximumHostCapabilityCount * (1 + maximumMetadataIdentifierLength) + 1 +
             maximumMetadataIdentifierLength) +
        1 + 1 + connectivity::maximumWifiSsidLength + 1 +
        connectivity::maximumWifiPassphraseLength + 3;
    explicit ConfigurationService(core::Storage& storage) : storage_(storage) {}
    ConfigurationResult load();
    ConfigurationResult ensureLoaded();
    ConfigurationResult save(const SystemConfiguration& value);
    const SystemConfiguration& value() const noexcept { return value_; }
    bool loaded() const noexcept { return loaded_; }
    static bool valid(const SystemConfiguration& value);
    static bool validMetadataIdentifier(std::string_view value);

  private:
    core::Storage& storage_;
    SystemConfiguration value_;
    bool loaded_ = false;
};

} // namespace cardputer_hub::services
