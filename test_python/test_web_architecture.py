"""Plan 050 rules for the local web UI that the layer matrix cannot express."""

import pathlib
import tempfile
import unittest

from scripts import check_architecture


ROOT = pathlib.Path(__file__).resolve().parents[1]


class WebArchitectureTests(unittest.TestCase):
    def kinds(self, files: dict[str, str]) -> list[str]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_root = pathlib.Path(temporary_directory) / "src"
            for relative_path, contents in files.items():
                path = source_root / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            return [
                violation.kind for violation in check_architecture.find_nfc_violations(source_root)
            ]

    def test_repository_web_sources_satisfy_every_web_rule(self) -> None:
        kinds = [
            violation.kind
            for violation in check_architecture.find_nfc_violations(ROOT / "src")
        ]
        self.assertEqual([], kinds)

    def test_only_the_server_adapter_includes_esp_http_server(self) -> None:
        self.assertEqual(
            [],
            self.kinds(
                {"hardware/esp32/http/esp32_http_server.cpp": '#include "esp_http_server.h"\n'}
            ),
        )
        for relative_path in (
            "hardware/esp32/http/esp32_http_server.h",
            "services/web_access/web_access_service.cpp",
            "main.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assertEqual(
                    ["httpd_include"],
                    self.kinds({relative_path: "#include <esp_http_server.h>\n"}),
                )

    def test_web_code_never_opens_files(self) -> None:
        for relative_path in (
            "services/web_access/web_access_service.cpp",
            "services/inventory/inventory_web_endpoint.cpp",
        ):
            for include in (
                '#include "core/storage/files/file_storage.h"\n',
                '#include "services/storage/removable_storage_service.h"\n',
                '#include "services/inventory/inventory_store.h"\n',
            ):
                with self.subTest(path=relative_path, include=include):
                    self.assertEqual(["web_file_access"], self.kinds({relative_path: include}))
        # Key-value storage for the access-point credentials is allowed.
        self.assertEqual(
            [],
            self.kinds(
                {"services/web_access/web_access_service.cpp": '#include "core/storage/storage.h"\n'}
            ),
        )

    def test_led_canvas_ownership_boundaries(self) -> None:
        for path, include, expected in (
            ("apps/led_canvas/led_canvas_engine.cpp", "services/led_canvas/led_canvas_service.h", "canvas_engine_dependency"),
            ("services/led_canvas/led_canvas_service.cpp", "services/indicator/indicator_service.h", "canvas_service_dependency"),
            ("services/led_canvas/led_canvas_service.cpp", "services/web_access/web_access_service.h", "canvas_service_dependency"),
            ("services/led_canvas/led_canvas_web_endpoint.cpp", "apps/led_canvas/led_canvas_engine.h", "canvas_endpoint_dependency"),
        ):
            with self.subTest(path=path, include=include):
                self.assertEqual([expected], self.kinds({path: f'#include "{include}"\n'}))
        self.assertEqual(
            ["web_app_mutates_service"],
            self.kinds({"apps/led_canvas/led_canvas_app.cpp": "web_.setEnabled(true);\n"}),
        )

    def test_web_access_app_changes_the_service_only_through_actions(self) -> None:
        self.assertEqual(
            ["web_app_mutates_service"],
            self.kinds({"apps/web_access/web_access_app.cpp": "web_.setEnabled(true);\n"}),
        )


if __name__ == "__main__":
    unittest.main()
