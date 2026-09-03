file(GLOB_RECURSE CARDPUTER_HUB_SOURCES
     "${CMAKE_CURRENT_LIST_DIR}/../src/*.cpp")

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
