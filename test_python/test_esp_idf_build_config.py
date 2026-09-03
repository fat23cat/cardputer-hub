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

    def test_esp32_adapter_uses_nimble_without_bluedroid(self) -> None:
        adapter = self.read("src/hardware/esp32/bluetooth/esp32_bluetooth_adapter.cpp")

        self.assertIn("nimble/nimble_port.h", adapter)
        self.assertIn("host/ble_gap.h", adapter)
        self.assertNotIn("esp_bt_main.h", adapter)
        self.assertNotIn("esp_bluedroid_", adapter)


if __name__ == "__main__":
    unittest.main()
