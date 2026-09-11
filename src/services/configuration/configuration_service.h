#pragma once

#include <optional>
#include <string>
#include <vector>

#include "connectivity/bluetooth/bluetooth_service.h"
#include "core/storage/storage.h"

namespace cardputer_hub::services {

struct HostProfile {
    std::uint32_t id = 0;
    std::string name;
    connectivity::BluetoothBondReference bond;
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
    explicit ConfigurationService(core::Storage& storage) : storage_(storage) {}
    ConfigurationResult load();
    ConfigurationResult save(const HostConfiguration& value);
    const HostConfiguration& value() const noexcept { return value_; }
    static bool valid(const HostConfiguration& value);

  private:
    core::Storage& storage_;
    HostConfiguration value_;
};

} // namespace cardputer_hub::services
