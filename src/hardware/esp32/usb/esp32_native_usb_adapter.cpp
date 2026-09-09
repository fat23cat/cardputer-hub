#include "hardware/esp32/usb/esp32_native_usb_adapter.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <variant>

#include "connectivity/usb/native_usb_descriptors.h"

#include "esp_err.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

namespace cardputer_hub::hardware {
namespace {

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
// cppcheck-suppress preprocessorErrorDirective
#error "TinyUSB and the fixed USB Serial/JTAG console cannot share the ESP32-S3 internal PHY"
#endif
#if !CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_CDC_COUNT != 1 ||                                \
    CONFIG_TINYUSB_HID_COUNT != 1 || !CONFIG_TINYUSB_SUSPEND_CALLBACK ||                           \
    !CONFIG_TINYUSB_RESUME_CALLBACK || CONFIG_TINYUSB_MIDI_COUNT != 0 ||                           \
    CONFIG_TINYUSB_VENDOR_COUNT != 0 || !CONFIG_TINYUSB_DFU_MODE_NONE ||                           \
    !CONFIG_TINYUSB_NET_MODE_NONE
// cppcheck-suppress preprocessorErrorDirective
#error "Cardputer Hub requires exactly one TinyUSB CDC and HID composite device"
#endif
#if CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_BTH_ENABLED
// cppcheck-suppress preprocessorErrorDirective
#error "Cardputer Hub native USB must not expose MSC or Bluetooth device classes"
#endif

Esp32NativeUsbAdapter* activeAdapter = nullptr;
tusb_desc_device_t installedDeviceDescriptor{};

void deviceEventCallback(tinyusb_event_t* event, void* argument) {
    auto* adapter = static_cast<Esp32NativeUsbAdapter*>(argument);
    if (adapter == nullptr || event == nullptr) {
        return;
    }
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        adapter->handleDeviceEvent(connectivity::NativeUsbEventType::Mounted);
        return;
    case TINYUSB_EVENT_DETACHED:
        adapter->handleDeviceEvent(connectivity::NativeUsbEventType::Unmounted);
        return;
    case TINYUSB_EVENT_SUSPENDED:
        adapter->handleDeviceEvent(connectivity::NativeUsbEventType::Suspended);
        return;
    case TINYUSB_EVENT_RESUMED:
        adapter->handleDeviceEvent(connectivity::NativeUsbEventType::Resumed);
        return;
    }
}

} // namespace

connectivity::NativeUsbAdapterResult Esp32NativeUsbAdapter::initialize(std::uint32_t lifecycle) {
    if (initialized_) {
        return connectivity::NativeUsbAdapterResult::Success;
    }
    if (activeAdapter != nullptr && activeAdapter != this) {
        return connectivity::NativeUsbAdapterResult::Error;
    }

    lifecycle_ = lifecycle;
    activeAdapter = this;
    auto config = TINYUSB_DEFAULT_CONFIG(deviceEventCallback, this);
    static_assert(sizeof(installedDeviceDescriptor) ==
                  connectivity::nativeUsbDeviceDescriptor.size());
    std::memcpy(&installedDeviceDescriptor, connectivity::nativeUsbDeviceDescriptor.data(),
                connectivity::nativeUsbDeviceDescriptor.size());
    config.descriptor.device = &installedDeviceDescriptor;
    config.descriptor.full_speed_config = connectivity::nativeUsbConfigurationDescriptor.data();
    config.descriptor.string = connectivity::nativeUsbStrings.data();
    config.descriptor.string_count = static_cast<int>(connectivity::nativeUsbStrings.size());
    // iSerialNumber = 0 is encoded in the project-owned device descriptor.
    if (tinyusb_driver_install(&config) != ESP_OK) {
        activeAdapter = nullptr;
        return connectivity::NativeUsbAdapterResult::Error;
    }

    tinyusb_config_cdcacm_t cdcConfig{};
    cdcConfig.cdc_port = TINYUSB_CDC_ACM_0;
    if (tinyusb_cdcacm_init(&cdcConfig) != ESP_OK) {
        (void)tinyusb_driver_uninstall();
        activeAdapter = nullptr;
        return connectivity::NativeUsbAdapterResult::Error;
    }
    if (tinyusb_console_init(TINYUSB_CDC_ACM_0) != ESP_OK) {
        (void)tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
        (void)tinyusb_driver_uninstall();
        activeAdapter = nullptr;
        return connectivity::NativeUsbAdapterResult::Error;
    }

    initialized_ = true;
    return connectivity::NativeUsbAdapterResult::Success;
}

connectivity::NativeUsbPollResult Esp32NativeUsbAdapter::pollEvent() {
    const auto queued = events_.pop();
    if (queued.status != connectivity::NativeUsbPollStatus::NoEvent) {
        return queued;
    }
    const bool ready = initialized_ && tud_mounted() && !tud_suspended() && tud_hid_ready();
    if (ready != lastEndpointReady_) {
        lastEndpointReady_ = ready;
        return {connectivity::NativeUsbPollStatus::Event,
                {connectivity::NativeUsbEventType::EndpointReadinessChanged, lifecycle_, ready}};
    }
    return {};
}

connectivity::NativeUsbAdapterResult
Esp32NativeUsbAdapter::sendHidReport(const connectivity::HidReport& report) {
    if (!initialized_ || !tud_mounted() || tud_suspended()) {
        return connectivity::NativeUsbAdapterResult::Unavailable;
    }
    if (!tud_hid_ready()) {
        return connectivity::NativeUsbAdapterResult::Busy;
    }

    if (const auto* keyboard = std::get_if<connectivity::HidKeyboardReport>(&report)) {
        std::array<std::uint8_t, 8> payload{};
        payload[0] = keyboard->modifiers;
        for (std::size_t index = 0; index < keyboard->usages.size(); ++index) {
            payload[index + 2] = keyboard->usages[index];
        }
        return tud_hid_report(connectivity::keyboardHidReportId, payload.data(), payload.size())
                   ? connectivity::NativeUsbAdapterResult::Success
                   : connectivity::NativeUsbAdapterResult::Busy;
    }

    const auto usage = std::get<connectivity::HidConsumerReport>(report).usage;
    const std::array<std::uint8_t, 2> payload{static_cast<std::uint8_t>(usage & 0xFFU),
                                              static_cast<std::uint8_t>(usage >> 8U)};
    return tud_hid_report(connectivity::consumerHidReportId, payload.data(), payload.size())
               ? connectivity::NativeUsbAdapterResult::Success
               : connectivity::NativeUsbAdapterResult::Busy;
}

void Esp32NativeUsbAdapter::handleDeviceEvent(connectivity::NativeUsbEventType type) noexcept {
    (void)events_.push({type, lifecycle_, false});
}

} // namespace cardputer_hub::hardware

extern "C" const std::uint8_t* tud_hid_descriptor_report_cb(std::uint8_t instance) {
    (void)instance;
    return cardputer_hub::connectivity::nativeUsbHidReportDescriptor.data();
}

extern "C" std::uint16_t tud_hid_get_report_cb(std::uint8_t instance, std::uint8_t reportId,
                                               hid_report_type_t reportType, std::uint8_t* buffer,
                                               std::uint16_t requestedLength) {
    (void)instance;
    (void)reportId;
    (void)reportType;
    (void)buffer;
    (void)requestedLength;
    return 0;
}

extern "C" void tud_hid_set_report_cb(std::uint8_t instance, std::uint8_t reportId,
                                      hid_report_type_t reportType, const std::uint8_t* buffer,
                                      std::uint16_t bufferSize) {
    (void)instance;
    (void)reportId;
    (void)reportType;
    (void)buffer;
    (void)bufferSize;
}
