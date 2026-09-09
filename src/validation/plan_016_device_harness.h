#pragma once

#include <array>
#include <cstddef>

#include "connectivity/usb/native_usb_hid.h"
#include "core/lifecycle/system_runtime.h"

namespace cardputer_hub::validation {

class Plan016DeviceHarness {
  public:
    Plan016DeviceHarness(core::SystemRuntime& runtime, connectivity::NativeUsbHidService& nativeUsb,
                         core::IDisplayAdapter& display) noexcept;

    void start();
    void update();

  private:
    void readConsole();
    void handleCommand(const char* command);
    void displayUsbState(connectivity::HidTransportState state);
    static const char* stateName(connectivity::HidTransportState state) noexcept;

    core::SystemRuntime& runtime_;
    connectivity::NativeUsbHidService& nativeUsb_;
    core::IDisplayAdapter& display_;
    std::array<char, 64> input_{};
    std::size_t inputLength_ = 0;
    bool inputOverflow_ = false;
    bool hasBeenReady_ = false;
    bool observedUnavailableAfterReady_ = false;
    connectivity::HidTransportState displayedState_ = connectivity::HidTransportState::Unavailable;
    bool displayedStateInitialized_ = false;
    bool started_ = false;
};

} // namespace cardputer_hub::validation
