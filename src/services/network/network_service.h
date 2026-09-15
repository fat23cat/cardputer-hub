#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "core/logging/logger.h"
#include "services/configuration/configuration_service.h"

namespace cardputer_hub::services {

enum class NetworkResult : std::uint8_t {
    Success,
    InvalidInput,
    NotConfigured,
    StorageError,
    ConnectivityError,
};

enum class WifiConnectionStatus : std::uint8_t {
    Off,
    Connecting,
    Connected,
    Error,
};

struct WifiStatusSnapshot {
    bool configured = false;
    bool enabled = false;
    WifiConnectionStatus connection = WifiConnectionStatus::Off;
    std::string ssid;
    std::optional<std::int32_t> signalStrengthDbm;
    NetworkResult lastResult = NetworkResult::Success;
};

class NetworkService {
  public:
    NetworkService(connectivity::WiFiService& wifi, ConfigurationService& configuration,
                   core::Logger* logger = nullptr) noexcept
        : wifi_(wifi), configuration_(configuration), logger_(logger) {}

    NetworkResult start();
    void update(std::chrono::milliseconds elapsed);
    NetworkResult configure(std::string ssid, std::string passphrase);
    NetworkResult setEnabled(bool enabled);
    NetworkResult forget();
    WifiStatusSnapshot status() const;

  private:
    NetworkResult ensureConfiguration();
    NetworkResult connectConfigured();
    NetworkResult save(const SystemConfiguration& candidate);
    NetworkResult finish(NetworkResult result, core::LogLevel level = core::LogLevel::Info,
                         const char* message = nullptr);

    connectivity::WiFiService& wifi_;
    ConfigurationService& configuration_;
    core::Logger* logger_;
    NetworkResult lastResult_ = NetworkResult::Success;
};

} // namespace cardputer_hub::services
