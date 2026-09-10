#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "connectivity/hid/hid_transport.h"
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

struct BluetoothBondReference {
    std::array<std::uint8_t, 16> bytes{};
};

constexpr bool operator==(const BluetoothBondReference& left,
                          const BluetoothBondReference& right) noexcept {
    for (std::size_t index = 0; index < left.bytes.size(); ++index) {
        if (left.bytes[index] != right.bytes[index]) {
            return false;
        }
    }
    return true;
}

constexpr bool operator!=(const BluetoothBondReference& left,
                          const BluetoothBondReference& right) noexcept {
    return !(left == right);
}

constexpr bool operator<(const BluetoothBondReference& left,
                         const BluetoothBondReference& right) noexcept {
    for (std::size_t index = 0; index < left.bytes.size(); ++index) {
        if (left.bytes[index] != right.bytes[index]) {
            return left.bytes[index] < right.bytes[index];
        }
    }
    return false;
}

enum class BluetoothState : std::uint8_t {
    Disabled,
    Idle,
    Advertising,
    Connected,
    RetryWaiting,
    Error,
};

enum class BluetoothPairingState : std::uint8_t {
    Closed,
    Preparing,
    Advertising,
    AwaitingResponse,
    Completing,
    Succeeded,
    Error,
};

enum class BluetoothPairingChallengeType : std::uint8_t {
    DisplayPasskey,
    EnterPasskey,
    ConfirmComparison,
};

struct BluetoothPairingChallenge {
    std::uint32_t generation = 0;
    BluetoothPairingChallengeType type = BluetoothPairingChallengeType::DisplayPasskey;
    std::optional<std::uint32_t> value;
};

struct BluetoothPairingResponse {
    std::uint32_t generation = 0;
    BluetoothPairingChallengeType type = BluetoothPairingChallengeType::DisplayPasskey;
    bool accepted = true;
    std::string passkey;
};

struct BluetoothSecurityProperties {
    bool secureConnections = false;
    bool encrypted = false;
    bool authenticated = false;
    bool bonded = false;
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
    PairingChallenge,
    PairingCompleted,
    HidReadinessChanged,
    AdapterFailed,
};

struct BluetoothEvent {
    BluetoothEventType type = BluetoothEventType::AdapterFailed;
    BluetoothPeerHandle peer{};
    BluetoothFailureClass failure = BluetoothFailureClass::Fatal;
    std::uint32_t lifecycle = 0;
    BluetoothPairingChallengeType challengeType = BluetoothPairingChallengeType::DisplayPasskey;
    std::optional<std::uint32_t> challengeValue;
    BluetoothSecurityProperties security{};
    bool keyboardSubscribed = false;
    bool consumerSubscribed = false;
    bool reportProtocol = false;

    constexpr BluetoothEvent() noexcept = default;
    constexpr BluetoothEvent(BluetoothEventType eventType, BluetoothPeerHandle eventPeer,
                             BluetoothFailureClass eventFailure,
                             std::uint32_t eventLifecycle) noexcept
        : type(eventType), peer(eventPeer), failure(eventFailure), lifecycle(eventLifecycle) {}
};

enum class BluetoothAdapterResult : std::uint8_t { Success, AdapterError };
enum class BluetoothHidAdapterResult : std::uint8_t {
    Sent,
    Ready,
    NotReady,
    Busy,
    Disconnected,
    AdapterError,
};
enum class BluetoothAdvertisingResult : std::uint8_t {
    Started,
    RetryableFailure,
    AdapterError,
};
enum class BluetoothPollStatus : std::uint8_t { NoEvent, Event, AdapterError };

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

enum class BluetoothBondQueryResult : std::uint8_t { Bonded, Unbonded, AdapterError };
enum class BluetoothBondListStatus : std::uint8_t { Success, AdapterError };

struct BluetoothBondListResult {
    BluetoothBondListStatus status = BluetoothBondListStatus::AdapterError;
    std::vector<BluetoothBondReference> bonds;
};

enum class BluetoothBondReferenceStatus : std::uint8_t { Found, NotFound, AdapterError };

struct BluetoothBondReferenceResult {
    BluetoothBondReferenceStatus status = BluetoothBondReferenceStatus::AdapterError;
    BluetoothBondReference reference{};
};

enum class BluetoothEnableResult : std::uint8_t { Enabled, AlreadyEnabled, AdapterError };
enum class BluetoothDisableResult : std::uint8_t { Disabled, AlreadyDisabled, AdapterError };
enum class BluetoothPairingOpenResult : std::uint8_t {
    Opened,
    AlreadyOpen,
    Disabled,
    CapacityReached,
    AdapterError,
};
enum class BluetoothPairingCancelResult : std::uint8_t {
    Cancelled,
    AlreadyClosed,
    AdapterError,
};
enum class BluetoothPairingResponseResult : std::uint8_t {
    Accepted,
    Rejected,
    NoChallenge,
    StaleGeneration,
    RepeatedResponse,
    WrongKind,
    MalformedPasskey,
    AdapterError,
};
enum class BluetoothBondSelectionResult : std::uint8_t {
    Selected,
    Cleared,
    AlreadySelected,
    NotFound,
    Disabled,
    AdapterError,
};
enum class BluetoothBondRemovalResult : std::uint8_t {
    Removed,
    Pending,
    NotFound,
    Disabled,
    Busy,
    AdapterError,
};
enum class BluetoothRemoveAllBondsResult : std::uint8_t {
    RemovedAll,
    Pending,
    Disabled,
    Busy,
    PartialFailure,
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
    virtual BluetoothAdapterResult beginPairing(BluetoothPeerHandle peer) = 0;
    virtual BluetoothAdapterResult respondToPairing(BluetoothPeerHandle peer,
                                                    BluetoothPairingChallengeType type,
                                                    bool accepted,
                                                    std::optional<std::uint32_t> passkey) = 0;
    virtual BluetoothBondListResult bonds() = 0;
    virtual BluetoothBondReferenceResult bondReference(BluetoothPeerHandle peer) = 0;
    virtual BluetoothAdapterResult deleteBond(const BluetoothBondReference& reference) = 0;
    virtual BluetoothAdapterResult deleteBondForPeer(BluetoothPeerHandle peer) = 0;
    virtual BluetoothHidAdapterResult hidReadiness(BluetoothPeerHandle peer) = 0;
    virtual BluetoothHidAdapterResult sendHidReport(BluetoothPeerHandle peer,
                                                    const HidReport& report) = 0;
    virtual BluetoothHidAdapterResult releaseHidReports(BluetoothPeerHandle peer) = 0;
};

class BluetoothService {
  public:
    static constexpr std::size_t maximumBondCount = 16;
    static constexpr auto pairingWindowDuration = std::chrono::seconds(120);

    explicit BluetoothService(IBluetoothAdapter& adapter) noexcept;
    BluetoothService(IBluetoothAdapter& adapter, core::Logger& logger) noexcept;

    BluetoothEnableResult enable(const BluetoothDeviceConfig& config);
    BluetoothDisableResult disable();
    void update(std::chrono::milliseconds elapsed);
    BluetoothState state() const noexcept;
    std::optional<BluetoothPeerHandle> currentConnection() const noexcept;

    BluetoothPairingOpenResult openPairing();
    BluetoothPairingCancelResult cancelPairing();
    BluetoothPairingResponseResult respondToPairing(const BluetoothPairingResponse& response);
    BluetoothPairingState pairingState() const noexcept;
    std::optional<BluetoothPairingChallenge> pairingChallenge() const noexcept;
    std::optional<BluetoothBondReference> completedPairing() const noexcept;
    BluetoothBondListResult bonds();
    BluetoothBondSelectionResult selectBond(std::optional<BluetoothBondReference> reference);
    std::optional<BluetoothBondReference> selectedBond() const noexcept;
    BluetoothBondRemovalResult removeBond(const BluetoothBondReference& reference);
    BluetoothBondRemovalResult lastBondRemovalResult() const noexcept;
    BluetoothRemoveAllBondsResult removeAllBonds();
    BluetoothRemoveAllBondsResult lastRemoveAllResult() const noexcept;
    IHidTransport& hidTransport() noexcept;
    const IHidTransport& hidTransport() const noexcept;

  private:
    class HidTransportView final : public IHidTransport {
      public:
        explicit HidTransportView(BluetoothService& service) noexcept : service_(service) {}

        HidTransportState state() const noexcept override;
        HidSendResult send(const HidReport& report) override;
        HidSendResult releaseAll() override;

      private:
        BluetoothService& service_;
    };

    enum class RetryKind : std::uint8_t { None, Reconnect, Advertising };
    enum class PendingBondOperation : std::uint8_t { None, RemoveOne, RemoveAll };

    BluetoothAdvertisingResult launchAdvertising();
    void handleEvent(const BluetoothEvent& event);
    void handleConnectedPeer(BluetoothPeerHandle peer);
    void handlePairingChallenge(const BluetoothEvent& event);
    void handlePairingCompleted(const BluetoothEvent& event);
    void handleDisconnectedPeer(BluetoothPeerHandle peer);
    void scheduleAdvertisingRetry();
    void scheduleReconnect();
    void resumeAfterDisconnection();
    void closePairing(BluetoothPairingState terminalState);
    bool pairingWindowActive() const noexcept;
    bool rejectPeer(BluetoothPeerHandle peer, const char* failureMessage);
    bool rejectionPending(BluetoothPeerHandle peer) const;
    static bool containsBond(const std::vector<BluetoothBondReference>& bonds,
                             const BluetoothBondReference& reference);
    void completePendingBondOperation();
    void enterError(const char* message);
    void log(core::LogLevel level, const char* message) const;
    HidTransportState hidTransportState() const noexcept;
    HidSendResult sendHidReport(const HidReport& report);
    HidSendResult releaseAllHidReports();
    void clearHidPeerState() noexcept;
    HidSendResult handleHidAdapterResult(BluetoothHidAdapterResult result);

    IBluetoothAdapter& adapter_;
    HidTransportView hidTransportView_;
    core::Logger* logger_ = nullptr;
    std::optional<BluetoothDeviceConfig> config_;
    std::optional<BluetoothPeerHandle> currentConnection_;
    std::optional<BluetoothBondReference> currentBond_;
    std::optional<BluetoothBondReference> selectedBond_;
    std::optional<BluetoothPeerHandle> pairingPeer_;
    std::optional<BluetoothPairingChallenge> pairingChallenge_;
    std::optional<BluetoothBondReference> completedPairing_;
    std::optional<std::uint32_t> lastRespondedGeneration_;
    std::vector<BluetoothPeerHandle> pendingRejectedPeers_;
    std::optional<BluetoothBondReference> pendingRemoval_;
    PendingBondOperation pendingBondOperation_ = PendingBondOperation::None;
    BluetoothRemoveAllBondsResult lastRemoveAllResult_ = BluetoothRemoveAllBondsResult::RemovedAll;
    BluetoothBondRemovalResult lastBondRemovalResult_ = BluetoothBondRemovalResult::Removed;
    BluetoothState state_ = BluetoothState::Disabled;
    BluetoothPairingState pairingState_ = BluetoothPairingState::Closed;
    std::chrono::milliseconds retryElapsed_{0};
    std::chrono::milliseconds pairingElapsed_{0};
    std::size_t retryIndex_ = 0;
    RetryKind retryKind_ = RetryKind::None;
    bool cleanupNeeded_ = false;
    bool enabled_ = false;
    bool advertisingPendingOrActive_ = false;
    BluetoothSecurityProperties currentSecurity_{};
    bool keyboardSubscribed_ = false;
    bool consumerSubscribed_ = false;
    bool reportProtocol_ = false;
    bool hidBusy_ = false;
    std::uint32_t lifecycle_ = 0;
    std::uint32_t nextLifecycle_ = 1;
    std::uint32_t nextChallengeGeneration_ = 1;
};

} // namespace cardputer_hub::connectivity
