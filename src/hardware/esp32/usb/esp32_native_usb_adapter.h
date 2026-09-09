#pragma once

#include <cstdint>

#include "connectivity/usb/native_usb_hid.h"

namespace cardputer_hub::hardware {

class Esp32NativeUsbAdapter final : public connectivity::INativeUsbAdapter {
  public:
    Esp32NativeUsbAdapter() noexcept = default;

    connectivity::NativeUsbAdapterResult initialize(std::uint32_t lifecycle) override;
    connectivity::NativeUsbPollResult pollEvent() override;
    connectivity::NativeUsbAdapterResult
    sendHidReport(const connectivity::HidReport& report) override;

    void handleDeviceEvent(connectivity::NativeUsbEventType type) noexcept;

  private:
    connectivity::NativeUsbEventQueue events_;
    std::uint32_t lifecycle_ = 0;
    bool initialized_ = false;
    bool lastEndpointReady_ = false;
};

} // namespace cardputer_hub::hardware
