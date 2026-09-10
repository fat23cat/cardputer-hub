#include "core/lifecycle/build_info.h"
#include "core/lifecycle/system_runtime.h"
#include "core/logging/logger.h"
#include "hardware/cardputer/cardputer_display_adapter.h"
#include "hardware/cardputer/cardputer_keyboard_adapter.h"
#include "hardware/cardputer/cardputer_platform.h"
#include "hardware/cardputer/serial_log_sink.h"
#include "hardware/esp32/usb/esp32_native_usb_adapter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CARDPUTER_HUB_PLAN_012_VALIDATION
#include <fcntl.h>
#include <unistd.h>

#include "validation/plan_012_device_harness.h"
#elif CARDPUTER_HUB_PLAN_014_VALIDATION
#include "validation/plan_014_device_harness.h"
#elif CARDPUTER_HUB_PLAN_015_VALIDATION
#include "validation/plan_015_device_harness.h"
#elif CARDPUTER_HUB_PLAN_016_VALIDATION
#include "validation/plan_016_device_harness.h"
#elif CARDPUTER_HUB_PLAN_017_VALIDATION
#include "validation/plan_017_device_harness.h"
#endif

namespace {

cardputer_hub::hardware::CardputerPlatform platform;
cardputer_hub::hardware::CardputerKeyboardAdapter keyboard;
cardputer_hub::hardware::CardputerDisplayAdapter display;
cardputer_hub::hardware::SerialLogSink logSink;
cardputer_hub::hardware::Esp32NativeUsbAdapter nativeUsbAdapter;
cardputer_hub::connectivity::NativeUsbHidService nativeUsb(nativeUsbAdapter);
cardputer_hub::core::Logger logger(logSink, cardputer_hub::core::LogLevel::Info);
cardputer_hub::core::SystemRuntime runtime(platform, keyboard, display, logger,
                                           cardputer_hub::core::firmwareBuildInfo());
#if CARDPUTER_HUB_PLAN_012_VALIDATION
cardputer_hub::validation::Plan012DeviceHarness validationHarness(logger);
#elif CARDPUTER_HUB_PLAN_014_VALIDATION
cardputer_hub::validation::Plan014DeviceHarness validationHarness(platform, keyboard, display,
                                                                  logger);
#elif CARDPUTER_HUB_PLAN_015_VALIDATION
cardputer_hub::validation::Plan015DeviceHarness validationHarness(platform, keyboard, display,
                                                                  logger);
#elif CARDPUTER_HUB_PLAN_016_VALIDATION
cardputer_hub::validation::Plan016DeviceHarness validationHarness(runtime, nativeUsb, display);
#elif CARDPUTER_HUB_PLAN_017_VALIDATION
cardputer_hub::validation::Plan017DeviceHarness validationHarness(platform, keyboard, display,
                                                                  logger, &nativeUsb);
#endif

} // namespace

extern "C" void app_main(void) {
    // USB failure is isolated: local platform startup must still proceed.
    (void)nativeUsb.initialize();
#if CARDPUTER_HUB_PLAN_012_VALIDATION
    (void)fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    validationHarness.start();
#elif CARDPUTER_HUB_PLAN_014_VALIDATION || CARDPUTER_HUB_PLAN_015_VALIDATION ||                    \
    CARDPUTER_HUB_PLAN_016_VALIDATION || CARDPUTER_HUB_PLAN_017_VALIDATION
    validationHarness.start();
#else
    runtime.start();
#endif

    for (;;) {
        nativeUsb.update();
#if CARDPUTER_HUB_PLAN_012_VALIDATION
        validationHarness.update();
#elif CARDPUTER_HUB_PLAN_014_VALIDATION || CARDPUTER_HUB_PLAN_015_VALIDATION ||                    \
    CARDPUTER_HUB_PLAN_016_VALIDATION || CARDPUTER_HUB_PLAN_017_VALIDATION
        validationHarness.update();
#else
        (void)runtime.update();
#endif
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
