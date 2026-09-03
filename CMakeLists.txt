cmake_minimum_required(VERSION 3.22)

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Preserve the Cardputer-Adv hardware USB CDC behavior from the former
# PlatformIO production environment.
add_compile_definitions(ARDUINO_USB_CDC_ON_BOOT=1 ARDUINO_USB_MODE=1 CORE_DEBUG_LEVEL=3)
project(cardputer_hub)
