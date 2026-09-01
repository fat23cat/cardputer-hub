import csv
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORMIO_CONFIG = ROOT / "platformio.ini"
PARTITION_TABLE = ROOT / "partitions.csv"
NVS_ADAPTER = ROOT / "src" / "hardware" / "esp32" / "esp32_nvs_storage_adapter.cpp"


def parse_size(value: str) -> int:
    units = {"K": 1024, "M": 1024 * 1024}
    normalized = value.strip().upper()
    if normalized[-1:] in units:
        return int(normalized[:-1], 0) * units[normalized[-1]]
    return int(normalized, 0)


class ConfigurationPartitionTest(unittest.TestCase):
    def test_cardputer_build_uses_project_partition_table(self) -> None:
        config = PLATFORMIO_CONFIG.read_text()
        self.assertIn("board_build.partitions = partitions.csv", config)

    def test_authoritative_configuration_has_a_separate_nvs_partition(self) -> None:
        with PARTITION_TABLE.open(newline="") as partition_file:
            rows = [
                row
                for row in csv.reader(
                    line for line in partition_file if not line.lstrip().startswith("#")
                )
                if row
            ]

        nvs_partitions = [
            row for row in rows if row[1].strip() == "data" and row[2].strip() == "nvs"
        ]
        self.assertGreaterEqual(len(nvs_partitions), 2)
        self.assertEqual(nvs_partitions[0][0].strip(), "nvs")

        configuration = next(row for row in nvs_partitions if row[0].strip() == "hub_config")
        self.assertGreaterEqual(parse_size(configuration[4]), 0x3000)

    def test_adapter_never_opens_or_erases_the_default_partition(self) -> None:
        source = NVS_ADAPTER.read_text()
        self.assertIn('configurationPartition = "hub_config"', source)
        self.assertIn("nvs_flash_init_partition(configurationPartition)", source)
        self.assertIn("nvs_open_from_partition(configurationPartition", source)
        self.assertNotIn("nvs_flash_erase", source)
        self.assertIsNone(re.search(r"\bnvs_open\(", source))


if __name__ == "__main__":
    unittest.main()
