# M5UnitUnified manages M5 unit components and their bus adapters. The pinned
# upstream revision is a Git submodule. Its googletest helpers pull in M5Unified
# for test scaffolding only and are not part of the firmware.
set(M5UNITUNIFIED_SOURCES
    "upstream/src/M5UnitComponent.cpp"
    "upstream/src/M5UnitUnified.cpp"
    "upstream/src/m5_unit_component/adapter.cpp"
    "upstream/src/m5_unit_component/adapter_gpio.cpp"
    "upstream/src/m5_unit_component/adapter_gpio_v1.cpp"
    "upstream/src/m5_unit_component/adapter_gpio_v2.cpp"
    "upstream/src/m5_unit_component/adapter_i2c.cpp"
    "upstream/src/m5_unit_component/adapter_spi.cpp"
    "upstream/src/m5_unit_component/adapter_uart.cpp"
    "upstream/src/m5_unit_component/pin.cpp"
)

idf_component_register(
    SRCS ${M5UNITUNIFIED_SOURCES}
    INCLUDE_DIRS "upstream/src"
    REQUIRES driver m5hal
    PRIV_REQUIRES esp_adc esp_timer m5utility)

# Linking M5Unified puts M5GFX's <utility/I2C_Class.hpp> on adapter_i2c.cpp's
# include path, so the real m5::I2C_Class adapter is compiled instead of a stub.
# Cardputer Hub owns the Grove port through M5.Ex_I2C.
idf_component_optional_requires(PRIVATE m5stack__m5unified)

# The libraries print identifiers (for example an NFC-B PUPI) at error level.
# Their logging is compiled out; card data reaches the console only through the
# opt-in NfcService diagnostics.
target_compile_definitions(${COMPONENT_LIB} PRIVATE M5_LOG_LEVEL=0)
