#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "connectivity/bluetooth/bluetooth_service.h"
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

enum class ConfigurationResult { Success, InvalidData, StorageError };

class ConfigurationService {
  public:
    static constexpr std::size_t maximumNameLength = 24;
    static constexpr std::size_t maximumMetadataIdentifierLength = 32;
    static constexpr std::size_t maximumHostCapabilityCount = 16;
    static constexpr std::size_t maximumSerializedSize =
        15 + connectivity::BluetoothService::maximumBondCount *
                 (4 + 1 + maximumNameLength + connectivity::BluetoothBondReference{}.bytes.size() +
                  1 + maximumMetadataIdentifierLength + 1 +
                  maximumHostCapabilityCount * (1 + maximumMetadataIdentifierLength) + 1 +
                  maximumMetadataIdentifierLength);
    explicit ConfigurationService(core::Storage& storage) : storage_(storage) {}
    ConfigurationResult load();
    ConfigurationResult save(const HostConfiguration& value);
    const HostConfiguration& value() const noexcept { return value_; }
    static bool valid(const HostConfiguration& value);
    static bool validMetadataIdentifier(std::string_view value);

  private:
    core::Storage& storage_;
    HostConfiguration value_;
};

} // namespace cardputer_hub::services
