#include "apps/hosts/host_settings.h"
#include "apps/shell/application_shell.h"
#include "esp_timer.h"
#include "hardware/cardputer/cardputer_battery_adapter.h"
#include "hardware/esp32/bluetooth/esp32_bluetooth_adapter.h"
#include "hardware/esp32/esp32_nvs_storage_adapter.h"
#include "services/battery/battery_service.h"
#include "services/hosts/host_service.h"

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
#include "validation/plan_014_device_harness.h"
#elif CARDPUTER_HUB_PLAN_015_VALIDATION
#include "validation/plan_015_device_harness.h"
#endif

namespace {

cardputer_hub::hardware::CardputerPlatform platform;
cardputer_hub::hardware::CardputerKeyboardAdapter keyboard;
cardputer_hub::hardware::CardputerDisplayAdapter display;
cardputer_hub::hardware::SerialLogSink logSink;
cardputer_hub::core::Logger logger(logSink, cardputer_hub::core::LogLevel::Info);
cardputer_hub::core::SystemRuntime runtime(platform, keyboard, display, logger,
                                           cardputer_hub::core::firmwareBuildInfo());
#if !CARDPUTER_HUB_PLAN_012_VALIDATION && !CARDPUTER_HUB_PLAN_014_VALIDATION &&                    \
    !CARDPUTER_HUB_PLAN_015_VALIDATION
cardputer_hub::hardware::Esp32NvsStorageAdapter configurationAdapter;
cardputer_hub::core::Storage configurationStorage(configurationAdapter);
cardputer_hub::services::ConfigurationService configuration(configurationStorage);
cardputer_hub::hardware::Esp32BluetoothAdapter bluetoothAdapter;
cardputer_hub::connectivity::BluetoothService bluetooth(bluetoothAdapter, logger);
cardputer_hub::services::HostService hosts(bluetooth, configuration, &logger);
cardputer_hub::hardware::CardputerBatteryAdapter batteryAdapter;
cardputer_hub::services::BatteryService battery(batteryAdapter);
cardputer_hub::core::ActionBus actions;
cardputer_hub::apps::HostSettings hostSettings(hosts, actions, display);
cardputer_hub::apps::ApplicationShell applicationShell(hosts, actions, display, hostSettings);
std::int64_t previousHostUpdateMilliseconds = 0;
bool homeVisible = false;
#endif
#if CARDPUTER_HUB_PLAN_012_VALIDATION
cardputer_hub::validation::Plan012DeviceHarness validationHarness(logger);
#elif CARDPUTER_HUB_PLAN_014_VALIDATION
cardputer_hub::validation::Plan014DeviceHarness validationHarness(platform, keyboard, display,
                                                                  logger);
#elif CARDPUTER_HUB_PLAN_015_VALIDATION
cardputer_hub::validation::Plan015DeviceHarness validationHarness(platform, keyboard, display,
                                                                  logger);
#endif

} // namespace

extern "C" void app_main(void) {
#if CARDPUTER_HUB_PLAN_012_VALIDATION
    (void)fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    validationHarness.start();
#elif CARDPUTER_HUB_PLAN_014_VALIDATION || CARDPUTER_HUB_PLAN_015_VALIDATION
    validationHarness.start();
#else
    runtime.start();
    for (const auto* id : {"host.select", "host.bluetooth", "host.rename", "host.platform",
                           "host.capability", "host.mapping-template", "host.pair",
                           "host.cancel-pairing", "host.pair-response", "host.delete"}) {
        (void)actions.registerHandler(id, hosts);
    }
    (void)hosts.start();
    battery.update(std::chrono::milliseconds(0));
    previousHostUpdateMilliseconds = esp_timer_get_time() / 1000;
#endif

    for (;;) {
#if CARDPUTER_HUB_PLAN_012_VALIDATION
        validationHarness.update();
#elif CARDPUTER_HUB_PLAN_014_VALIDATION || CARDPUTER_HUB_PLAN_015_VALIDATION
        validationHarness.update();
#else
        const auto now = esp_timer_get_time() / 1000;
        const auto elapsed = std::chrono::milliseconds(now - previousHostUpdateMilliseconds);
        previousHostUpdateMilliseconds = now;
        const auto& input = runtime.update(elapsed);
        hosts.update(elapsed);
        battery.update(elapsed);
        if (!homeVisible) {
            if (runtime.splashFinished()) {
                // Consume any key sampled on the frame that dismisses the splash.
                applicationShell.update({}, std::chrono::milliseconds(0), battery.percent());
                homeVisible = true;
            }
        } else {
            applicationShell.update(input, elapsed, battery.percent());
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
