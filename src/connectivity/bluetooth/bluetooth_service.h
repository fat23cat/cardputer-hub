#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/logging/logger.h"

namespace cardputer_hub::connectivity {

struct BluetoothDeviceConfig {
    std::string deviceName;
};

struct BluetoothPeerHandle {
    std::uint32_t value = 0;
};

constexpr bool operator==(BluetoothPeerHandle left, BluetoothPeerHandle right) noexcept {
    return left.value == right.value;
}

constexpr bool operator!=(BluetoothPeerHandle left, BluetoothPeerHandle right) noexcept {
    return !(left == right);
}

enum class BluetoothState : std::uint8_t {
    Disabled,
    Idle,
    Advertising,
    Connected,
    RetryWaiting,
    Error,
};

enum class BluetoothFailureClass : std::uint8_t {
    Retryable,
    Fatal,
};

enum class BluetoothEventType : std::uint8_t {
    AdvertisingStarted,
    AdvertisingFailed,
    PeerConnected,
    PeerDisconnected,
    AdapterFailed,
};

struct BluetoothEvent {
    BluetoothEventType type = BluetoothEventType::AdapterFailed;
    BluetoothPeerHandle peer{};
    BluetoothFailureClass failure = BluetoothFailureClass::Fatal;
    std::uint32_t lifecycle = 0;
};

enum class BluetoothAdapterResult : std::uint8_t {
    Success,
    AdapterError,
};

enum class BluetoothAdvertisingResult : std::uint8_t {
    Started,
    RetryableFailure,
    AdapterError,
};

enum class BluetoothPollStatus : std::uint8_t {
    NoEvent,
    Event,
    AdapterError,
};

struct BluetoothPollResult {
    BluetoothPollStatus status = BluetoothPollStatus::NoEvent;
    BluetoothEvent event{};

    static constexpr BluetoothPollResult noEvent() noexcept { return {}; }
    static constexpr BluetoothPollResult withEvent(BluetoothEvent value) noexcept {
        return {BluetoothPollStatus::Event, value};
    }
    static constexpr BluetoothPollResult adapterError() noexcept {
        return {BluetoothPollStatus::AdapterError, {}};
    }
};

enum class BluetoothBondQueryResult : std::uint8_t {
    Bonded,
    Unbonded,
    AdapterError,
};

enum class BluetoothEnableResult : std::uint8_t {
    Enabled,
    AlreadyEnabled,
    AdapterError,
};

enum class BluetoothDisableResult : std::uint8_t {
    Disabled,
    AlreadyDisabled,
    AdapterError,
};

class IBluetoothAdapter {
  public:
    virtual ~IBluetoothAdapter() = default;

    virtual BluetoothAdapterResult initialize(const BluetoothDeviceConfig& config,
                                              std::uint32_t lifecycle) = 0;
    virtual BluetoothAdapterResult shutdown() = 0;
    virtual BluetoothAdvertisingResult startAdvertising(std::uint32_t lifecycle) = 0;
    virtual BluetoothAdapterResult requestAdvertisingStop() = 0;
    virtual BluetoothAdapterResult disconnectPeer(BluetoothPeerHandle peer) = 0;
    virtual BluetoothPollResult pollEvent() = 0;
    virtual BluetoothBondQueryResult bondState(BluetoothPeerHandle peer) = 0;
};

class BluetoothService {
  public:
    explicit BluetoothService(IBluetoothAdapter& adapter) noexcept;
    BluetoothService(IBluetoothAdapter& adapter, core::Logger& logger) noexcept;

    BluetoothEnableResult enable(const BluetoothDeviceConfig& config);
    BluetoothDisableResult disable();
    void update(std::chrono::milliseconds elapsed);
    BluetoothState state() const noexcept;
    std::optional<BluetoothPeerHandle> currentConnection() const noexcept;

  private:
    enum class RetryKind : std::uint8_t {
        None,
        Reconnect,
        Advertising,
    };

    BluetoothAdvertisingResult launchAdvertising();
    void handleEvent(const BluetoothEvent& event);
    void scheduleAdvertisingRetry();
    void scheduleReconnect();
    bool rejectionPending(BluetoothPeerHandle peer) const;
    void enterError(const char* message);
    void log(core::LogLevel level, const char* message) const;

    IBluetoothAdapter& adapter_;
    core::Logger* logger_ = nullptr;
    std::optional<BluetoothDeviceConfig> config_;
    std::optional<BluetoothPeerHandle> currentConnection_;
    std::vector<BluetoothPeerHandle> pendingRejectedPeers_;
    BluetoothState state_ = BluetoothState::Disabled;
    std::chrono::milliseconds retryElapsed_{0};
    std::size_t retryIndex_ = 0;
    RetryKind retryKind_ = RetryKind::None;
    bool cleanupNeeded_ = false;
    bool enabled_ = false;
    bool advertisingPendingOrActive_ = false;
    std::uint32_t lifecycle_ = 0;
    std::uint32_t nextLifecycle_ = 1;
};

} // namespace cardputer_hub::connectivity
