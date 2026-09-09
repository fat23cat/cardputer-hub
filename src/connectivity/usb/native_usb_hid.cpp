#include "connectivity/usb/native_usb_hid.h"

namespace cardputer_hub::connectivity {

bool NativeUsbEventQueue::push(const NativeUsbEvent& event) noexcept {
    const auto write = writeIndex_.load(std::memory_order_relaxed);
    const auto read = readIndex_.load(std::memory_order_acquire);
    if (write - read >= capacity) {
        overflowed_.store(true, std::memory_order_release);
        return false;
    }
    events_[write % capacity] = event;
    writeIndex_.store(write + 1, std::memory_order_release);
    return true;
}

NativeUsbPollResult NativeUsbEventQueue::pop() noexcept {
    if (overflowed_.exchange(false, std::memory_order_acq_rel)) {
        return {NativeUsbPollStatus::AdapterError, {}};
    }
    const auto read = readIndex_.load(std::memory_order_relaxed);
    const auto write = writeIndex_.load(std::memory_order_acquire);
    if (read == write) {
        return {NativeUsbPollStatus::NoEvent, {}};
    }
    const auto event = events_[read % capacity];
    readIndex_.store(read + 1, std::memory_order_release);
    return {NativeUsbPollStatus::Event, event};
}

NativeUsbHidService::NativeUsbHidService(INativeUsbAdapter& adapter) noexcept : adapter_(adapter) {}

NativeUsbInitializeResult NativeUsbHidService::initialize() {
    if (initialized_) {
        return NativeUsbInitializeResult::AlreadyInitialized;
    }
    if (error_) {
        return NativeUsbInitializeResult::AdapterError;
    }
    if (adapter_.initialize(lifecycle) != NativeUsbAdapterResult::Success) {
        error_ = true;
        return NativeUsbInitializeResult::AdapterError;
    }
    initialized_ = true;
    return NativeUsbInitializeResult::Initialized;
}

void NativeUsbHidService::update() {
    if (!initialized_ || error_) {
        return;
    }
    for (std::size_t count = 0; count < maxEventsPerUpdate; ++count) {
        const auto result = adapter_.pollEvent();
        if (result.status == NativeUsbPollStatus::NoEvent) {
            return;
        }
        if (result.status == NativeUsbPollStatus::AdapterError) {
            error_ = true;
            return;
        }
        if (result.event.lifecycle == lifecycle) {
            handleEvent(result.event);
        }
    }
}

HidTransportState NativeUsbHidService::state() const noexcept {
    if (error_) {
        return HidTransportState::Error;
    }
    if (!initialized_ || !mounted_ || suspended_) {
        return HidTransportState::Unavailable;
    }
    if (!endpointReady_) {
        return HidTransportState::Starting;
    }
    return busy_ ? HidTransportState::Busy : HidTransportState::Ready;
}

HidSendResult NativeUsbHidService::send(const HidReport& report) {
    if (error_) {
        return HidSendResult::AdapterError;
    }
    if (releaseStage_ != ReleaseStage::None) {
        return HidSendResult::Busy;
    }
    if (state() != HidTransportState::Ready && state() != HidTransportState::Busy) {
        return HidSendResult::NotReady;
    }
    if (!isValidHidReport(report)) {
        return HidSendResult::AdapterError;
    }
    return sendToAdapter(report);
}

HidSendResult NativeUsbHidService::releaseAll() {
    if (error_) {
        return HidSendResult::AdapterError;
    }
    if (!initialized_ || !mounted_) {
        releaseStage_ = ReleaseStage::None;
        busy_ = false;
        return HidSendResult::Sent;
    }
    if (suspended_ || !endpointReady_) {
        return HidSendResult::NotReady;
    }
    if (releaseStage_ == ReleaseStage::None) {
        releaseStage_ = ReleaseStage::Keyboard;
    }
    if (releaseStage_ == ReleaseStage::Keyboard) {
        const auto result = sendToAdapter(HidKeyboardReport::neutral());
        if (result != HidSendResult::Sent) {
            return result;
        }
        releaseStage_ = ReleaseStage::Consumer;
    }
    const auto result = sendToAdapter(HidConsumerReport::neutral());
    if (result == HidSendResult::Sent) {
        releaseStage_ = ReleaseStage::None;
    }
    return result;
}

void NativeUsbHidService::handleEvent(const NativeUsbEvent& event) noexcept {
    switch (event.type) {
    case NativeUsbEventType::Mounted:
        mounted_ = true;
        suspended_ = false;
        endpointReady_ = false;
        busy_ = false;
        releaseStage_ = ReleaseStage::None;
        return;
    case NativeUsbEventType::Unmounted:
        mounted_ = false;
        suspended_ = false;
        endpointReady_ = false;
        busy_ = false;
        releaseStage_ = ReleaseStage::None;
        return;
    case NativeUsbEventType::Suspended:
        suspended_ = true;
        endpointReady_ = false;
        busy_ = false;
        return;
    case NativeUsbEventType::Resumed:
        suspended_ = false;
        endpointReady_ = false;
        busy_ = false;
        return;
    case NativeUsbEventType::EndpointReadinessChanged:
        endpointReady_ = mounted_ && !suspended_ && event.endpointReady;
        if (!endpointReady_) {
            busy_ = false;
        }
        return;
    case NativeUsbEventType::AdapterError:
        error_ = true;
        return;
    }
}

HidSendResult NativeUsbHidService::sendToAdapter(const HidReport& report) {
    switch (adapter_.sendHidReport(report)) {
    case NativeUsbAdapterResult::Success:
        busy_ = false;
        return HidSendResult::Sent;
    case NativeUsbAdapterResult::Unavailable:
        endpointReady_ = false;
        busy_ = false;
        return HidSendResult::NotReady;
    case NativeUsbAdapterResult::Busy:
        busy_ = true;
        return HidSendResult::Busy;
    case NativeUsbAdapterResult::Error:
        error_ = true;
        busy_ = false;
        return HidSendResult::AdapterError;
    }
    error_ = true;
    return HidSendResult::AdapterError;
}

} // namespace cardputer_hub::connectivity
