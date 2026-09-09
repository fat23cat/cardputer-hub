set(CARDPUTER_HUB_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/bluetooth/bluetooth_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/hid/hid_transport.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/usb/native_usb_descriptors.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/usb/native_usb_hid.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/wifi/wifi_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/actions/action.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/actions/action_bus.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/app_registry/app_registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/capabilities/capability_registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/input/keyboard_event_translator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/lifecycle/build_info.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/lifecycle/system_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/logging/logger.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/navigation/navigation_stack.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/storage/files/file_storage.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/core/storage/storage.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_display_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_adv_keyboard_layout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_keyboard_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_platform.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/serial_log_sink.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/bluetooth/esp32_bluetooth_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/esp32_nvs_storage_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/usb/esp32_native_usb_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/wifi/esp32_wifi_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/storage/microsd/cardputer_microsd_file_storage_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/storage/microsd/microsd_io_error.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../components/m5cardputer/upstream/src/utility/Adafruit_TCA8418/Adafruit_TCA8418.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/main.cpp")

if(CARDPUTER_HUB_PLAN_012_VALIDATION)
    list(APPEND CARDPUTER_HUB_SOURCES
         "${CMAKE_CURRENT_LIST_DIR}/../src/validation/plan_012_device_harness.cpp")
elseif(CARDPUTER_HUB_PLAN_014_VALIDATION)
    list(APPEND CARDPUTER_HUB_SOURCES
         "${CMAKE_CURRENT_LIST_DIR}/../src/validation/plan_014_device_harness.cpp")
elseif(CARDPUTER_HUB_PLAN_015_VALIDATION)
    list(APPEND CARDPUTER_HUB_SOURCES
         "${CMAKE_CURRENT_LIST_DIR}/../src/validation/plan_014_device_harness.cpp")
elseif(CARDPUTER_HUB_PLAN_016_VALIDATION)
    list(APPEND CARDPUTER_HUB_SOURCES
         "${CMAKE_CURRENT_LIST_DIR}/../src/validation/plan_016_device_harness.cpp")
endif()

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
        espressif__esp_tinyusb
        fatfs
        mbedtls
        m5stack__m5gfx
        m5stack__m5unified
        nvs_flash)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -Wall -Wextra -Werror)
if(CARDPUTER_HUB_PLAN_012_VALIDATION)
    target_compile_definitions(${COMPONENT_LIB}
                               PRIVATE CARDPUTER_HUB_PLAN_012_VALIDATION=1)
elseif(CARDPUTER_HUB_PLAN_014_VALIDATION)
    target_compile_definitions(${COMPONENT_LIB}
                               PRIVATE CARDPUTER_HUB_PLAN_014_VALIDATION=1)
elseif(CARDPUTER_HUB_PLAN_015_VALIDATION)
    target_compile_definitions(${COMPONENT_LIB}
                               PRIVATE CARDPUTER_HUB_PLAN_015_VALIDATION=1)
elseif(CARDPUTER_HUB_PLAN_016_VALIDATION)
    target_compile_definitions(${COMPONENT_LIB}
                               PRIVATE CARDPUTER_HUB_PLAN_016_VALIDATION=1)
endif()

set(CARDPUTER_HUB_VERSION "$ENV{CARDPUTER_HUB_VERSION}")
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
