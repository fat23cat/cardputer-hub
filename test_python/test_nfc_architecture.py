import contextlib
import io
import pathlib
import tempfile
import unittest

from scripts import check_architecture


ROOT = pathlib.Path(__file__).resolve().parents[1]

READER_HEADER = """#pragma once
class INfcReader {
  public:
    virtual ~INfcReader() = default;
    virtual NfcReaderInitResult initialize() = 0;
    virtual void shutdown() = 0;
    virtual void setFieldEnabled(bool enabled) = 0;
    virtual NfcDetection detect() = 0;
    virtual NfcPresence presence(NfcActivationId activation) = 0;
    virtual NfcPageReadResult readPages(NfcActivationId activation,
                                        std::uint16_t firstPage) = 0;
    virtual NfcPageWriteResult writePage(NfcActivationId activation, std::uint16_t page,
                                         const NfcType2Page& data) = 0;
};
"""


class NfcArchitectureTests(unittest.TestCase):
    def check(self, files: dict[str, str]) -> list[check_architecture.Violation]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_root = pathlib.Path(temporary_directory) / "src"
            for relative_path, contents in files.items():
                path = source_root / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            return check_architecture.find_nfc_violations(source_root)

    def kinds(self, files: dict[str, str]) -> list[str]:
        return [violation.kind for violation in self.check(files)]

    def test_repository_nfc_sources_satisfy_every_nfc_rule(self) -> None:
        self.assertEqual([], check_architecture.find_nfc_violations(ROOT / "src"))

    # ---- vendor library confinement ------------------------------------------

    def test_adapter_source_may_include_the_vendor_library(self) -> None:
        self.assertEqual(
            [],
            self.check(
                {
                    "hardware/nfc/st25r3916_adapter.cpp": (
                        "#include <M5Unified.hpp>\n"
                        "#include <M5UnitUnified.hpp>\n"
                        "#include <nfc/layer/a/nfc_layer_a.hpp>\n"
                        "#include <unit/unit_ST25R3916.hpp>\n"
                    )
                }
            ),
        )

    def test_vendor_headers_cannot_appear_outside_the_adapter_source(self) -> None:
        for relative_path in (
            "hardware/nfc/st25r3916_adapter.h",
            "services/nfc/nfc_service.cpp",
            "apps/nfc/nfc_app.cpp",
            "core/nfc/nfc_types.h",
            "hardware/cardputer/cardputer_platform.cpp",
            "main.cpp",
        ):
            for include in (
                "#include <M5UnitUnified.hpp>\n",
                '#include "M5UnitUnifiedNFC.h"\n',
                "#include <nfc/layer/a/nfc_layer_a.hpp>\n",
                "#include <unit/unit_ST25R3916.hpp>\n",
                "#include <M5HAL.hpp>\n",
                "#include <M5Utility.hpp>\n",
            ):
                with self.subTest(path=relative_path, include=include.strip()):
                    self.assertEqual(
                        ["vendor_nfc_include"], self.kinds({relative_path: include})
                    )

    def test_unrelated_m5_headers_are_not_mistaken_for_the_nfc_library(self) -> None:
        self.assertEqual(
            [],
            self.check(
                {
                    "hardware/cardputer/cardputer_platform.cpp": (
                        "#include <M5Unified.hpp>\n#include <M5GFX.h>\n"
                    )
                }
            ),
        )

    def test_vendor_include_hidden_in_a_comment_is_ignored(self) -> None:
        self.assertEqual(
            [],
            self.check(
                {"services/nfc/nfc_service.cpp": "// #include <M5UnitUnified.hpp>\n"}
            ),
        )

    # ---- adapter header ownership --------------------------------------------

    def test_only_the_composition_root_and_the_nfc_adapter_may_include_the_adapter(
        self,
    ) -> None:
        include = '#include "hardware/nfc/st25r3916_adapter.h"\n'
        self.assertEqual([], self.check({"main.cpp": include}))
        self.assertEqual([], self.check({"hardware/nfc/other.cpp": include}))
        for relative_path in (
            "hardware/cardputer/cardputer_platform.cpp",
            "services/nfc/nfc_service.cpp",
            "apps/nfc/nfc_app.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assertEqual(
                    ["nfc_adapter_include"], self.kinds({relative_path: include})
                )

    # ---- tag writes, emulation and key handling --------------------------------

    def test_nfc_sources_cannot_use_other_writes_emulation_or_key_handling(self) -> None:
        for snippet in (
            "layer.write16(1, data, 16);",
            "layer.ndefWrite(message);",
            "layer.writeCapabilityContainer(cc);",
            "layer.mifareClassicAuthenticateA(4, key);",
            "reader.authenticateClassic(a, b, c, d);",
            "NfcClassicKey key{};",
            "layer.mifareClassicWriteValueBlock(4, 1);",
            "layer.mifareClassicIncrementValueBlock(4, 1);",
            "layer.mifarePlusUpgradeSecurityLevel3();",
            "unit.writePtMemoryA(data, 4);",
            "unit.configureEmulationMode(mode);",
            "EmulationLayerA layer;",
            "layer.dump(key);",
            "bruteForceKeys();",
            "tryDictionary();",
            "runDarkside();",
            "nestedAttack();",
            "cloneTag();",
            'log("nfc.diag block=1");',
        ):
            for relative_path in (
                "hardware/nfc/st25r3916_adapter.cpp",
                "services/nfc/nfc_service.cpp",
                "services/inventory/inventory_service.cpp",
                "apps/nfc/nfc_app.cpp",
                "core/nfc/nfc_types.h",
            ):
                with self.subTest(path=relative_path, snippet=snippet):
                    self.assertEqual(
                        ["forbidden_nfc_api"],
                        self.kinds({relative_path: snippet + "\n"}),
                    )

    def test_only_the_adapter_source_may_issue_the_page_write(self) -> None:
        snippet = "impl.layerA.write4(page, data, 4, true);\n"
        self.assertEqual([], self.check({"hardware/nfc/st25r3916_adapter.cpp": snippet}))
        for relative_path in (
            "hardware/nfc/st25r3916_adapter.h",
            "services/nfc/nfc_service.cpp",
            "services/inventory/inventory_service.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assertEqual(["forbidden_nfc_api"], self.kinds({relative_path: snippet}))

    def test_forbidden_words_in_comments_do_not_count(self) -> None:
        self.assertEqual(
            [],
            self.check(
                {
                    "services/nfc/nfc_service.cpp": (
                        "// never emulates, clones or brute forces\n"
                        "/* dictionary and nested attacks are out of scope */\n"
                        "int value = 0;\n"
                    )
                }
            ),
        )

    def test_the_rule_covers_only_nfc_directories(self) -> None:
        self.assertEqual(
            [],
            self.check({"services/pomodoro/pomodoro_service.cpp": "write16(1, 2);\n"}),
        )

    def test_reader_interface_surface_is_exactly_the_reviewed_set(self) -> None:
        self.assertEqual([], self.check({"core/nfc/nfc_reader.h": READER_HEADER}))

        for added in (
            "virtual bool writeBlock(int block) = 0;",
            "virtual void emulate() = 0;",
            "virtual void lockTag() = 0;",
            "virtual void searchKeys() = 0;",
        ):
            with self.subTest(added=added):
                source = READER_HEADER.replace("};\n", f"    {added}\n}};\n")
                self.assertIn("reader_surface", self.kinds({"core/nfc/nfc_reader.h": source}))

        removed = READER_HEADER.replace(
            "    virtual void shutdown() = 0;\n", ""
        )
        self.assertIn("reader_surface", self.kinds({"core/nfc/nfc_reader.h": removed}))

    # ---- ownership ----------------------------------------------------------------

    def test_the_nfc_app_cannot_drive_the_reader_storage_or_transport(self) -> None:
        for snippet in (
            "INfcReader& reader;",
            "NfcService& nfc;",
            "nfc.startScanning();",
            "nfc.writeMessage(1, message);",
            "reader.readPages(a, 4);",
            "reader.setFieldEnabled(true);",
            "reader.detect();",
            "service_.update(elapsed);",
            '#include "core/storage/files/file_storage.h"',
            '#include "services/storage/removable_storage_service.h"',
            '#include "services/companion/companion_service.h"',
            '#include "services/nfc/nfc_service.h"',
        ):
            with self.subTest(snippet=snippet):
                self.assertIn(
                    "app_drives_reader",
                    self.kinds({"apps/nfc/nfc_app.cpp": snippet + "\n"}),
                )

    def test_the_nfc_app_may_use_inventory_commands_and_status(self) -> None:
        self.assertEqual(
            [],
            self.check(
                {
                    "apps/nfc/nfc_app.cpp": (
                        '#include "services/inventory/inventory_service.h"\n'
                        "inventory_.open();\n"
                        "inventory_.registerTag(view.draft);\n"
                        "const auto& status = inventory_.status();\n"
                    )
                }
            ),
        )

    def test_reader_and_session_code_keeps_no_data(self) -> None:
        for relative_path in (
            "services/nfc/nfc_service.cpp",
            "apps/nfc/nfc_app.cpp",
            "hardware/nfc/st25r3916_adapter.cpp",
        ):
            for include in (
                '#include "core/storage/storage.h"\n',
                '#include "services/configuration/configuration_service.h"\n',
            ):
                with self.subTest(path=relative_path, include=include.strip()):
                    self.assertIn("nfc_persists_data", self.kinds({relative_path: include}))

    def test_inventory_owns_record_storage(self) -> None:
        self.assertEqual(
            [],
            self.check(
                {
                    "services/inventory/inventory_store.h": (
                        '#include "core/storage/files/file_storage.h"\n'
                    )
                }
            ),
        )

    def test_board_filesystem_apis_stay_in_the_microsd_adapter(self) -> None:
        include = '#include "esp_vfs_fat.h"\n'
        self.assertEqual(
            [], self.check({"hardware/storage/microsd/cardputer_microsd.cpp": include})
        )
        for relative_path in (
            "services/inventory/inventory_store.cpp",
            "hardware/cardputer/cardputer_platform.cpp",
            "main.cpp",
        ):
            for directive in (include, "#include <dirent.h>\n", '#include "sdmmc_cmd.h"\n'):
                with self.subTest(path=relative_path, include=directive.strip()):
                    self.assertEqual(
                        ["board_filesystem_include"], self.kinds({relative_path: directive})
                    )

    def test_companion_code_never_opens_storage(self) -> None:
        for relative_path in (
            "connectivity/companion/companion_protocol.cpp",
            "services/companion/companion_service.cpp",
            "services/inventory/inventory_companion_endpoint.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assertEqual(
                    ["companion_file_access"],
                    self.kinds({relative_path: '#include "core/storage/files/file_storage.h"\n'}),
                )

    def test_other_code_may_use_storage(self) -> None:
        self.assertEqual(
            [],
            self.check(
                {"services/pomodoro/pomodoro_service.cpp": '#include "core/storage/storage.h"\n'}
            ),
        )

    def test_cli_reports_nfc_violations(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_root = pathlib.Path(temporary_directory) / "src"
            path = source_root / "apps" / "nfc" / "nfc_app.cpp"
            path.parent.mkdir(parents=True)
            path.write_text("reader.detect();\n", encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stderr(output):
                result = check_architecture.main(["--source-root", str(source_root)])

        self.assertEqual(1, result)
        report = output.getvalue()
        self.assertIn("apps/nfc/nfc_app.cpp:1", report)
        self.assertIn("app_drives_reader", report)


if __name__ == "__main__":
    unittest.main()
