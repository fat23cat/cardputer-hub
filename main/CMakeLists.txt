set(CARDPUTER_HUB_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/connectivity/bluetooth/bluetooth_service.cpp"
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
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_keyboard_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/cardputer_platform.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/cardputer/serial_log_sink.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/bluetooth/esp32_bluetooth_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/esp32_nvs_storage_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/esp32/wifi/esp32_wifi_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/hardware/storage/microsd/cardputer_microsd_file_storage_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/main.cpp")

idf_component_register(
    SRCS ${CARDPUTER_HUB_SOURCES}
    INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../src"
    REQUIRES
        espressif__arduino-esp32
        arduino_irremote
        bt
        esp_event
        esp_netif
        esp_wifi
        fatfs
        m5cardputer
        m5stack__m5gfx
        m5stack__m5unified
        nvs_flash)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -Wall -Wextra -Werror)

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
