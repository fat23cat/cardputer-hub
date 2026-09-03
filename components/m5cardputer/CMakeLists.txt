file(GLOB_RECURSE M5CARDPUTER_SOURCES "upstream/src/*.cpp")

idf_component_register(
    SRCS ${M5CARDPUTER_SOURCES}
    INCLUDE_DIRS "upstream/src"
    REQUIRES espressif__arduino-esp32 arduino_irremote m5stack__m5gfx m5stack__m5unified)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
