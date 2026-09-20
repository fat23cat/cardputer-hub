#include "apps/hosts/host_settings.h"
#include "apps/mac_control/mac_control_app.h"
#include "apps/network/wifi_settings.h"
#include "apps/pomodoro/pomodoro_app.h"
#include "apps/runtime/mini_app_runtime.h"
#include "apps/shell/application_shell.h"
#include "apps/shell/ui_scheduler.h"
#include "apps/system/system_app.h"
#include "core/app_registry/app_registry.h"
#include "core/capabilities/capability_registry.h"
#include "esp_timer.h"
#include "hardware/cardputer/cardputer_audio_adapter.h"
#include "hardware/cardputer/cardputer_battery_adapter.h"
#include "hardware/cardputer/cardputer_puzzle_ws2812_adapter.h"
#include "hardware/esp32/bluetooth/esp32_bluetooth_adapter.h"
#include "hardware/esp32/esp32_nvs_storage_adapter.h"
#include "hardware/esp32/wifi/esp32_wifi_adapter.h"
#include "services/audio/audio_service.h"
#include "services/battery/battery_service.h"
#include "services/companion/companion_service.h"
#include "services/host_control/host_control_service.h"
#include "services/hosts/host_service.h"
#include "services/indicator/indicator_service.h"
#include "services/network/network_service.h"
#include "services/pomodoro/pomodoro_led_controller.h"
#include "services/pomodoro/pomodoro_service.h"

#include "core/lifecycle/build_info.h"
#include "core/lifecycle/system_runtime.h"
#include "core/logging/logger.h"
#include "core/power/display_power_controller.h"
#include "hardware/cardputer/cardputer_backlight_adapter.h"
#include "hardware/cardputer/cardputer_display_adapter.h"
#include "hardware/cardputer/cardputer_keyboard_adapter.h"
#include "hardware/cardputer/cardputer_platform.h"
#include "hardware/cardputer/serial_log_sink.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

cardputer_hub::hardware::CardputerPlatform platform;
cardputer_hub::hardware::CardputerKeyboardAdapter keyboard;
cardputer_hub::hardware::CardputerDisplayAdapter display;
cardputer_hub::hardware::CardputerAudioAdapter audioAdapter;
cardputer_hub::hardware::SerialLogSink logSink;
cardputer_hub::core::Logger logger(logSink, cardputer_hub::core::LogLevel::Info);
cardputer_hub::hardware::CardputerBacklightAdapter backlight;
cardputer_hub::core::DisplayPowerController displayPower(backlight);
cardputer_hub::core::SystemRuntime runtime(platform, keyboard, display, displayPower, logger,
                                           cardputer_hub::core::firmwareBuildInfo());
cardputer_hub::hardware::Esp32NvsStorageAdapter configurationAdapter;
cardputer_hub::core::Storage configurationStorage(configurationAdapter);
cardputer_hub::services::ConfigurationService configuration(configurationStorage);
cardputer_hub::services::AudioService audio(configuration, audioAdapter);
cardputer_hub::hardware::Esp32WifiAdapter wifiAdapter;
cardputer_hub::connectivity::WiFiService wifiConnectivity(wifiAdapter, logger);
cardputer_hub::services::NetworkService network(wifiConnectivity, configuration, &logger);
cardputer_hub::hardware::Esp32BluetoothAdapter bluetoothAdapter;
cardputer_hub::connectivity::BluetoothService bluetooth(bluetoothAdapter, logger);
cardputer_hub::services::HostService hosts(bluetooth, configuration, &logger);
cardputer_hub::hardware::CardputerBatteryAdapter batteryAdapter;
cardputer_hub::services::BatteryService battery(batteryAdapter);
cardputer_hub::core::ActionBus actions;
cardputer_hub::core::AppRegistry appRegistry;
cardputer_hub::core::CapabilityRegistry capabilities;
cardputer_hub::apps::MiniAppRuntime miniApps(appRegistry, capabilities);
cardputer_hub::apps::SystemApp systemApp(battery, hosts, network, display);
cardputer_hub::apps::HostSettings hostSettings(hosts, actions, display);
cardputer_hub::apps::WiFiSettings wifiSettings(network, actions, display);
cardputer_hub::apps::ApplicationShell applicationShell(hosts, network, actions, display,
                                                       hostSettings, wifiSettings, audio, miniApps,
                                                       capabilities);
cardputer_hub::services::CompanionService companion(bluetooth.companionTransport(), capabilities,
                                                    &logger);
cardputer_hub::services::HostControlService hostControl(hosts, companion, capabilities);
cardputer_hub::apps::MacControlApp macControl(actions, hostControl, display);
cardputer_hub::hardware::EspPuzzleLedBackend puzzleLedBackend;
cardputer_hub::hardware::PuzzleWs2812Adapter puzzleLeds(puzzleLedBackend);
cardputer_hub::services::IndicatorService indicator(puzzleLeds);
cardputer_hub::services::PomodoroService pomodoro;
cardputer_hub::services::PomodoroLedController pomodoroLed(pomodoro, indicator, &audio,
                                                           &displayPower);
cardputer_hub::apps::PomodoroApp pomodoroApp(pomodoro, display);
cardputer_hub::apps::UiScheduler uiScheduler;
std::int64_t previousUpdateMilliseconds = 0;
bool homeVisible = false;

} // namespace

extern "C" void app_main(void) {
    runtime.start();
    (void)configuration.ensureLoaded();
    (void)puzzleLeds.begin();
    for (const auto* id : {"host.select", "host.bluetooth", "host.rename", "host.platform",
                           "host.capability", "host.mapping-template", "host.pair",
                           "host.cancel-pairing", "host.pair-response", "host.delete"}) {
        (void)actions.registerHandler(id, hosts);
    }
    (void)actions.registerHandler("audio.volume.step", audio);
    for (const auto* id : {"network.set-enabled", "network.configure", "network.forget"})
        (void)actions.registerHandler(id, network);
    (void)actions.registerHandler(cardputer_hub::services::hostAppActivateActionId, hostControl);
    (void)hosts.start();
    (void)network.start();
    (void)audio.start();
    (void)appRegistry.registerApp({"system", "SYSTEM", "system", "system/home", {}});
    (void)miniApps.registerInstance("system", systemApp);
    (void)appRegistry.registerApp({"mac-control",
                                   "MAC CONTROL",
                                   "mac-control",
                                   "mac-control",
                                   {cardputer_hub::connectivity::companionCapabilityId}});
    (void)miniApps.registerInstance("mac-control", macControl);
    (void)appRegistry.registerApp({"pomodoro", "POMODORO", "pomodoro", "pomodoro", {}});
    (void)miniApps.registerInstance("pomodoro", pomodoroApp);
    battery.update(std::chrono::milliseconds(0));
    previousUpdateMilliseconds = esp_timer_get_time() / 1000;

    for (;;) {
        const auto now = esp_timer_get_time() / 1000;
        const auto elapsed = std::chrono::milliseconds(now - previousUpdateMilliseconds);
        previousUpdateMilliseconds = now;
        const auto& input = runtime.update(elapsed);
        hosts.update(elapsed);
        companion.update(elapsed);
        hostControl.update();
        network.update(elapsed);
        battery.update(elapsed);
        pomodoro.update(elapsed);
        pomodoroLed.update(elapsed);
        indicator.update();
        if (!homeVisible) {
            if (runtime.splashFinished()) {
                // Consume any key sampled on the frame that dismisses the splash.
                applicationShell.update({}, std::chrono::milliseconds(0), battery.percent());
                homeVisible = true;
            }
        } else {
            const auto uiElapsed = uiScheduler.elapsedForUpdate(elapsed, !input.empty());
            if (uiElapsed)
                applicationShell.update(input, *uiElapsed, battery.percent(), runtime.displayOff());
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
