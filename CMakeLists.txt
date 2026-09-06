cmake_minimum_required(VERSION 3.22)

option(CARDPUTER_HUB_PLAN_012_VALIDATION
       "Build the local serial validation harness for plan 012" OFF)
option(CARDPUTER_HUB_PLAN_014_VALIDATION
       "Build the local pairing validation harness for plan 014" OFF)

if(CARDPUTER_HUB_PLAN_012_VALIDATION AND CARDPUTER_HUB_PLAN_014_VALIDATION)
    message(FATAL_ERROR "Only one validation harness may be enabled")
endif()

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_definitions(CORE_DEBUG_LEVEL=3)
project(cardputer_hub)
