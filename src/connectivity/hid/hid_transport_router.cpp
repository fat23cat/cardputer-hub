#include "connectivity/hid/hid_transport_router.h"

#include <algorithm>
#include <variant>

namespace cardputer_hub::connectivity {
namespace {

bool hasNonNeutralKeyboard(const HidTransaction& transaction) noexcept {
    return std::any_of(transaction.frames.begin(), transaction.frames.end(), [](const auto& frame) {
        const auto* report = std::get_if<HidKeyboardReport>(&frame.report);
        return report != nullptr && !isNeutralHidReport(frame.report);
    });
}

bool hasNonNeutralConsumer(const HidTransaction& transaction) noexcept {
    return std::any_of(transaction.frames.begin(), transaction.frames.end(), [](const auto& frame) {
        const auto* report = std::get_if<HidConsumerReport>(&frame.report);
        return report != nullptr && !isNeutralHidReport(frame.report);
    });
}

bool lastReportOfKindIsNeutral(const HidTransaction& transaction, bool keyboard) noexcept {
    for (auto frame = transaction.frames.rbegin(); frame != transaction.frames.rend(); ++frame) {
        const bool matches = keyboard ? std::holds_alternative<HidKeyboardReport>(frame->report)
                                      : std::holds_alternative<HidConsumerReport>(frame->report);
        if (matches) {
            return isNeutralHidReport(frame->report);
        }
    }
    return true;
}

} // namespace

bool isValidHidTransaction(const HidTransaction& transaction) noexcept {
    if (transaction.frames.empty() ||
        transaction.frames.size() > HidTransaction::maximumFrameCount) {
        return false;
    }
    const bool framesValid =
        std::all_of(transaction.frames.begin(), transaction.frames.end(), [](const auto& frame) {
            return frame.dwell >= std::chrono::milliseconds::zero() &&
                   isValidHidReport(frame.report);
        });
    if (!framesValid) {
        return false;
    }
    if (!isNeutralHidReport(transaction.frames.back().report)) {
        return false;
    }
    return (!hasNonNeutralKeyboard(transaction) || lastReportOfKindIsNeutral(transaction, true)) &&
           (!hasNonNeutralConsumer(transaction) || lastReportOfKindIsNeutral(transaction, false));
}

HidTransportRouter::HidTransportRouter(IUsbHidTransport& usb,
                                       IBluetoothHidTarget& bleTarget) noexcept
    : usb_(usb), bleTargetControl_(bleTarget) {
    refreshIdleState();
}

void HidTransportRouter::setBleTarget(std::optional<BluetoothBondReference> target) {
    if (desiredBleTarget_ == target && bleTargetControl_.selectedBond() == target) {
        return;
    }
    desiredBleTarget_ = target;
    if (activeTransaction_.has_value() && activeTransaction_->transport == HidTransportKind::Ble) {
        applyTargetAfterCleanup_ = true;
        beginCleanup(false);
        retryCleanup();
        return;
    }
    applyBleTarget();
    if (!activeTransaction_.has_value()) {
        refreshIdleState();
    }
}

HidRouteResult HidTransportRouter::route(const HidTransaction& transaction) {
    if (!isValidHidTransaction(transaction)) {
        return HidRouteResult::InvalidTransaction;
    }
    if (activeTransaction_.has_value() || cleanupPending_) {
        return state_ == HidRouterState::Error ? HidRouteResult::TransportError
                                               : HidRouteResult::Busy;
    }

    const auto selected = selectTransport();
    if (selected == HidTransportKind::None) {
        refreshIdleState();
        return HidRouteResult::NoReadyTransport;
    }

    activeTransaction_ = ActiveTransaction{transaction, selected};
    state_ = HidRouterState::Dispatching;
    return sendCurrentFrame();
}

void HidTransportRouter::cancel() {
    if (!activeTransaction_.has_value()) {
        refreshIdleState();
        return;
    }
    beginCleanup(state_ == HidRouterState::Error);
    retryCleanup();
}

void HidTransportRouter::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero()) {
        elapsed = std::chrono::milliseconds::zero();
    }
    if (!activeTransaction_.has_value()) {
        refreshIdleState();
        return;
    }
    if (cleanupPending_) {
        retryCleanup();
        return;
    }

    if (activeTransaction_->transport == HidTransportKind::Ble && usbHasTakenOwnership()) {
        beginCleanup(false);
        retryCleanup();
        return;
    }
    if (activeTransaction_->transport == HidTransportKind::Usb && usbNeedsHandover()) {
        if (usb_.linkState() == HidPhysicalLinkState::Disconnected) {
            finishCleanup();
            return;
        }
        beginCleanup(false);
        retryCleanup();
        return;
    }
    if (activeTransaction_->transport == HidTransportKind::Ble) {
        const auto bleState = transport(HidTransportKind::Ble).state();
        if (bleState == HidTransportState::Unavailable) {
            finishCleanup();
            return;
        }
        if (!isReady(transport(HidTransportKind::Ble))) {
            beginCleanup(bleState == HidTransportState::Error);
            retryCleanup();
            return;
        }
    }
    advance(elapsed);
}

HidRouterState HidTransportRouter::state() const noexcept { return state_; }

HidTransportKind HidTransportRouter::activeTransport() const noexcept { return selectTransport(); }

HidTransportKind HidTransportRouter::activeTransactionTransport() const noexcept {
    return activeTransaction_.has_value() ? activeTransaction_->transport : HidTransportKind::None;
}

bool HidTransportRouter::isReady(const IHidTransport& transport) noexcept {
    const auto transportState = transport.state();
    return transportState == HidTransportState::Ready || transportState == HidTransportState::Busy;
}

HidTransportKind HidTransportRouter::selectTransport() const noexcept {
    if (usb_.linkState() == HidPhysicalLinkState::Connected && isReady(usb_)) {
        return HidTransportKind::Usb;
    }
    if (desiredBleTarget_.has_value() && bleTargetControl_.selectedBond() == desiredBleTarget_ &&
        isReady(bleTargetControl_.hidTransport())) {
        return HidTransportKind::Ble;
    }
    return HidTransportKind::None;
}

IHidTransport& HidTransportRouter::transport(HidTransportKind kind) noexcept {
    return kind == HidTransportKind::Usb ? static_cast<IHidTransport&>(usb_)
                                         : bleTargetControl_.hidTransport();
}

const IHidTransport& HidTransportRouter::transport(HidTransportKind kind) const noexcept {
    return kind == HidTransportKind::Usb ? static_cast<const IHidTransport&>(usb_)
                                         : bleTargetControl_.hidTransport();
}

void HidTransportRouter::refreshIdleState() noexcept {
    if (activeTransaction_.has_value()) {
        return;
    }
    state_ = selectTransport() == HidTransportKind::None ? HidRouterState::Unavailable
                                                         : HidRouterState::Idle;
}

void HidTransportRouter::applyBleTarget() { (void)bleTargetControl_.selectBond(desiredBleTarget_); }

HidRouteResult HidTransportRouter::sendCurrentFrame() {
    if (!activeTransaction_.has_value()) {
        refreshIdleState();
        return HidRouteResult::NoReadyTransport;
    }
    const auto result =
        transport(activeTransaction_->transport)
            .send(activeTransaction_->transaction.frames[activeTransaction_->frameIndex].report);
    switch (result) {
    case HidSendResult::Sent:
        activeTransaction_->waitingToSend = false;
        activeTransaction_->hasSentFrame = true;
        activeTransaction_->elapsedSinceFrame = std::chrono::milliseconds::zero();
        if (activeTransaction_->frameIndex + 1 >= activeTransaction_->transaction.frames.size()) {
            activeTransaction_.reset();
            refreshIdleState();
        }
        return HidRouteResult::Accepted;
    case HidSendResult::Busy:
        activeTransaction_->waitingToSend = true;
        return HidRouteResult::Accepted;
    case HidSendResult::NotReady:
        if (activeTransaction_->hasSentFrame) {
            beginCleanup(false);
            return HidRouteResult::Accepted;
        }
        activeTransaction_.reset();
        refreshIdleState();
        return HidRouteResult::NoReadyTransport;
    case HidSendResult::AdapterError:
        beginCleanup(true);
        retryCleanup();
        return HidRouteResult::TransportError;
    }
    beginCleanup(true);
    return HidRouteResult::TransportError;
}

void HidTransportRouter::advance(std::chrono::milliseconds elapsed) {
    if (!activeTransaction_.has_value()) {
        return;
    }
    if (activeTransaction_->waitingToSend) {
        (void)sendCurrentFrame();
        return;
    }

    activeTransaction_->elapsedSinceFrame += elapsed;
    const auto dwell = activeTransaction_->transaction.frames[activeTransaction_->frameIndex].dwell;
    if (activeTransaction_->elapsedSinceFrame < dwell) {
        return;
    }

    ++activeTransaction_->frameIndex;
    activeTransaction_->waitingToSend = true;
    activeTransaction_->elapsedSinceFrame = std::chrono::milliseconds::zero();
    while (activeTransaction_.has_value() && activeTransaction_->waitingToSend) {
        const auto result = sendCurrentFrame();
        if (result != HidRouteResult::Accepted || !activeTransaction_.has_value() ||
            activeTransaction_->waitingToSend) {
            return;
        }
        const auto nextDwell =
            activeTransaction_->transaction.frames[activeTransaction_->frameIndex].dwell;
        if (nextDwell != std::chrono::milliseconds::zero()) {
            return;
        }
        ++activeTransaction_->frameIndex;
        activeTransaction_->waitingToSend = true;
    }
}

void HidTransportRouter::beginCleanup(bool error) {
    if (!activeTransaction_.has_value()) {
        return;
    }
    cleanupPending_ = true;
    cleanupError_ = cleanupError_ || error;
    state_ = cleanupError_ ? HidRouterState::Error : HidRouterState::Handover;
}

void HidTransportRouter::retryCleanup() {
    if (!cleanupPending_ || !activeTransaction_.has_value()) {
        return;
    }
    if (boundTransportDisconnected()) {
        finishCleanup();
        return;
    }
    if (activeTransaction_->transport == HidTransportKind::Usb &&
        usb_.linkState() == HidPhysicalLinkState::Suspended) {
        state_ = cleanupError_ ? HidRouterState::Error : HidRouterState::Handover;
        return;
    }

    switch (transport(activeTransaction_->transport).releaseAll()) {
    case HidSendResult::Sent:
        finishCleanup();
        return;
    case HidSendResult::Busy:
    case HidSendResult::NotReady:
        state_ = cleanupError_ ? HidRouterState::Error : HidRouterState::Handover;
        return;
    case HidSendResult::AdapterError:
        cleanupError_ = true;
        state_ = HidRouterState::Error;
        return;
    }
    cleanupError_ = true;
    state_ = HidRouterState::Error;
}

void HidTransportRouter::finishCleanup() {
    activeTransaction_.reset();
    cleanupPending_ = false;
    cleanupError_ = false;
    if (applyTargetAfterCleanup_) {
        applyTargetAfterCleanup_ = false;
        applyBleTarget();
    }
    refreshIdleState();
}

bool HidTransportRouter::boundTransportDisconnected() const noexcept {
    if (!activeTransaction_.has_value()) {
        return true;
    }
    if (activeTransaction_->transport == HidTransportKind::Usb) {
        return usb_.linkState() == HidPhysicalLinkState::Disconnected;
    }
    return transport(HidTransportKind::Ble).state() == HidTransportState::Unavailable;
}

bool HidTransportRouter::usbNeedsHandover() const noexcept {
    return usb_.linkState() != HidPhysicalLinkState::Connected || !isReady(usb_);
}

bool HidTransportRouter::usbHasTakenOwnership() const noexcept {
    return usb_.linkState() == HidPhysicalLinkState::Connected && isReady(usb_);
}

} // namespace cardputer_hub::connectivity
