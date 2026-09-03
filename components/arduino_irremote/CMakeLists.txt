# IRremote 4.x is header-only. Keeping it as a component makes its immutable
# upstream revision available to Arduino libraries without a global install.
idf_component_register(INCLUDE_DIRS "upstream/src" REQUIRES espressif__arduino-esp32)
