# M5Unit-NFC drives the ST25R3916 NFC front end of the M5Stack Unit NFC. The
# pinned upstream revision is a Git submodule. Only
# src/hardware/nfc/st25r3916_adapter.cpp may include its headers.
set(M5UNITNFC_SOURCES
    "upstream/src/nfc/a/mifare.cpp"
    "upstream/src/nfc/a/nfca.cpp"
    "upstream/src/nfc/apdu/apdu.cpp"
    "upstream/src/nfc/b/nfcb.cpp"
    "upstream/src/nfc/crypto/aes.cpp"
    "upstream/src/nfc/f/nfcf.cpp"
    "upstream/src/nfc/isoDEP/desfire_file_system.cpp"
    "upstream/src/nfc/isoDEP/file_system.cpp"
    "upstream/src/nfc/isoDEP/isoDEP.cpp"
    "upstream/src/nfc/isoDEP/nfc_b_file_system.cpp"
    "upstream/src/nfc/layer/a/emulation_layer_a.cpp"
    "upstream/src/nfc/layer/a/emulation_layer_a_ST25R3916.cpp"
    "upstream/src/nfc/layer/a/nfc_layer_a.cpp"
    "upstream/src/nfc/layer/a/nfc_layer_a_ST25R3916.cpp"
    "upstream/src/nfc/layer/b/nfc_layer_b.cpp"
    "upstream/src/nfc/layer/b/nfc_layer_b_ST25R3916.cpp"
    "upstream/src/nfc/layer/f/emulation_layer_f.cpp"
    "upstream/src/nfc/layer/f/emulation_layer_f_ST25R3916.cpp"
    "upstream/src/nfc/layer/f/nfc_layer_f.cpp"
    "upstream/src/nfc/layer/f/nfc_layer_f_ST25R3916.cpp"
    "upstream/src/nfc/layer/ndef_layer.cpp"
    "upstream/src/nfc/layer/v/nfc_layer_v.cpp"
    "upstream/src/nfc/layer/v/nfc_layer_v_ST25R3916.cpp"
    "upstream/src/nfc/ndef/ndef.cpp"
    "upstream/src/nfc/ndef/ndef_record.cpp"
    "upstream/src/nfc/ndef/ndef_tlv.cpp"
    "upstream/src/nfc/v/nfcv.cpp"
    "upstream/src/unit/unit_ST25R3916.cpp"
    "upstream/src/unit/unit_ST25R3916_nfca.cpp"
    "upstream/src/unit/unit_ST25R3916_nfcb.cpp"
    "upstream/src/unit/unit_ST25R3916_nfcf.cpp"
    "upstream/src/unit/unit_ST25R3916_nfcv.cpp"
    "upstream/src/unit/unit_ST25R3916_util.cpp"
)

idf_component_register(
    SRCS ${M5UNITNFC_SOURCES}
    INCLUDE_DIRS "upstream/src"
    REQUIRES m5unitunified m5utility
    PRIV_REQUIRES driver mbedtls)

# The libraries print identifiers (for example an NFC-B PUPI) at error level.
# Their logging is compiled out; card data reaches the console only through the
# opt-in NfcService diagnostics.
target_compile_definitions(${COMPONENT_LIB} PRIVATE M5_LOG_LEVEL=0)
