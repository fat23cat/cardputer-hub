cmake_minimum_required(VERSION 3.22)

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_definitions(CORE_DEBUG_LEVEL=3)
project(cardputer_hub)

# sdkconfig.defaults does not replace a cached console selection. Reject old
# composite-USB configurations instead of silently producing a console-less image.
if(NOT CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    message(FATAL_ERROR
            "BLE-only firmware requires USB Serial/JTAG diagnostics. Move the generated sdkconfig aside and reconfigure from sdkconfig.defaults; do not erase device NVS.")
endif()
