#include "connectivity/bluetooth/bluetooth_service.h"

#include <algorithm>
#include <array>

namespace cardputer_hub::connectivity {
namespace {

constexpr auto reconnectDelay = std::chrono::seconds(1);
constexpr std::array advertisingRetryDelays{
    std::chrono::seconds(1), std::chrono::seconds(2),  std::chrono::seconds(4),
    std::chrono::seconds(8), std::chrono::seconds(16), std::chrono::seconds(30),
};
constexpr std::size_t maxEventsPerUpdate = 16;

} // namespace

BluetoothService::BluetoothService(IBluetoothAdapter& adapter) noexcept : adapter_(adapter) {}

BluetoothService::BluetoothService(IBluetoothAdapter& adapter, core::Logger& logger) noexcept
    : adapter_(adapter), logger_(&logger) {}

BluetoothEnableResult BluetoothService::enable(const BluetoothDeviceConfig& config) {
    if (enabled_ && state_ != BluetoothState::Error) {
        return BluetoothEnableResult::AlreadyEnabled;
    }

    if (cleanupNeeded_) {
        if (adapter_.shutdown() != BluetoothAdapterResult::Success) {
            state_ = BluetoothState::Error;
            log(core::LogLevel::Error, "previous Bluetooth cleanup still failing");
            return BluetoothEnableResult::AdapterError;
        }
        cleanupNeeded_ = false;
    }

    config_ = config;
    lifecycle_ = nextLifecycle_++;
    if (nextLifecycle_ == 0) {
        nextLifecycle_ = 1;
    }
    retryElapsed_ = std::chrono::milliseconds::zero();
    retryIndex_ = 0;
    retryKind_ = RetryKind::None;
    pendingRejectedPeers_.clear();

    cleanupNeeded_ = true;
    if (adapter_.initialize(*config_, lifecycle_) != BluetoothAdapterResult::Success) {
        config_.reset();
        enterError("adapter initialization failed");
        return BluetoothEnableResult::AdapterError;
    }

    enabled_ = true;
    const auto advertisingResult = launchAdvertising();
    if (advertisingResult == BluetoothAdvertisingResult::AdapterError) {
        enabled_ = false;
        config_.reset();
        enterError("advertising launch failed");
        return BluetoothEnableResult::AdapterError;
    }
    if (advertisingResult == BluetoothAdvertisingResult::RetryableFailure) {
        scheduleAdvertisingRetry();
    }
    log(core::LogLevel::Info, "Bluetooth enabled");
    return BluetoothEnableResult::Enabled;
}

BluetoothDisableResult BluetoothService::disable() {
    if (!enabled_ && state_ == BluetoothState::Disabled) {
        return BluetoothDisableResult::AlreadyDisabled;
    }

    bool cleanupFailed = false;
    enabled_ = false;
    retryKind_ = RetryKind::None;
    retryElapsed_ = std::chrono::milliseconds::zero();
    retryIndex_ = 0;

    if (advertisingPendingOrActive_ &&
        adapter_.requestAdvertisingStop() != BluetoothAdapterResult::Success) {
        cleanupFailed = true;
    }
    advertisingPendingOrActive_ = false;

    if (currentConnection_.has_value() &&
        adapter_.disconnectPeer(*currentConnection_) != BluetoothAdapterResult::Success) {
        cleanupFailed = true;
    }
    currentConnection_.reset();
    pendingRejectedPeers_.clear();
    config_.reset();

    if (cleanupNeeded_) {
        if (adapter_.shutdown() == BluetoothAdapterResult::Success) {
            cleanupNeeded_ = false;
        } else {
            cleanupFailed = true;
        }
    }
    state_ = cleanupFailed ? BluetoothState::Error : BluetoothState::Disabled;
    log(cleanupFailed ? core::LogLevel::Error : core::LogLevel::Info,
        cleanupFailed ? "Bluetooth disabled with adapter error" : "Bluetooth disabled");
    return cleanupFailed ? BluetoothDisableResult::AdapterError : BluetoothDisableResult::Disabled;
}

void BluetoothService::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero()) {
        elapsed = std::chrono::milliseconds::zero();
    }

    if (enabled_ && state_ == BluetoothState::RetryWaiting) {
        const auto delay = retryKind_ == RetryKind::Reconnect ? reconnectDelay
                                                              : advertisingRetryDelays[retryIndex_];
        if (elapsed < delay - retryElapsed_) {
            retryElapsed_ += elapsed;
        } else {
            retryElapsed_ = std::chrono::milliseconds::zero();
            const auto previousRetryKind = retryKind_;
            retryKind_ = RetryKind::None;
            if (previousRetryKind == RetryKind::Advertising &&
                retryIndex_ + 1 < advertisingRetryDelays.size()) {
                ++retryIndex_;
            }
            const auto result = launchAdvertising();
            if (result == BluetoothAdvertisingResult::RetryableFailure) {
                scheduleAdvertisingRetry();
            } else if (result == BluetoothAdvertisingResult::AdapterError) {
                enterError("advertising retry failed");
            }
        }
    }

    for (std::size_t count = 0; count < maxEventsPerUpdate; ++count) {
        if (!enabled_ || state_ == BluetoothState::Error) {
            return;
        }
        const auto result = adapter_.pollEvent();
        if (result.status == BluetoothPollStatus::NoEvent) {
            return;
        }
        if (result.status == BluetoothPollStatus::AdapterError) {
            enterError("adapter event polling failed");
            return;
        }
        if (result.event.lifecycle == lifecycle_) {
            handleEvent(result.event);
        }
    }
}

BluetoothState BluetoothService::state() const noexcept { return state_; }

std::optional<BluetoothPeerHandle> BluetoothService::currentConnection() const noexcept {
    return currentConnection_;
}

BluetoothAdvertisingResult BluetoothService::launchAdvertising() {
    const auto result = adapter_.startAdvertising(lifecycle_);
    advertisingPendingOrActive_ = result == BluetoothAdvertisingResult::Started;
    if (advertisingPendingOrActive_) {
        state_ = BluetoothState::Idle;
        log(core::LogLevel::Info, "advertising requested");
    }
    return result;
}

void BluetoothService::handleEvent(const BluetoothEvent& event) {
    switch (event.type) {
    case BluetoothEventType::AdvertisingStarted:
        if (advertisingPendingOrActive_) {
            state_ = BluetoothState::Advertising;
            retryIndex_ = 0;
            retryKind_ = RetryKind::None;
            retryElapsed_ = std::chrono::milliseconds::zero();
            log(core::LogLevel::Info, "advertising started");
        }
        return;
    case BluetoothEventType::AdvertisingFailed:
        advertisingPendingOrActive_ = false;
        if (event.failure == BluetoothFailureClass::Retryable) {
            scheduleAdvertisingRetry();
        } else {
            enterError("advertising failed");
        }
        return;
    case BluetoothEventType::PeerConnected:
        advertisingPendingOrActive_ = false;
        retryKind_ = RetryKind::None;
        retryElapsed_ = std::chrono::milliseconds::zero();
        if (currentConnection_.has_value() || !pendingRejectedPeers_.empty()) {
            if ((currentConnection_.has_value() && *currentConnection_ == event.peer) ||
                rejectionPending(event.peer)) {
                return;
            }
            if (adapter_.disconnectPeer(event.peer) != BluetoothAdapterResult::Success) {
                enterError("additional peer could not be rejected");
                return;
            }
            pendingRejectedPeers_.push_back(event.peer);
            if (!currentConnection_.has_value()) {
                state_ = BluetoothState::Idle;
            }
            return;
        }
        switch (adapter_.bondState(event.peer)) {
        case BluetoothBondQueryResult::Bonded:
            currentConnection_ = event.peer;
            state_ = BluetoothState::Connected;
            retryIndex_ = 0;
            retryKind_ = RetryKind::None;
            retryElapsed_ = std::chrono::milliseconds::zero();
            log(core::LogLevel::Info, "bonded peer connected");
            return;
        case BluetoothBondQueryResult::Unbonded:
            if (adapter_.disconnectPeer(event.peer) != BluetoothAdapterResult::Success) {
                enterError("unbonded peer could not be rejected");
                return;
            }
            pendingRejectedPeers_.push_back(event.peer);
            state_ = BluetoothState::Idle;
            log(core::LogLevel::Warning, "unbonded peer rejected");
            return;
        case BluetoothBondQueryResult::AdapterError:
            (void)adapter_.disconnectPeer(event.peer);
            enterError("peer bond state unavailable");
            return;
        }
        return;
    case BluetoothEventType::PeerDisconnected:
        if (const auto rejected =
                std::find(pendingRejectedPeers_.begin(), pendingRejectedPeers_.end(), event.peer);
            rejected != pendingRejectedPeers_.end()) {
            pendingRejectedPeers_.erase(rejected);
            if (!currentConnection_.has_value() && pendingRejectedPeers_.empty()) {
                scheduleReconnect();
            }
            return;
        }
        if (currentConnection_.has_value() && *currentConnection_ == event.peer) {
            currentConnection_.reset();
            advertisingPendingOrActive_ = false;
            if (pendingRejectedPeers_.empty()) {
                scheduleReconnect();
            } else {
                retryKind_ = RetryKind::None;
                retryElapsed_ = std::chrono::milliseconds::zero();
                state_ = BluetoothState::Idle;
            }
            log(core::LogLevel::Warning, "peer disconnected");
        }
        return;
    case BluetoothEventType::AdapterFailed:
        advertisingPendingOrActive_ = false;
        enterError("adapter reported fatal error");
        return;
    }
}

void BluetoothService::scheduleAdvertisingRetry() {
    advertisingPendingOrActive_ = false;
    retryKind_ = RetryKind::Advertising;
    retryElapsed_ = std::chrono::milliseconds::zero();
    state_ = BluetoothState::RetryWaiting;
    log(core::LogLevel::Warning, "advertising retry scheduled");
}

void BluetoothService::scheduleReconnect() {
    retryKind_ = RetryKind::Reconnect;
    retryElapsed_ = std::chrono::milliseconds::zero();
    state_ = BluetoothState::RetryWaiting;
}

bool BluetoothService::rejectionPending(BluetoothPeerHandle peer) const {
    return std::find(pendingRejectedPeers_.begin(), pendingRejectedPeers_.end(), peer) !=
           pendingRejectedPeers_.end();
}

void BluetoothService::enterError(const char* message) {
    if (cleanupNeeded_ && adapter_.shutdown() == BluetoothAdapterResult::Success) {
        cleanupNeeded_ = false;
    }
    enabled_ = false;
    advertisingPendingOrActive_ = false;
    currentConnection_.reset();
    pendingRejectedPeers_.clear();
    config_.reset();
    retryKind_ = RetryKind::None;
    retryElapsed_ = std::chrono::milliseconds::zero();
    state_ = BluetoothState::Error;
    log(core::LogLevel::Error, message);
}

void BluetoothService::log(core::LogLevel level, const char* message) const {
    if (logger_ != nullptr) {
        logger_->log({level, "bluetooth", message});
    }
}

} // namespace cardputer_hub::connectivity
