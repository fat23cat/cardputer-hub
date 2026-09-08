import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class EspIdfBuildConfigurationTests(unittest.TestCase):
    def read(self, relative_path: str) -> str:
        return (ROOT / relative_path).read_text(encoding="utf-8")

    def test_production_dependencies_are_exact_and_idf_is_constrained(self) -> None:
        manifest = self.read("main/idf_component.yml")
        lock = self.read("dependencies.lock")

        self.assertIn('idf: "==5.5.5"', manifest)
        self.assertIn('m5stack/m5unified: "==0.2.21"', manifest)
        self.assertIn('m5stack/m5gfx: "==0.2.28"', manifest)
        self.assertNotIn("espressif/arduino-esp32", manifest)
        self.assertNotIn("espressif/arduino-esp32", lock)
        self.assertIn('version: 0.2.21', lock)
        self.assertIn('version: 0.2.28', lock)

    def test_platformio_is_only_a_native_test_runner(self) -> None:
        configuration = self.read("platformio.ini")

        self.assertIn("default_envs = native", configuration)
        self.assertIn("[env:native]", configuration)
        self.assertNotIn("[env:cardputer-adv]", configuration)

    def test_nimble_is_the_only_enabled_bluetooth_host(self) -> None:
        configuration = self.read("sdkconfig.defaults")

        expected_settings = (
            "CONFIG_BT_ENABLED=y",
            "CONFIG_BT_BLUEDROID_ENABLED=n",
            "CONFIG_BT_NIMBLE_ENABLED=y",
            "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y",
            "CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y",
            "CONFIG_BT_NIMBLE_ROLE_CENTRAL=n",
            "CONFIG_BT_NIMBLE_ROLE_OBSERVER=n",
            "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1",
            "CONFIG_FREERTOS_HZ=1000",
        )
        for setting in expected_settings:
            self.assertIn(setting, configuration)
        self.assertNotIn("CONFIG_ARDUINO_", configuration)

    def test_nimble_pairing_requires_authenticated_secure_connections(self) -> None:
        configuration = self.read("sdkconfig.defaults")

        expected_settings = (
            "CONFIG_BT_NIMBLE_SECURITY_ENABLE=y",
            "CONFIG_BT_NIMBLE_SM_LEGACY=n",
            "CONFIG_BT_NIMBLE_SM_SC=y",
            "CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS=n",
            "CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_ENCRYPTION=y",
            "CONFIG_BT_NIMBLE_SM_LVL=3",
            "CONFIG_BT_NIMBLE_SM_SC_ONLY=1",
            "CONFIG_BT_NIMBLE_MAX_BONDS=16",
            "CONFIG_BT_NIMBLE_NVS_PERSIST=y",
        )
        for setting in expected_settings:
            self.assertIn(setting, configuration)
        adapter = self.read("src/hardware/esp32/bluetooth/esp32_bluetooth_adapter.cpp")
        self.assertIn("ble_hs_cfg.sm_sec_lvl = CONFIG_BT_NIMBLE_SM_LVL;", adapter)
        self.assertNotIn("ble_hs_cfg.sm_sec_lvl = 4;", adapter)

    def test_production_build_uses_idf_output_and_partition_table(self) -> None:
        makefile = self.read("Makefile")
        configuration = self.read("sdkconfig.defaults")

        self.assertIn("idf.py", makefile)
        self.assertIn("cardputer_hub.bin", makefile)
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"', configuration)

    def test_fat_supports_the_managed_root_and_logical_path_lengths(self) -> None:
        configuration = self.read("sdkconfig.defaults")
        adapter = self.read(
            "src/hardware/storage/microsd/cardputer_microsd_file_storage_adapter.cpp"
        )

        self.assertIn("CONFIG_FATFS_LFN_HEAP=y", configuration)
        self.assertIn("CONFIG_FATFS_MAX_LFN=255", configuration)
        self.assertNotIn("CONFIG_FATFS_LFN_NONE=y", configuration)
        self.assertIn("constexpr int microSdFrequencyKhz = 10'000;", adapter)

    def test_project_code_retains_cxx17_and_warnings_as_errors(self) -> None:
        component = self.read("main/CMakeLists.txt")

        self.assertIn("cxx_std_17", component)
        self.assertIn("-std=gnu++17 -Wall -Wextra -Werror", component)

    def test_production_initializes_m5_without_arduino_runtime(self) -> None:
        project = self.read("CMakeLists.txt")
        defaults = self.read("sdkconfig.defaults")
        entrypoint = self.read("src/main.cpp")
        platform = self.read("src/hardware/cardputer/cardputer_platform.cpp")

        self.assertNotIn("M5GFX_BOARD=", project)
        self.assertIn("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y", defaults)
        self.assertIn('extern "C" void app_main(void)', entrypoint)
        self.assertNotIn("void setup()", entrypoint)
        self.assertNotIn("void loop()", entrypoint)
        self.assertIn("M5.begin(config);", platform)
        self.assertNotIn("initArduino();", platform)
        self.assertNotIn("Serial.begin", platform)
        self.assertNotIn("fallback_board", platform)
        self.assertNotIn("cardputerPowerStabilizationMs", platform)

    def test_production_hardware_graph_is_native_esp_idf(self) -> None:
        project = self.read("CMakeLists.txt")
        component = self.read("main/CMakeLists.txt")
        manifest = self.read("main/idf_component.yml")
        defaults = self.read("sdkconfig.defaults")
        platform = self.read("src/hardware/cardputer/cardputer_platform.cpp")
        keyboard = self.read("src/hardware/cardputer/cardputer_keyboard_adapter.cpp")
        display = self.read("src/hardware/cardputer/cardputer_display_adapter.cpp")
        serial = self.read("src/hardware/cardputer/serial_log_sink.cpp")
        microsd = self.read(
            "src/hardware/storage/microsd/cardputer_microsd_file_storage_adapter.cpp"
        )

        self.assertNotIn("espressif/arduino-esp32", manifest)
        self.assertNotIn("espressif__arduino-esp32", component)
        self.assertNotIn("EXTRA_COMPONENT_DIRS", project)
        self.assertNotIn("\n        m5cardputer\n", component)
        self.assertNotIn("arduino_irremote", component)
        self.assertIn("Adafruit_TCA8418.cpp", component)
        self.assertNotIn("ARDUINO_", project)
        self.assertNotIn("CONFIG_ARDUINO_", defaults)
        for source in (platform, keyboard, display, serial, microsd):
            self.assertNotIn("<Arduino.h>", source)
            self.assertNotIn("<M5Cardputer.h>", source)
        self.assertNotIn("<SD.h>", microsd)
        self.assertNotIn("<SPI.h>", microsd)

    def test_component_sources_are_explicitly_enumerated(self) -> None:
        application_component = self.read("main/CMakeLists.txt")
        cardputer_component = self.read("components/m5cardputer/CMakeLists.txt")

        self.assertNotIn("GLOB", application_component)
        self.assertNotIn("GLOB", cardputer_component)
        for source in (ROOT / "src").rglob("*.cpp"):
            self.assertIn(f"../{source.relative_to(ROOT).as_posix()}", application_component)
        cardputer_root = ROOT / "components" / "m5cardputer"
        for source in (cardputer_root / "upstream" / "src").rglob("*.cpp"):
            self.assertIn(source.relative_to(cardputer_root).as_posix(), cardputer_component)

    def test_plan_012_device_harness_is_opt_in(self) -> None:
        project = self.read("CMakeLists.txt")
        component = self.read("main/CMakeLists.txt")
        entrypoint = self.read("src/main.cpp")
        instructions = self.read("docs/validation/plan-012-device-harness.md")

        self.assertIn("option(CARDPUTER_HUB_PLAN_012_VALIDATION", project)
        self.assertIn('"Build the local serial validation harness for plan 012" OFF)', project)
        self.assertIn("if(CARDPUTER_HUB_PLAN_012_VALIDATION)", component)
        self.assertIn("validation/plan_012_device_harness.cpp", component)
        self.assertIn("CARDPUTER_HUB_PLAN_012_VALIDATION=1", component)
        self.assertIn("#if CARDPUTER_HUB_PLAN_012_VALIDATION", entrypoint)
        self.assertIn(
            "#if CARDPUTER_HUB_PLAN_012_VALIDATION\n"
            "    (void)fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);\n"
            "    validationHarness.start();\n"
            "#elif CARDPUTER_HUB_PLAN_014_VALIDATION\n"
            "    validationHarness.start();\n"
            "#else\n"
            "    runtime.start();\n"
            "#endif",
            entrypoint,
        )
        self.assertIn("-D CARDPUTER_HUB_PLAN_012_VALIDATION=ON", instructions)
        self.assertIn("build-validation-012", instructions)

    def test_plan_012_device_harness_yields_to_the_idle_task(self) -> None:
        entrypoint = self.read("src/main.cpp")

        self.assertIn(
            "#if CARDPUTER_HUB_PLAN_012_VALIDATION\n"
            "        validationHarness.update();\n"
            "#elif CARDPUTER_HUB_PLAN_014_VALIDATION\n"
            "        validationHarness.update();\n"
            "#else\n"
            "        (void)runtime.update();\n"
            "#endif\n"
            "        vTaskDelay(pdMS_TO_TICKS(1));",
            entrypoint,
        )

    def test_plan_014_device_harness_is_opt_in_and_non_shipping(self) -> None:
        project = self.read("CMakeLists.txt")
        component = self.read("main/CMakeLists.txt")
        entrypoint = self.read("src/main.cpp")
        instructions = self.read("docs/validation/plan-014-device-harness.md")

        self.assertIn("option(CARDPUTER_HUB_PLAN_014_VALIDATION", project)
        self.assertIn('"Build the local pairing validation harness for plan 014" OFF)', project)
        self.assertIn("CARDPUTER_HUB_PLAN_012_VALIDATION AND CARDPUTER_HUB_PLAN_014_VALIDATION", project)
        self.assertIn("if(CARDPUTER_HUB_PLAN_014_VALIDATION)", component)
        self.assertIn("validation/plan_014_device_harness.cpp", component)
        self.assertIn("CARDPUTER_HUB_PLAN_014_VALIDATION=1", component)
        self.assertIn("#elif CARDPUTER_HUB_PLAN_014_VALIDATION", entrypoint)
        self.assertIn("Plan014DeviceHarness validationHarness", entrypoint)
        self.assertIn("-D CARDPUTER_HUB_PLAN_014_VALIDATION=ON", instructions)
        self.assertIn("build-validation-014", instructions)
        self.assertIn("make upload UPLOAD_PORT=", instructions)

    def test_plan_014_pairing_values_stay_off_the_serial_console(self) -> None:
        harness = self.read("src/validation/plan_014_device_harness.cpp")

        self.assertIn("displayPairingChallenge", harness)
        self.assertIn("keyboard_.poll", harness)
        self.assertIn("usb_serial_jtag_driver_install", harness)
        self.assertIn("usb_serial_jtag_read_bytes", harness)
        self.assertNotIn('Serial.printf("%06', harness)
        self.assertNotIn("reference.bytes", harness)
        self.assertNotIn("challenge->value", harness)

    def test_ci_installs_exact_idf_and_packages_idf_outputs(self) -> None:
        installer = self.read("scripts/install_esp_idf.sh")

        self.assertIn('ESP_IDF_VERSION="5.5.5"', installer)
        self.assertIn('ESP_IDF_COMMIT="b774170ff46c393eeb5e495ea37936038d3f4f4f"', installer)
        for workflow_path in (
            ".github/workflows/ci.yml",
            ".github/workflows/release.yml",
        ):
            workflow = self.read(workflow_path)
            self.assertIn("scripts/install_esp_idf.sh", workflow)
            self.assertIn("build/cardputer_hub.bin", workflow)
            self.assertIn("build/partition_table/partition-table.bin", workflow)

    def test_ci_parallelizes_host_checks_and_firmware_build(self) -> None:
        workflow = self.read(".github/workflows/ci.yml")
        makefile = self.read("Makefile")

        self.assertIn("host-checks:", workflow)
        self.assertIn("firmware-build:", workflow)
        self.assertIn("make host-check", workflow)
        self.assertIn("make firmware-check", workflow)
        self.assertIn("needs.firmware-build.result", workflow)
        self.assertIn("needs.host-checks.result", workflow)
        self.assertIn("host-check: lock-check format-check lint test", makefile)
        self.assertIn("firmware-check: build", makefile)
        self.assertIn("check: host-check firmware-check", makefile)

    def test_ci_reuses_idf_components_and_compiler_outputs(self) -> None:
        workflow = self.read(".github/workflows/ci.yml")

        self.assertIn('${HOME}/.espressif/frameworks/esp-idf-v5.5.5', workflow)
        self.assertNotIn('${RUNNER_TEMP}/esp-idf-v5.5.5', workflow)
        self.assertIn("esp-idf-${{ runner.os }}-v5.5.5-b774170f", workflow)
        self.assertIn("managed_components", workflow)
        self.assertIn("${{ github.workspace }}/.ccache", workflow)
        self.assertIn('IDF_CCACHE_ENABLE: "1"', workflow)

    def test_release_rebuilds_without_repeating_host_checks(self) -> None:
        workflow = self.read(".github/workflows/release.yml")

        self.assertIn('${HOME}/.espressif/frameworks/esp-idf-v5.5.5', workflow)
        self.assertIn("make firmware-check", workflow)
        self.assertNotIn("make check", workflow)

    def test_reviewed_build_and_disconnect_settings_are_unambiguous(self) -> None:
        cmake = self.read("CMakeLists.txt")
        configuration = self.read("sdkconfig.defaults")
        adapter = self.read("src/hardware/esp32/bluetooth/esp32_bluetooth_adapter.cpp")

        self.assertIn("add_compile_definitions", cmake)
        self.assertNotIn("list(APPEND compile_definitions", cmake)
        self.assertIn("CONFIG_BT_NIMBLE_LOG_LEVEL_NONE=y", configuration)
        self.assertNotIn("CONFIG_BT_NIMBLE_LOG_LEVEL=", configuration)
        self.assertIn("result == BLE_HS_ENOTCONN", adapter)

    def test_esp32_adapter_uses_nimble_without_bluedroid(self) -> None:
        adapter = self.read("src/hardware/esp32/bluetooth/esp32_bluetooth_adapter.cpp")

        self.assertIn("nimble/nimble_port.h", adapter)
        self.assertIn("host/ble_gap.h", adapter)
        self.assertNotIn("esp_bt_main.h", adapter)
        self.assertNotIn("esp_bluedroid_", adapter)

    def test_controller_identity_log_is_suppressed_before_bluetooth_init(self) -> None:
        adapter = self.read("src/hardware/esp32/bluetooth/esp32_bluetooth_adapter.cpp")

        suppression = adapter.index("suppressIdentityBearingBluetoothLogTags();")
        controller_init = adapter.index("esp_bt_controller_init(&controllerConfig)")
        self.assertLess(suppression, controller_init)
        self.assertIn('"BLE_INIT"', adapter)

    def test_network_identity_logs_are_suppressed_before_wifi_init(self) -> None:
        adapter = self.read("src/hardware/esp32/wifi/esp32_wifi_adapter.cpp")

        self.assertIn(
            "Esp32WifiAdapter::initializeStation() {\n"
            "    suppressIdentityBearingWifiLogTags();\n"
            "    if (!initializeNetworkStack())",
            adapter,
        )
        self.assertIn('"wifi"', adapter)
        self.assertIn('"esp_netif_handlers"', adapter)


if __name__ == "__main__":
    unittest.main()
