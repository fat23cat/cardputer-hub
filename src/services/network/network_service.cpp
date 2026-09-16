#include "services/network/network_service.h"

#include <utility>
#include <variant>

#include "connectivity/wifi/wifi_service.h"

namespace cardputer_hub::services {
namespace {
template <typename T> const T* parameter(const core::Action& action, const char* name) {
    const auto* value = action.findParameter(name);
    return value ? std::get_if<T>(value) : nullptr;
}
} // namespace

static_assert(NetworkService::maximumSsidLength == connectivity::maximumWifiSsidLength);
static_assert(NetworkService::maximumPassphraseLength == connectivity::maximumWifiPassphraseLength);

NetworkResult NetworkService::finish(NetworkResult result, core::LogLevel level,
                                     const char* message) {
    lastResult_ = result;
    if (logger_ != nullptr && message != nullptr)
        logger_->log({level, "NetworkService", message});
    return result;
}

NetworkResult NetworkService::ensureConfiguration() {
    if (configuration_.ensureLoaded() != ConfigurationResult::Success)
        return finish(NetworkResult::StorageError, core::LogLevel::Error,
                      "configuration unavailable");
    return NetworkResult::Success;
}

NetworkResult NetworkService::save(const SystemConfiguration& candidate) {
    const auto result = configuration_.save(candidate);
    if (result == ConfigurationResult::Success)
        return NetworkResult::Success;
    if (result == ConfigurationResult::InvalidData)
        return finish(NetworkResult::InvalidInput, core::LogLevel::Warning,
                      "wifi configuration rejected");
    return finish(NetworkResult::StorageError, core::LogLevel::Error,
                  "wifi configuration could not be stored");
}

NetworkResult NetworkService::connectConfigured() {
    const auto& configured = configuration_.value().wifi;
    const auto result = wifi_.connect({configured.ssid, configured.passphrase});
    if (result == connectivity::WifiConnectResult::Started)
        return finish(NetworkResult::Success, core::LogLevel::Info, "wifi connection requested");
    return finish(NetworkResult::ConnectivityError, core::LogLevel::Error,
                  "wifi connection request failed");
}

NetworkResult NetworkService::start() {
    const auto ready = ensureConfiguration();
    if (ready != NetworkResult::Success)
        return ready;
    if (!configuration_.value().wifi.enabled)
        return finish(NetworkResult::Success);
    return connectConfigured();
}

void NetworkService::update(std::chrono::milliseconds elapsed) {
    wifi_.update(elapsed);
    if (configuration_.loaded() && configuration_.value().wifi.enabled &&
        wifi_.state() == connectivity::WifiState::Error) {
        lastResult_ = NetworkResult::ConnectivityError;
    }
}

NetworkResult NetworkService::configure(std::string ssid, std::string passphrase) {
    if (!connectivity::validWifiNetworkConfig({ssid, passphrase}))
        return finish(NetworkResult::InvalidInput, core::LogLevel::Warning,
                      "wifi configuration rejected");
    const auto ready = ensureConfiguration();
    if (ready != NetworkResult::Success)
        return ready;

    auto candidate = configuration_.value();
    candidate.wifi.ssid = std::move(ssid);
    candidate.wifi.passphrase = std::move(passphrase);
    const auto stored = save(candidate);
    if (stored != NetworkResult::Success)
        return stored;
    if (!candidate.wifi.enabled)
        return finish(NetworkResult::Success, core::LogLevel::Info, "wifi configuration updated");
    return connectConfigured();
}

NetworkResult NetworkService::setEnabled(bool enabled) {
    const auto ready = ensureConfiguration();
    if (ready != NetworkResult::Success)
        return ready;
    if (enabled && configuration_.value().wifi.ssid.empty())
        return finish(NetworkResult::NotConfigured, core::LogLevel::Warning,
                      "wifi enable rejected without a configured network");

    auto candidate = configuration_.value();
    candidate.wifi.enabled = enabled;
    const auto stored = save(candidate);
    if (stored != NetworkResult::Success)
        return stored;
    if (enabled)
        return connectConfigured();

    const auto disconnected = wifi_.disconnect();
    if (disconnected != connectivity::WifiDisconnectResult::Disconnected)
        return finish(NetworkResult::ConnectivityError, core::LogLevel::Error,
                      "wifi disconnect failed");
    return finish(NetworkResult::Success, core::LogLevel::Info, "wifi disabled");
}

NetworkResult NetworkService::forget() {
    const auto ready = ensureConfiguration();
    if (ready != NetworkResult::Success)
        return ready;

    auto candidate = configuration_.value();
    candidate.wifi = {};
    const auto stored = save(candidate);
    if (stored != NetworkResult::Success)
        return stored;

    const auto disconnected = wifi_.disconnect();
    if (disconnected != connectivity::WifiDisconnectResult::Disconnected)
        return finish(NetworkResult::ConnectivityError, core::LogLevel::Error,
                      "wifi disconnect failed after forgetting network");
    return finish(NetworkResult::Success, core::LogLevel::Info, "wifi network forgotten");
}

WifiStatusSnapshot NetworkService::status() const {
    WifiStatusSnapshot snapshot;
    const auto& configured = configuration_.value().wifi;
    snapshot.configured = !configured.ssid.empty();
    snapshot.enabled = configured.enabled;
    snapshot.ssid = configured.ssid;
    snapshot.lastResult = lastResult_;

    if (!configured.enabled)
        return snapshot;

    switch (wifi_.state()) {
    case connectivity::WifiState::Connected:
        snapshot.connection = WifiConnectionStatus::Connected;
        snapshot.signalStrengthDbm = wifi_.signalStrengthDbm();
        break;
    case connectivity::WifiState::Error:
        snapshot.connection = WifiConnectionStatus::Error;
        break;
    case connectivity::WifiState::Idle:
    case connectivity::WifiState::Connecting:
    case connectivity::WifiState::RetryWaiting:
        snapshot.connection = WifiConnectionStatus::Connecting;
        break;
    }
    return snapshot;
}

core::ActionHandlingResult NetworkService::handle(const core::Action& action) {
    if (action.id == "network.set-enabled") {
        const auto* enabled = parameter<bool>(action, "enabled");
        if (!enabled)
            return core::ActionHandlingResult::Rejected;
        (void)setEnabled(*enabled);
        return core::ActionHandlingResult::Handled;
    }
    if (action.id == "network.configure") {
        const auto* ssid = parameter<std::string>(action, "ssid");
        const auto* passphrase = parameter<std::string>(action, "passphrase");
        if (!ssid || !passphrase)
            return core::ActionHandlingResult::Rejected;
        (void)configure(*ssid, *passphrase);
        return core::ActionHandlingResult::Handled;
    }
    if (action.id == "network.forget") {
        (void)forget();
        return core::ActionHandlingResult::Handled;
    }
    return core::ActionHandlingResult::Rejected;
}

} // namespace cardputer_hub::services
