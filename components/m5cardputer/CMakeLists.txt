set(M5CARDPUTER_SOURCES
    "upstream/src/M5Cardputer.cpp"
    "upstream/src/utility/Adafruit_TCA8418/Adafruit_TCA8418.cpp"
    "upstream/src/utility/Keyboard/Keyboard.cpp"
    "upstream/src/utility/Keyboard/KeyboardReader/IOMatrix.cpp"
    "upstream/src/utility/Keyboard/KeyboardReader/TCA8418.cpp")

idf_component_register(
    SRCS ${M5CARDPUTER_SOURCES}
    INCLUDE_DIRS "upstream/src"
    REQUIRES espressif__arduino-esp32 arduino_irremote m5stack__m5gfx m5stack__m5unified)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
