#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "core/logging/logger.h"

namespace cardputer_hub::connectivity {

struct WifiNetworkConfig {
    std::string ssid;
    std::string passphrase;
};

enum class WifiState : std::uint8_t {
    Idle,
    Connecting,
    Connected,
    RetryWaiting,
    Error,
};

enum class WifiConnectResult : std::uint8_t {
    Started,
    InvalidConfig,
    AdapterError,
};

enum class WifiAdapterResult : std::uint8_t {
    Success,
    Error,
};

enum class WifiAdapterState : std::uint8_t {
    Disconnected,
    Connecting,
    Connected,
    Error,
};

class IWifiAdapter {
  public:
    virtual ~IWifiAdapter() = default;

    virtual WifiAdapterResult initializeStation() = 0;
    virtual WifiAdapterResult connect(const WifiNetworkConfig& config) = 0;
    virtual void disconnect() = 0;
    virtual WifiAdapterState state() const = 0;
    virtual std::int32_t signalStrengthDbm() const = 0;
};

class WiFiService {
  public:
    explicit WiFiService(IWifiAdapter& adapter) noexcept;
    WiFiService(IWifiAdapter& adapter, core::Logger& logger) noexcept;

    WifiConnectResult connect(const WifiNetworkConfig& config);
    void disconnect();
    void update(std::chrono::milliseconds elapsed);
    WifiState state() const noexcept;
    std::optional<std::int32_t> signalStrengthDbm() const;

  private:
    void log(core::LogLevel level, const char* message) const;

    IWifiAdapter& adapter_;
    core::Logger* logger_ = nullptr;
    std::optional<WifiNetworkConfig> target_;
    WifiState state_ = WifiState::Idle;
    std::chrono::milliseconds stateElapsed_{0};
    std::size_t retryIndex_ = 0;
    bool initialized_ = false;
};

} // namespace cardputer_hub::connectivity
