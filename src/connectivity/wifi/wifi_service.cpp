#include "connectivity/wifi/wifi_service.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace cardputer_hub::connectivity {
namespace {

constexpr auto attemptTimeout = std::chrono::seconds(15);
constexpr std::array retryDelays{
    std::chrono::seconds(1), std::chrono::seconds(2),  std::chrono::seconds(4),
    std::chrono::seconds(8), std::chrono::seconds(16), std::chrono::seconds(30),
};

bool isValidSsid(std::string_view ssid) {
    return !ssid.empty() && ssid.size() <= 32 && ssid.find('\0') == std::string_view::npos;
}

bool isHexDigit(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

bool isValidPassphrase(std::string_view passphrase) {
    if (passphrase.find('\0') != std::string_view::npos) {
        return false;
    }
    if (passphrase.empty() || (passphrase.size() >= 8 && passphrase.size() <= 63)) {
        return true;
    }
    return passphrase.size() == 64 && std::all_of(passphrase.begin(), passphrase.end(), isHexDigit);
}

bool isValidConfig(const WifiNetworkConfig& config) {
    return isValidSsid(config.ssid) && isValidPassphrase(config.passphrase);
}

} // namespace

WiFiService::WiFiService(IWifiAdapter& adapter) noexcept : adapter_(adapter) {}

WiFiService::WiFiService(IWifiAdapter& adapter, core::Logger& logger) noexcept
    : adapter_(adapter), logger_(&logger) {}

WifiConnectResult WiFiService::connect(const WifiNetworkConfig& config) {
    if (!isValidConfig(config)) {
        log(core::LogLevel::Warning, "connection request rejected");
        return WifiConnectResult::InvalidConfig;
    }

    if (target_.has_value()) {
        if (adapter_.disconnect() != WifiAdapterResult::Success) {
            state_ = WifiState::Error;
            stateElapsed_ = std::chrono::milliseconds::zero();
            log(core::LogLevel::Error, "active connection could not stop");
            return WifiConnectResult::AdapterError;
        }
    }
    target_ = config;
    stateElapsed_ = std::chrono::milliseconds::zero();
    retryIndex_ = 0;

    if (!initialized_) {
        if (adapter_.initializeStation() != WifiAdapterResult::Success) {
            target_.reset();
            state_ = WifiState::Error;
            log(core::LogLevel::Error, "station initialization failed");
            return WifiConnectResult::AdapterError;
        }
        initialized_ = true;
    }

    if (adapter_.connect(*target_) != WifiAdapterResult::Success) {
        target_.reset();
        state_ = WifiState::Error;
        log(core::LogLevel::Error, "connection attempt could not start");
        return WifiConnectResult::AdapterError;
    }

    state_ = WifiState::Connecting;
    log(core::LogLevel::Info, "connection attempt started");
    return WifiConnectResult::Started;
}

WifiDisconnectResult WiFiService::disconnect() {
    auto result = WifiDisconnectResult::Disconnected;
    if (target_.has_value()) {
        if (adapter_.disconnect() != WifiAdapterResult::Success) {
            result = WifiDisconnectResult::AdapterError;
        }
    }
    target_.reset();
    state_ = result == WifiDisconnectResult::Disconnected ? WifiState::Idle : WifiState::Error;
    stateElapsed_ = std::chrono::milliseconds::zero();
    retryIndex_ = 0;
    log(result == WifiDisconnectResult::Disconnected ? core::LogLevel::Info : core::LogLevel::Error,
        result == WifiDisconnectResult::Disconnected
            ? "connection intent cleared"
            : "connection intent cleared with adapter error");
    return result;
}

void WiFiService::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero()) {
        elapsed = std::chrono::milliseconds::zero();
    }

    if (state_ == WifiState::RetryWaiting) {
        const auto retryDelay = retryDelays[retryIndex_];
        if (elapsed < retryDelay - stateElapsed_) {
            stateElapsed_ += elapsed;
            return;
        }

        stateElapsed_ = std::chrono::milliseconds::zero();
        if (!target_.has_value() || adapter_.connect(*target_) != WifiAdapterResult::Success) {
            target_.reset();
            state_ = WifiState::Error;
            log(core::LogLevel::Error, "retry attempt could not start");
            return;
        }
        if (retryIndex_ + 1 < retryDelays.size()) {
            ++retryIndex_;
        }
        state_ = WifiState::Connecting;
        log(core::LogLevel::Info, "retry attempt started");
        return;
    }

    if (state_ != WifiState::Connecting && state_ != WifiState::Connected) {
        return;
    }

    const auto adapterState = adapter_.state();
    if (adapterState == WifiAdapterState::Error) {
        state_ = WifiState::Error;
        stateElapsed_ = std::chrono::milliseconds::zero();
        log(core::LogLevel::Error, "adapter reported fatal error");
        return;
    }

    if (adapterState == WifiAdapterState::Connected) {
        const bool connectionCompleted = state_ == WifiState::Connecting;
        state_ = WifiState::Connected;
        stateElapsed_ = std::chrono::milliseconds::zero();
        retryIndex_ = 0;
        if (connectionCompleted) {
            log(core::LogLevel::Info, "connection established");
        }
        return;
    }

    if (adapterState == WifiAdapterState::Disconnected || state_ == WifiState::Connected) {
        if (adapter_.disconnect() != WifiAdapterResult::Success) {
            state_ = WifiState::Error;
            stateElapsed_ = std::chrono::milliseconds::zero();
            log(core::LogLevel::Error, "failed connection could not stop");
            return;
        }
        state_ = WifiState::RetryWaiting;
        stateElapsed_ = std::chrono::milliseconds::zero();
        log(core::LogLevel::Warning, "connection retry scheduled");
        return;
    }

    if (elapsed >= attemptTimeout - stateElapsed_) {
        if (adapter_.disconnect() != WifiAdapterResult::Success) {
            state_ = WifiState::Error;
            stateElapsed_ = std::chrono::milliseconds::zero();
            log(core::LogLevel::Error, "timed-out connection could not stop");
            return;
        }
        state_ = WifiState::RetryWaiting;
        stateElapsed_ = std::chrono::milliseconds::zero();
        log(core::LogLevel::Warning, "connection attempt timed out");
        return;
    }

    stateElapsed_ += elapsed;
}

WifiState WiFiService::state() const noexcept { return state_; }

std::optional<std::int32_t> WiFiService::signalStrengthDbm() const {
    if (state_ != WifiState::Connected) {
        return std::nullopt;
    }
    return adapter_.signalStrengthDbm();
}

void WiFiService::log(core::LogLevel level, const char* message) const {
    if (logger_ != nullptr) {
        logger_->log({level, "wifi", message});
    }
}

} // namespace cardputer_hub::connectivity
