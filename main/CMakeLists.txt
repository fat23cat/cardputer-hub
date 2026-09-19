set(CARDPUTER_HUB_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/display/slide_transition.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/shell/home_graphics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/shell/ui_scheduler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/services/battery/battery_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/services/audio/audio_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_audio_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_battery_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/hosts/host_settings.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/network/wifi_settings.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/runtime/mini_app_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/launcher/launcher.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/launcher/launcher_graphics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/system/system_app.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/mac_control/mac_control_app.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/mac_control/mac_control_bindings.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/mac_control/mac_control_graphics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/apps/shell/application_shell.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/services/configuration/configuration_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/services/network/network_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/services/hosts/host_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/companion/companion_protocol.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/companion/companion_framer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/bluetooth/bluetooth_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/hid/hid_transport.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/wifi/wifi_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/services/companion/companion_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/services/host_control/host_control_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/actions/action.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/actions/action_bus.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/app_registry/app_registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/capabilities/capability_registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/input/keyboard_event_translator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/lifecycle/build_info.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/lifecycle/system_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/logging/logger.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/navigation/navigation_stack.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/power/display_power_controller.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/storage/files/file_storage.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/storage/storage.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_backlight_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_display_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_adv_keyboard_layout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_keyboard_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_platform.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/serial_log_sink.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/bluetooth/esp32_bluetooth_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/esp32_nvs_storage_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/wifi/esp32_wifi_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/storage/microsd/cardputer_microsd_file_storage_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/storage/microsd/microsd_io_error.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../components/m5cardputer/upstream/src/utility/Adafruit_TCA8418/Adafruit_TCA8418.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/main.cpp")

idf_component_register(
    SRCS ${CARDPUTER_HUB_SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../src"
        "${CMAKE_CURRENT_LIST_DIR}/../components/m5cardputer/upstream/src/utility/Adafruit_TCA8418"
    REQUIRES
        bt
        driver
        esp_event
        esp_hid
        esp_netif
        esp_wifi
        fatfs
        mbedtls
        m5stack__m5gfx
        m5stack__m5unified
        nvs_flash)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -Wall -Wextra -Werror)

set(CARDPUTER_HUB_VERSION_OVERRIDE "" CACHE STRING
    "Explicit firmware version supplied by the build entry point")
set(CARDPUTER_HUB_VERSION "${CARDPUTER_HUB_VERSION_OVERRIDE}")
if(CARDPUTER_HUB_VERSION STREQUAL "")
    set(CARDPUTER_HUB_VERSION "$ENV{CARDPUTER_HUB_VERSION}")
endif()
if(CARDPUTER_HUB_VERSION STREQUAL "")
    set(CARDPUTER_HUB_VERSION "0.1.0-dev")
endif()

set(CARDPUTER_HUB_COMMIT "$ENV{CARDPUTER_HUB_COMMIT}")
if(CARDPUTER_HUB_COMMIT STREQUAL "")
    execute_process(
        COMMAND git rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
        OUTPUT_VARIABLE CARDPUTER_HUB_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(CARDPUTER_HUB_COMMIT STREQUAL "")
    set(CARDPUTER_HUB_COMMIT "unknown")
endif()

set(CARDPUTER_HUB_BUILD_TYPE "$ENV{CARDPUTER_HUB_BUILD_TYPE}")
if(CARDPUTER_HUB_BUILD_TYPE STREQUAL "")
    set(CARDPUTER_HUB_BUILD_TYPE "cardputer-adv")
endif()

target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE
        CARDPUTER_HUB_VERSION="${CARDPUTER_HUB_VERSION}"
        CARDPUTER_HUB_COMMIT="${CARDPUTER_HUB_COMMIT}"
        CARDPUTER_HUB_BUILD_TYPE="${CARDPUTER_HUB_BUILD_TYPE}")
