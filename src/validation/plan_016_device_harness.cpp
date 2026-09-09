#include "validation/plan_016_device_harness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace cardputer_hub::validation {
namespace {

constexpr core::RgbColor black{0, 0, 0};
constexpr core::RgbColor white{255, 255, 255};
constexpr core::TextStyle usbStateStyle{white, black, 1};

} // namespace

Plan016DeviceHarness::Plan016DeviceHarness(core::SystemRuntime& runtime,
                                           connectivity::NativeUsbHidService& nativeUsb,
                                           core::IDisplayAdapter& display) noexcept
    : runtime_(runtime), nativeUsb_(nativeUsb), display_(display) {}

void Plan016DeviceHarness::start() {
    if (started_) {
        return;
    }
    started_ = true;
    runtime_.start();
    std::printf("[VALIDATION 016] native USB CDC/HID harness active\n");
    std::printf("[VALIDATION 016] commands: status, key, six, consumer, release\n");
}

void Plan016DeviceHarness::update() {
    if (!started_) {
        return;
    }
    (void)runtime_.update();
    const auto usbState = nativeUsb_.state();
    if (usbState == connectivity::HidTransportState::Ready ||
        usbState == connectivity::HidTransportState::Busy) {
        hasBeenReady_ = true;
    } else if (hasBeenReady_ && usbState == connectivity::HidTransportState::Unavailable) {
        observedUnavailableAfterReady_ = true;
    }
    if (!displayedStateInitialized_ || usbState != displayedState_) {
        displayUsbState(usbState);
    }
    readConsole();
}

void Plan016DeviceHarness::readConsole() {
    for (;;) {
        unsigned char next = 0;
        if (::read(STDIN_FILENO, &next, 1) != 1) {
            return;
        }
        if (next == '\r') {
            continue;
        }
        if (next == '\n') {
            if (inputOverflow_) {
                std::printf("[VALIDATION 016] input rejected: line too long\n");
            } else {
                input_[inputLength_] = '\0';
                handleCommand(input_.data());
            }
            input_.fill('\0');
            inputLength_ = 0;
            inputOverflow_ = false;
        } else if (!inputOverflow_ && inputLength_ + 1 < input_.size()) {
            input_[inputLength_++] = static_cast<char>(next);
        } else {
            inputOverflow_ = true;
        }
    }
}

void Plan016DeviceHarness::handleCommand(const char* command) {
    connectivity::HidSendResult result = connectivity::HidSendResult::NotReady;
    if (std::strcmp(command, "status") == 0) {
        std::printf("[VALIDATION 016] USB HID state=%s unavailable_after_ready=%s\n",
                    stateName(nativeUsb_.state()), observedUnavailableAfterReady_ ? "yes" : "no");
        return;
    }
    if (std::strcmp(command, "key") == 0) {
        result = nativeUsb_.send(connectivity::HidKeyboardReport{0x02, {0x04}});
    } else if (std::strcmp(command, "six") == 0) {
        result = nativeUsb_.send(
            connectivity::HidKeyboardReport{0x00, {0x04, 0x05, 0x06, 0x07, 0x08, 0x09}});
    } else if (std::strcmp(command, "consumer") == 0) {
        result = nativeUsb_.send(connectivity::HidConsumerReport{0x00E9});
    } else if (std::strcmp(command, "release") == 0) {
        result = nativeUsb_.releaseAll();
    } else if (*command != '\0') {
        std::printf("[VALIDATION 016] unknown command\n");
        return;
    } else {
        return;
    }
    std::printf("[VALIDATION 016] send result=%u\n", static_cast<unsigned>(result));
}

void Plan016DeviceHarness::displayUsbState(connectivity::HidTransportState state) {
    std::array<char, 20> label{};
    (void)std::snprintf(label.data(), label.size(), "USB: %-11s", stateName(state));
    display_.drawText({8, 52}, label.data(), usbStateStyle);
    displayedState_ = state;
    displayedStateInitialized_ = true;
}

const char* Plan016DeviceHarness::stateName(connectivity::HidTransportState state) noexcept {
    switch (state) {
    case connectivity::HidTransportState::Unavailable:
        return "unavailable";
    case connectivity::HidTransportState::Starting:
        return "starting";
    case connectivity::HidTransportState::Ready:
        return "ready";
    case connectivity::HidTransportState::Busy:
        return "busy";
    case connectivity::HidTransportState::Error:
        return "error";
    }
    return "unknown";
}

} // namespace cardputer_hub::validation
