cmake_minimum_required(VERSION 3.22)

option(CARDPUTER_HUB_PLAN_012_VALIDATION
       "Build the local serial validation harness for plan 012" OFF)

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_definitions(CORE_DEBUG_LEVEL=3)
project(cardputer_hub)
