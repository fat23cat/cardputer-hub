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
        self.assertIn('espressif/arduino-esp32: "==3.3.11"', manifest)
        self.assertIn('m5stack/m5unified: "==0.2.21"', manifest)
        self.assertIn('m5stack/m5gfx: "==0.2.28"', manifest)
        self.assertIn('version: 3.3.11', lock)
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
        self.assertIn("CONFIG_ARDUINO_SELECTIVE_BLE=n", configuration)
        self.assertIn("CONFIG_ARDUINO_SELECTIVE_BluetoothSerial=n", configuration)

    def test_production_build_uses_idf_output_and_partition_table(self) -> None:
        makefile = self.read("Makefile")
        configuration = self.read("sdkconfig.defaults")

        self.assertIn("idf.py", makefile)
        self.assertIn("cardputer_hub.bin", makefile)
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"', configuration)

    def test_project_code_retains_cxx17_and_warnings_as_errors(self) -> None:
        component = self.read("main/CMakeLists.txt")

        self.assertIn("cxx_std_17", component)
        self.assertIn("-std=gnu++17 -Wall -Wextra -Werror", component)

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


if __name__ == "__main__":
    unittest.main()
