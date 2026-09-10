cmake_minimum_required(VERSION 3.22)

option(CARDPUTER_HUB_PLAN_012_VALIDATION
       "Build the local serial validation harness for plan 012" OFF)
option(CARDPUTER_HUB_PLAN_014_VALIDATION
       "Build the local pairing validation harness for plan 014" OFF)
option(CARDPUTER_HUB_PLAN_015_VALIDATION
       "Build the local BLE HID validation harness for plan 015" OFF)
option(CARDPUTER_HUB_PLAN_016_VALIDATION
       "Build the local native USB HID validation harness for plan 016" OFF)
option(CARDPUTER_HUB_PLAN_017_VALIDATION
       "Build the local HID routing validation harness for plan 017" OFF)

set(CARDPUTER_HUB_VALIDATION_HARNESS_COUNT 0)
foreach(CARDPUTER_HUB_VALIDATION_OPTION
        CARDPUTER_HUB_PLAN_012_VALIDATION
        CARDPUTER_HUB_PLAN_014_VALIDATION
        CARDPUTER_HUB_PLAN_015_VALIDATION
        CARDPUTER_HUB_PLAN_016_VALIDATION
        CARDPUTER_HUB_PLAN_017_VALIDATION)
    if(${CARDPUTER_HUB_VALIDATION_OPTION})
        math(EXPR CARDPUTER_HUB_VALIDATION_HARNESS_COUNT
             "${CARDPUTER_HUB_VALIDATION_HARNESS_COUNT}+1")
    endif()
endforeach()
if(CARDPUTER_HUB_VALIDATION_HARNESS_COUNT GREATER 1)
    message(FATAL_ERROR "Only one validation harness may be enabled")
endif()

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_definitions(CORE_DEBUG_LEVEL=3)
project(cardputer_hub)
