# M5HAL is the hardware abstraction used by M5UnitUnified. The pinned upstream
# revision is a Git submodule.
idf_component_register(
    SRCS "upstream/src/M5HAL.cpp"
    INCLUDE_DIRS "upstream/src"
    REQUIRES m5utility
    PRIV_REQUIRES esp_driver_gpio)

# The libraries print identifiers (for example an NFC-B PUPI) at error level.
# Their logging is compiled out; card data reaches the console only through the
# opt-in NfcService diagnostics.
target_compile_definitions(${COMPONENT_LIB} PRIVATE M5_LOG_LEVEL=0)
