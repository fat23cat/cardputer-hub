#include "connectivity/bluetooth/bluetooth_service.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <numeric>

namespace cardputer_hub::connectivity {
namespace {

constexpr auto reconnectDelay = std::chrono::seconds(1);
constexpr std::array advertisingRetryDelays{
    std::chrono::seconds(1), std::chrono::seconds(2),  std::chrono::seconds(4),
    std::chrono::seconds(8), std::chrono::seconds(16), std::chrono::seconds(30),
};
constexpr std::size_t maxEventsPerUpdate = 16;

std::optional<std::uint32_t> parsePasskey(const std::string& value) {
    if (value.size() != 6 || !std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        return std::nullopt;
    }
    return std::accumulate(value.begin(), value.end(), std::uint32_t{0},
                           [](std::uint32_t result, char character) {
                               return result * 10 + static_cast<std::uint32_t>(character - '0');
                           });
}

} // namespace

BluetoothService::BluetoothService(IBluetoothAdapter& adapter) noexcept
    : adapter_(adapter), hidTransportView_(*this) {}

BluetoothService::BluetoothService(IBluetoothAdapter& adapter, core::Logger& logger) noexcept
    : adapter_(adapter), hidTransportView_(*this), logger_(&logger) {}

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
    pairingState_ = BluetoothPairingState::Closed;
    pairingElapsed_ = std::chrono::milliseconds::zero();
    pairingPeer_.reset();
    pairingChallenge_.reset();
    completedPairing_.reset();
    lastRespondedGeneration_.reset();
    pendingRejectedPeers_.clear();
    pendingBondOperation_ = PendingBondOperation::None;
    pendingRemoval_.reset();
    clearHidPeerState();

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
    pairingState_ = BluetoothPairingState::Closed;
    pairingElapsed_ = std::chrono::milliseconds::zero();
    pairingPeer_.reset();
    pairingChallenge_.reset();
    completedPairing_.reset();
    lastRespondedGeneration_.reset();

    if (currentConnection_.has_value() && selectedBond_.has_value() &&
        currentBond_ == selectedBond_) {
        const auto releaseResult = adapter_.releaseHidReports(*currentConnection_);
        if (releaseResult == BluetoothHidAdapterResult::AdapterError) {
            cleanupFailed = true;
        }
    }

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
    currentBond_.reset();
    clearHidPeerState();
    pendingRejectedPeers_.clear();
    pendingBondOperation_ = PendingBondOperation::None;
    pendingRemoval_.reset();
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

    if (enabled_ && pairingWindowActive() && pairingState_ != BluetoothPairingState::Preparing) {
        if (elapsed >= pairingWindowDuration - pairingElapsed_) {
            pairingElapsed_ = pairingWindowDuration;
            if (cancelPairing() == BluetoothPairingCancelResult::AdapterError) {
                return;
            }
        } else {
            pairingElapsed_ += elapsed;
        }
    }

    if (enabled_ && pendingBondOperation_ != PendingBondOperation::None &&
        !currentConnection_.has_value()) {
        completePendingBondOperation();
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

IHidTransport& BluetoothService::hidTransport() noexcept { return hidTransportView_; }

const IHidTransport& BluetoothService::hidTransport() const noexcept { return hidTransportView_; }

HidTransportState BluetoothService::HidTransportView::state() const noexcept {
    return service_.hidTransportState();
}

HidSendResult BluetoothService::HidTransportView::send(const HidReport& report) {
    return service_.sendHidReport(report);
}

HidSendResult BluetoothService::HidTransportView::releaseAll() {
    return service_.releaseAllHidReports();
}

std::optional<BluetoothPeerHandle> BluetoothService::currentConnection() const noexcept {
    return currentConnection_;
}

BluetoothPairingOpenResult BluetoothService::openPairing() {
    if (!enabled_ || state_ == BluetoothState::Error) {
        return BluetoothPairingOpenResult::Disabled;
    }
    if (pairingWindowActive()) {
        return BluetoothPairingOpenResult::AlreadyOpen;
    }
    const auto knownBonds = adapter_.bonds();
    if (knownBonds.status != BluetoothBondListStatus::Success) {
        enterError("bond registry unavailable");
        return BluetoothPairingOpenResult::AdapterError;
    }
    if (knownBonds.bonds.size() >= maximumBondCount) {
        return BluetoothPairingOpenResult::CapacityReached;
    }

    pairingElapsed_ = std::chrono::milliseconds::zero();
    pairingPeer_.reset();
    pairingChallenge_.reset();
    completedPairing_.reset();
    lastRespondedGeneration_.reset();
    pairingState_ = state_ == BluetoothState::Advertising ? BluetoothPairingState::Advertising
                                                          : BluetoothPairingState::Preparing;
    if (currentConnection_.has_value()) {
        if (selectedBond_.has_value() && currentBond_ == selectedBond_ &&
            adapter_.releaseHidReports(*currentConnection_) ==
                BluetoothHidAdapterResult::AdapterError) {
            pairingState_ = BluetoothPairingState::Error;
            enterError("HID release failed before pairing");
            return BluetoothPairingOpenResult::AdapterError;
        }
        if (adapter_.disconnectPeer(*currentConnection_) != BluetoothAdapterResult::Success) {
            pairingState_ = BluetoothPairingState::Error;
            enterError("connected peer could not be released for pairing");
            return BluetoothPairingOpenResult::AdapterError;
        }
    }
    log(core::LogLevel::Info, "pairing window opened");
    return BluetoothPairingOpenResult::Opened;
}

BluetoothPairingCancelResult BluetoothService::cancelPairing() {
    if (!pairingWindowActive()) {
        return BluetoothPairingCancelResult::AlreadyClosed;
    }
    pairingChallenge_.reset();
    if (pairingPeer_.has_value()) {
        const auto peer = *pairingPeer_;
        pairingPeer_.reset();
        if (!rejectPeer(peer, "incomplete pairing peer could not be rejected")) {
            pairingState_ = BluetoothPairingState::Error;
            return BluetoothPairingCancelResult::AdapterError;
        }
    }
    closePairing(BluetoothPairingState::Closed);
    log(core::LogLevel::Info, "pairing window closed");
    return BluetoothPairingCancelResult::Cancelled;
}

BluetoothPairingResponseResult
BluetoothService::respondToPairing(const BluetoothPairingResponse& response) {
    if (!pairingChallenge_.has_value() || !pairingPeer_.has_value()) {
        if (lastRespondedGeneration_.has_value() &&
            response.generation == *lastRespondedGeneration_) {
            return BluetoothPairingResponseResult::RepeatedResponse;
        }
        return BluetoothPairingResponseResult::NoChallenge;
    }
    if (response.generation != pairingChallenge_->generation) {
        return BluetoothPairingResponseResult::StaleGeneration;
    }
    if (response.type != pairingChallenge_->type) {
        return BluetoothPairingResponseResult::WrongKind;
    }

    std::optional<std::uint32_t> passkey;
    if (response.type == BluetoothPairingChallengeType::EnterPasskey) {
        passkey = parsePasskey(response.passkey);
        if (!passkey.has_value()) {
            return BluetoothPairingResponseResult::MalformedPasskey;
        }
    } else if (!response.passkey.empty()) {
        return BluetoothPairingResponseResult::MalformedPasskey;
    } else if (response.type == BluetoothPairingChallengeType::DisplayPasskey) {
        passkey = pairingChallenge_->value;
    }

    if (adapter_.respondToPairing(*pairingPeer_, response.type, response.accepted, passkey) !=
        BluetoothAdapterResult::Success) {
        pairingState_ = BluetoothPairingState::Error;
        enterError("pairing response failed");
        return BluetoothPairingResponseResult::AdapterError;
    }
    lastRespondedGeneration_ = response.generation;
    pairingChallenge_.reset();
    pairingState_ = BluetoothPairingState::Completing;
    return response.accepted ? BluetoothPairingResponseResult::Accepted
                             : BluetoothPairingResponseResult::Rejected;
}

BluetoothPairingState BluetoothService::pairingState() const noexcept { return pairingState_; }

std::optional<BluetoothPairingChallenge> BluetoothService::pairingChallenge() const noexcept {
    return pairingChallenge_;
}

std::optional<BluetoothBondReference> BluetoothService::completedPairing() const noexcept {
    return completedPairing_;
}

BluetoothBondListResult BluetoothService::bonds() {
    if (!enabled_) {
        return {BluetoothBondListStatus::AdapterError, {}};
    }
    return adapter_.bonds();
}

BluetoothBondSelectionResult
BluetoothService::selectBond(std::optional<BluetoothBondReference> reference) {
    if (!enabled_ || state_ == BluetoothState::Error) {
        return BluetoothBondSelectionResult::Disabled;
    }
    if (reference == selectedBond_) {
        return BluetoothBondSelectionResult::AlreadySelected;
    }
    if (currentConnection_.has_value() && selectedBond_.has_value() &&
        currentBond_ == selectedBond_ &&
        adapter_.releaseHidReports(*currentConnection_) ==
            BluetoothHidAdapterResult::AdapterError) {
        enterError("HID release failed before target selection changed");
        return BluetoothBondSelectionResult::AdapterError;
    }
    if (!reference.has_value()) {
        selectedBond_.reset();
        hidBusy_ = false;
        return BluetoothBondSelectionResult::Cleared;
    }
    const auto knownBonds = adapter_.bonds();
    if (knownBonds.status != BluetoothBondListStatus::Success) {
        enterError("bond registry unavailable during selection");
        return BluetoothBondSelectionResult::AdapterError;
    }
    if (!containsBond(knownBonds.bonds, *reference)) {
        return BluetoothBondSelectionResult::NotFound;
    }
    selectedBond_ = reference;
    hidBusy_ = false;
    if (currentConnection_.has_value() && currentBond_ != selectedBond_) {
        if (adapter_.disconnectPeer(*currentConnection_) != BluetoothAdapterResult::Success) {
            enterError("old selected peer could not be disconnected");
            return BluetoothBondSelectionResult::AdapterError;
        }
    }
    return BluetoothBondSelectionResult::Selected;
}

std::optional<BluetoothBondReference> BluetoothService::selectedBond() const noexcept {
    return selectedBond_;
}

BluetoothBondRemovalResult BluetoothService::removeBond(const BluetoothBondReference& reference) {
    if (!enabled_ || state_ == BluetoothState::Error) {
        return BluetoothBondRemovalResult::Disabled;
    }
    if (pendingBondOperation_ != PendingBondOperation::None) {
        return BluetoothBondRemovalResult::Busy;
    }
    const auto knownBonds = adapter_.bonds();
    if (knownBonds.status != BluetoothBondListStatus::Success) {
        enterError("bond registry unavailable during removal");
        return BluetoothBondRemovalResult::AdapterError;
    }
    if (!containsBond(knownBonds.bonds, reference)) {
        return BluetoothBondRemovalResult::NotFound;
    }
    if (currentConnection_.has_value() && currentBond_ == reference) {
        pendingBondOperation_ = PendingBondOperation::RemoveOne;
        pendingRemoval_ = reference;
        lastBondRemovalResult_ = BluetoothBondRemovalResult::Pending;
        if (selectedBond_ == currentBond_ && adapter_.releaseHidReports(*currentConnection_) ==
                                                 BluetoothHidAdapterResult::AdapterError) {
            pendingBondOperation_ = PendingBondOperation::None;
            pendingRemoval_.reset();
            enterError("HID release failed before active bond removal");
            return lastBondRemovalResult_ = BluetoothBondRemovalResult::AdapterError;
        }
        if (adapter_.disconnectPeer(*currentConnection_) != BluetoothAdapterResult::Success) {
            pendingBondOperation_ = PendingBondOperation::None;
            pendingRemoval_.reset();
            return lastBondRemovalResult_ = BluetoothBondRemovalResult::AdapterError;
        }
        return BluetoothBondRemovalResult::Pending;
    }
    pendingBondOperation_ = PendingBondOperation::RemoveOne;
    pendingRemoval_ = reference;
    lastBondRemovalResult_ = BluetoothBondRemovalResult::Pending;
    return BluetoothBondRemovalResult::Pending;
}

BluetoothBondRemovalResult BluetoothService::lastBondRemovalResult() const noexcept {
    return lastBondRemovalResult_;
}

BluetoothRemoveAllBondsResult BluetoothService::removeAllBonds() {
    if (!enabled_ || state_ == BluetoothState::Error) {
        return BluetoothRemoveAllBondsResult::Disabled;
    }
    if (pendingBondOperation_ != PendingBondOperation::None) {
        return BluetoothRemoveAllBondsResult::Busy;
    }
    const auto knownBonds = adapter_.bonds();
    if (knownBonds.status != BluetoothBondListStatus::Success) {
        return lastRemoveAllResult_ = BluetoothRemoveAllBondsResult::AdapterError;
    }
    if (knownBonds.bonds.empty()) {
        selectedBond_.reset();
        return lastRemoveAllResult_ = BluetoothRemoveAllBondsResult::RemovedAll;
    }

    pendingBondOperation_ = PendingBondOperation::RemoveAll;
    lastRemoveAllResult_ = BluetoothRemoveAllBondsResult::Pending;
    if (currentConnection_.has_value()) {
        if (selectedBond_ == currentBond_ && adapter_.releaseHidReports(*currentConnection_) ==
                                                 BluetoothHidAdapterResult::AdapterError) {
            pendingBondOperation_ = PendingBondOperation::None;
            lastRemoveAllResult_ = BluetoothRemoveAllBondsResult::AdapterError;
            enterError("HID release failed before all bonds were removed");
            return lastRemoveAllResult_;
        }
        if (adapter_.disconnectPeer(*currentConnection_) != BluetoothAdapterResult::Success) {
            pendingBondOperation_ = PendingBondOperation::None;
            lastRemoveAllResult_ = BluetoothRemoveAllBondsResult::AdapterError;
            return lastRemoveAllResult_;
        }
    }
    return lastRemoveAllResult_;
}

BluetoothRemoveAllBondsResult BluetoothService::lastRemoveAllResult() const noexcept {
    return lastRemoveAllResult_;
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
            if (pairingState_ == BluetoothPairingState::Preparing) {
                pairingState_ = BluetoothPairingState::Advertising;
                pairingElapsed_ = std::chrono::milliseconds::zero();
            }
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
        handleConnectedPeer(event.peer);
        return;
    case BluetoothEventType::PeerDisconnected:
        handleDisconnectedPeer(event.peer);
        return;
    case BluetoothEventType::PairingChallenge:
        handlePairingChallenge(event);
        return;
    case BluetoothEventType::PairingCompleted:
        handlePairingCompleted(event);
        return;
    case BluetoothEventType::HidReadinessChanged:
        if (currentConnection_.has_value() && event.peer == *currentConnection_) {
            currentSecurity_ = event.security;
            keyboardSubscribed_ = event.keyboardSubscribed;
            consumerSubscribed_ = event.consumerSubscribed;
            reportProtocol_ = event.reportProtocol;
            hidBusy_ = false;
        }
        return;
    case BluetoothEventType::AdapterFailed:
        advertisingPendingOrActive_ = false;
        enterError("adapter reported fatal error");
        return;
    }
}

void BluetoothService::handleConnectedPeer(BluetoothPeerHandle peer) {
    advertisingPendingOrActive_ = false;
    retryKind_ = RetryKind::None;
    retryElapsed_ = std::chrono::milliseconds::zero();
    if (currentConnection_.has_value() || !pendingRejectedPeers_.empty() ||
        pairingPeer_.has_value()) {
        if ((currentConnection_.has_value() && *currentConnection_ == peer) ||
            rejectionPending(peer) || (pairingPeer_.has_value() && *pairingPeer_ == peer)) {
            return;
        }
        (void)rejectPeer(peer, "additional peer could not be rejected");
        return;
    }
    clearHidPeerState();

    const auto bondState = adapter_.bondState(peer);
    if (pairingWindowActive()) {
        if (bondState == BluetoothBondQueryResult::Unbonded) {
            if (adapter_.beginPairing(peer) != BluetoothAdapterResult::Success) {
                (void)adapter_.disconnectPeer(peer);
                pairingState_ = BluetoothPairingState::Error;
                enterError("security initiation failed");
                return;
            }
            pairingPeer_ = peer;
            pairingState_ = BluetoothPairingState::Completing;
            state_ = BluetoothState::Idle;
            return;
        }
        if (bondState == BluetoothBondQueryResult::AdapterError) {
            (void)adapter_.disconnectPeer(peer);
            enterError("peer bond state unavailable");
            return;
        }
        (void)rejectPeer(peer, "bonded peer could not be rejected during pairing");
        return;
    }

    if (bondState == BluetoothBondQueryResult::Bonded) {
        const auto reference = adapter_.bondReference(peer);
        if (reference.status != BluetoothBondReferenceStatus::Found) {
            (void)adapter_.disconnectPeer(peer);
            enterError("bond reference unavailable");
            return;
        }
        if (selectedBond_.has_value() && reference.reference != *selectedBond_) {
            (void)rejectPeer(peer, "unselected bonded peer could not be rejected");
            return;
        }
        currentConnection_ = peer;
        currentBond_ = reference.reference;
        state_ = BluetoothState::Connected;
        retryIndex_ = 0;
        log(core::LogLevel::Info, "bonded peer connected");
        return;
    }
    if (bondState == BluetoothBondQueryResult::Unbonded) {
        (void)rejectPeer(peer, "unbonded peer could not be rejected");
        log(core::LogLevel::Warning, "unbonded peer rejected");
        return;
    }
    (void)adapter_.disconnectPeer(peer);
    enterError("peer bond state unavailable");
}

void BluetoothService::handlePairingChallenge(const BluetoothEvent& event) {
    if (!pairingWindowActive() || !pairingPeer_.has_value() || event.peer != *pairingPeer_ ||
        pairingChallenge_.has_value()) {
        (void)rejectPeer(event.peer, "unexpected security challenge could not be rejected");
        return;
    }
    if ((event.challengeType != BluetoothPairingChallengeType::EnterPasskey &&
         (!event.challengeValue.has_value() || *event.challengeValue > 999'999)) ||
        (event.challengeType == BluetoothPairingChallengeType::EnterPasskey &&
         event.challengeValue.has_value())) {
        (void)rejectPeer(event.peer, "invalid security challenge could not be rejected");
        pairingState_ = BluetoothPairingState::Error;
        return;
    }
    const auto generation = nextChallengeGeneration_++;
    if (nextChallengeGeneration_ == 0) {
        nextChallengeGeneration_ = 1;
    }
    pairingChallenge_ = {generation, event.challengeType, event.challengeValue};
    pairingState_ = BluetoothPairingState::AwaitingResponse;
}

void BluetoothService::handlePairingCompleted(const BluetoothEvent& event) {
    if (!pairingWindowActive() || !pairingPeer_.has_value() || event.peer != *pairingPeer_) {
        return;
    }
    pairingChallenge_.reset();
    if (!(event.security.secureConnections && event.security.encrypted &&
          event.security.authenticated && event.security.bonded)) {
        if (event.security.bonded &&
            adapter_.deleteBondForPeer(event.peer) != BluetoothAdapterResult::Success) {
            enterError("rejected bond cleanup failed");
            return;
        }
        const auto peer = *pairingPeer_;
        pairingPeer_.reset();
        if (!rejectPeer(peer, "insecure pairing peer could not be rejected")) {
            pairingState_ = BluetoothPairingState::Error;
            return;
        }
        pairingState_ = BluetoothPairingState::Error;
        return;
    }

    const auto reference = adapter_.bondReference(event.peer);
    if (reference.status != BluetoothBondReferenceStatus::Found) {
        (void)adapter_.deleteBondForPeer(event.peer);
        (void)adapter_.disconnectPeer(event.peer);
        enterError("new bond finalization failed");
        return;
    }
    currentConnection_ = event.peer;
    currentBond_ = reference.reference;
    currentSecurity_ = event.security;
    pairingPeer_.reset();
    completedPairing_ = reference.reference;
    pairingState_ = BluetoothPairingState::Succeeded;
    pairingElapsed_ = std::chrono::milliseconds::zero();
    state_ = BluetoothState::Connected;
    log(core::LogLevel::Info, "authenticated pairing completed");
}

void BluetoothService::handleDisconnectedPeer(BluetoothPeerHandle peer) {
    if (pairingPeer_.has_value() && *pairingPeer_ == peer) {
        pairingPeer_.reset();
        pairingChallenge_.reset();
        if (pairingWindowActive()) {
            pairingState_ = BluetoothPairingState::Error;
        }
        if (pendingRejectedPeers_.empty()) {
            scheduleReconnect();
        }
        return;
    }
    if (const auto rejected =
            std::find(pendingRejectedPeers_.begin(), pendingRejectedPeers_.end(), peer);
        rejected != pendingRejectedPeers_.end()) {
        pendingRejectedPeers_.erase(rejected);
        if (!currentConnection_.has_value() && pendingRejectedPeers_.empty()) {
            resumeAfterDisconnection();
        }
        return;
    }
    if (currentConnection_.has_value() && *currentConnection_ == peer) {
        currentConnection_.reset();
        currentBond_.reset();
        clearHidPeerState();
        advertisingPendingOrActive_ = false;
        if (pendingBondOperation_ != PendingBondOperation::None) {
            completePendingBondOperation();
            if (state_ == BluetoothState::Error) {
                return;
            }
        }
        if (pairingWindowActive()) {
            const auto result = launchAdvertising();
            if (result == BluetoothAdvertisingResult::RetryableFailure) {
                scheduleAdvertisingRetry();
            } else if (result == BluetoothAdvertisingResult::AdapterError) {
                enterError("pairing advertising launch failed");
            }
        } else if (pendingRejectedPeers_.empty()) {
            scheduleReconnect();
        } else {
            state_ = BluetoothState::Idle;
        }
        log(core::LogLevel::Warning, "peer disconnected");
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

void BluetoothService::resumeAfterDisconnection() {
    if (pairingWindowActive()) {
        const auto result = launchAdvertising();
        if (result == BluetoothAdvertisingResult::RetryableFailure) {
            scheduleAdvertisingRetry();
        } else if (result == BluetoothAdvertisingResult::AdapterError) {
            enterError("pairing advertising launch failed");
        }
    } else {
        scheduleReconnect();
    }
}

void BluetoothService::closePairing(BluetoothPairingState terminalState) {
    pairingState_ = terminalState;
    pairingElapsed_ = std::chrono::milliseconds::zero();
    pairingChallenge_.reset();
}

bool BluetoothService::pairingWindowActive() const noexcept {
    return pairingState_ == BluetoothPairingState::Preparing ||
           pairingState_ == BluetoothPairingState::Advertising ||
           pairingState_ == BluetoothPairingState::AwaitingResponse ||
           pairingState_ == BluetoothPairingState::Completing;
}

bool BluetoothService::rejectPeer(BluetoothPeerHandle peer, const char* failureMessage) {
    if (adapter_.disconnectPeer(peer) != BluetoothAdapterResult::Success) {
        enterError(failureMessage);
        return false;
    }
    if (!rejectionPending(peer)) {
        pendingRejectedPeers_.push_back(peer);
    }
    if (!currentConnection_.has_value()) {
        state_ = BluetoothState::Idle;
    }
    return true;
}

bool BluetoothService::rejectionPending(BluetoothPeerHandle peer) const {
    return std::find(pendingRejectedPeers_.begin(), pendingRejectedPeers_.end(), peer) !=
           pendingRejectedPeers_.end();
}

bool BluetoothService::containsBond(const std::vector<BluetoothBondReference>& bonds,
                                    const BluetoothBondReference& reference) {
    return std::find(bonds.begin(), bonds.end(), reference) != bonds.end();
}

void BluetoothService::completePendingBondOperation() {
    if (pendingBondOperation_ == PendingBondOperation::RemoveOne && pendingRemoval_.has_value()) {
        if (adapter_.deleteBond(*pendingRemoval_) != BluetoothAdapterResult::Success) {
            lastBondRemovalResult_ = BluetoothBondRemovalResult::AdapterError;
        } else {
            if (selectedBond_ == pendingRemoval_) {
                selectedBond_.reset();
            }
            lastBondRemovalResult_ = BluetoothBondRemovalResult::Removed;
        }
    } else if (pendingBondOperation_ == PendingBondOperation::RemoveAll) {
        const auto knownBonds = adapter_.bonds();
        bool failed = knownBonds.status != BluetoothBondListStatus::Success;
        if (!failed) {
            for (const auto& reference : knownBonds.bonds) {
                if (adapter_.deleteBond(reference) != BluetoothAdapterResult::Success) {
                    failed = true;
                }
            }
        }
        selectedBond_.reset();
        lastRemoveAllResult_ = failed ? BluetoothRemoveAllBondsResult::PartialFailure
                                      : BluetoothRemoveAllBondsResult::RemovedAll;
    }
    pendingBondOperation_ = PendingBondOperation::None;
    pendingRemoval_.reset();
}

void BluetoothService::enterError(const char* message) {
    if (cleanupNeeded_ && adapter_.shutdown() == BluetoothAdapterResult::Success) {
        cleanupNeeded_ = false;
    }
    enabled_ = false;
    advertisingPendingOrActive_ = false;
    currentConnection_.reset();
    currentBond_.reset();
    clearHidPeerState();
    pairingPeer_.reset();
    pairingChallenge_.reset();
    if (pairingWindowActive()) {
        pairingState_ = BluetoothPairingState::Error;
    }
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

HidTransportState BluetoothService::hidTransportState() const noexcept {
    if (state_ == BluetoothState::Error) {
        return HidTransportState::Error;
    }
    if (!enabled_ || state_ != BluetoothState::Connected || pairingWindowActive() ||
        !currentConnection_.has_value() || !selectedBond_.has_value() ||
        currentBond_ != selectedBond_) {
        return HidTransportState::Unavailable;
    }
    if (!(currentSecurity_.encrypted && currentSecurity_.authenticated && currentSecurity_.bonded &&
          keyboardSubscribed_ && consumerSubscribed_ && reportProtocol_)) {
        return HidTransportState::Starting;
    }
    return hidBusy_ ? HidTransportState::Busy : HidTransportState::Ready;
}

HidSendResult BluetoothService::sendHidReport(const HidReport& report) {
    if (!isValidHidReport(report)) {
        return HidSendResult::AdapterError;
    }
    const auto transportState = hidTransportState();
    if ((transportState != HidTransportState::Ready && transportState != HidTransportState::Busy) ||
        !currentConnection_.has_value()) {
        return transportState == HidTransportState::Error ? HidSendResult::AdapterError
                                                          : HidSendResult::NotReady;
    }
    const auto readiness = adapter_.hidReadiness(*currentConnection_);
    if (readiness != BluetoothHidAdapterResult::Ready) {
        return handleHidAdapterResult(readiness);
    }
    return handleHidAdapterResult(adapter_.sendHidReport(*currentConnection_, report));
}

HidSendResult BluetoothService::releaseAllHidReports() {
    if (!currentConnection_.has_value()) {
        return HidSendResult::Sent;
    }
    if (!selectedBond_.has_value() || currentBond_ != selectedBond_) {
        return HidSendResult::NotReady;
    }
    const auto result = adapter_.releaseHidReports(*currentConnection_);
    if (result == BluetoothHidAdapterResult::Disconnected) {
        clearHidPeerState();
        return HidSendResult::Sent;
    }
    return handleHidAdapterResult(result);
}

void BluetoothService::clearHidPeerState() noexcept {
    currentSecurity_ = {};
    keyboardSubscribed_ = false;
    consumerSubscribed_ = false;
    reportProtocol_ = false;
    hidBusy_ = false;
}

HidSendResult BluetoothService::handleHidAdapterResult(BluetoothHidAdapterResult result) {
    switch (result) {
    case BluetoothHidAdapterResult::Sent:
        hidBusy_ = false;
        return HidSendResult::Sent;
    case BluetoothHidAdapterResult::Busy:
        hidBusy_ = true;
        return HidSendResult::Busy;
    case BluetoothHidAdapterResult::Ready:
    case BluetoothHidAdapterResult::NotReady:
    case BluetoothHidAdapterResult::Disconnected:
        clearHidPeerState();
        return HidSendResult::NotReady;
    case BluetoothHidAdapterResult::AdapterError:
        enterError("BLE HID adapter failed");
        return HidSendResult::AdapterError;
    }
    enterError("BLE HID adapter returned an invalid result");
    return HidSendResult::AdapterError;
}

} // namespace cardputer_hub::connectivity
