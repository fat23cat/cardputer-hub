# M5Utility is pure C++ utility code shared by the M5 unit libraries. The pinned
# upstream revision is a Git submodule; this wrapper only registers its sources
# so the ESP-IDF build sees one deterministic component. Sources are listed
# explicitly, like the other components, so a submodule update that adds a file
# is a visible change here.
set(M5UTILITY_SOURCES
    "upstream/src/m5_utility/base64.cpp"
    "upstream/src/m5_utility/button_status.cpp"
    "upstream/src/m5_utility/compatibility_feature.cpp"
    "upstream/src/m5_utility/container/circular_buffer.cpp"
    "upstream/src/m5_utility/crypto/des.cpp"
    "upstream/src/m5_utility/log/library_log.cpp"
    "upstream/src/m5_utility/murmurhash3.cpp"
    "upstream/src/m5_utility/string.cpp"
)

idf_component_register(
    SRCS ${M5UTILITY_SOURCES}
    INCLUDE_DIRS "upstream/src"
    PRIV_REQUIRES esp_timer)

# The libraries print identifiers (for example an NFC-B PUPI) at error level.
# Their logging is compiled out; card data reaches the console only through the
# opt-in NfcService diagnostics.
target_compile_definitions(${COMPONENT_LIB} PRIVATE M5_LOG_LEVEL=0)
