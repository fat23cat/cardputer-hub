#include "core/lifecycle/build_info.h"
#include "core/lifecycle/system_runtime.h"
#include "core/logging/logger.h"
#include "hardware/cardputer/cardputer_display_adapter.h"
#include "hardware/cardputer/cardputer_keyboard_adapter.h"
#include "hardware/cardputer/cardputer_platform.h"
#include "hardware/cardputer/serial_log_sink.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CARDPUTER_HUB_PLAN_012_VALIDATION
#include <fcntl.h>
#include <unistd.h>

#include "validation/plan_012_device_harness.h"
#elif CARDPUTER_HUB_PLAN_014_VALIDATION
#include <fcntl.h>
#include <unistd.h>

#include "validation/plan_014_device_harness.h"
#endif

namespace {

cardputer_hub::hardware::CardputerPlatform platform;
cardputer_hub::hardware::CardputerKeyboardAdapter keyboard;
cardputer_hub::hardware::CardputerDisplayAdapter display;
cardputer_hub::hardware::SerialLogSink logSink;
cardputer_hub::core::Logger logger(logSink, cardputer_hub::core::LogLevel::Info);
cardputer_hub::core::SystemRuntime runtime(platform, keyboard, display, logger,
                                           cardputer_hub::core::firmwareBuildInfo());
#if CARDPUTER_HUB_PLAN_012_VALIDATION
cardputer_hub::validation::Plan012DeviceHarness validationHarness(logger);
#elif CARDPUTER_HUB_PLAN_014_VALIDATION
cardputer_hub::validation::Plan014DeviceHarness validationHarness(platform, keyboard, display,
                                                                  logger);
#endif

} // namespace

extern "C" void app_main(void) {
#if CARDPUTER_HUB_PLAN_012_VALIDATION
    (void)fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    validationHarness.start();
#elif CARDPUTER_HUB_PLAN_014_VALIDATION
    (void)fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    validationHarness.start();
#else
    runtime.start();
#endif

    for (;;) {
#if CARDPUTER_HUB_PLAN_012_VALIDATION
        validationHarness.update();
#elif CARDPUTER_HUB_PLAN_014_VALIDATION
        validationHarness.update();
#else
        (void)runtime.update();
#endif
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
