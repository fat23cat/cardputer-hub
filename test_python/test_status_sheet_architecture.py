"""Plan 051 rules for G0 and the STATUS sheet that the layer matrix cannot express."""

import pathlib
import tempfile
import unittest

from scripts import check_architecture


ROOT = pathlib.Path(__file__).resolve().parents[1]


class StatusSheetArchitectureTests(unittest.TestCase):
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

    def test_repository_sources_satisfy_the_g0_rules(self) -> None:
        kinds = [
            violation.kind
            for violation in check_architecture.find_nfc_violations(ROOT / "src")
        ]
        self.assertNotIn("g0_outside_shell", kinds)
        self.assertNotIn("status_toggle_outside_shell", kinds)

    def test_only_the_keyboard_adapter_and_shell_name_g0(self) -> None:
        line = "if (event.namedKey == core::NamedKey::G0) {}\n"
        for relative_path in (
            "apps/shell/application_shell.cpp",
            "hardware/cardputer/cardputer_keyboard_adapter.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assertEqual([], self.kinds({relative_path: line}))
        for relative_path in (
            "apps/pomodoro/pomodoro_app.cpp",
            "apps/network/wifi_settings.cpp",
            "services/hosts/host_service.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assertEqual(["g0_outside_shell"], self.kinds({relative_path: line}))

    def test_comments_may_mention_g0(self) -> None:
        self.assertEqual(
            [], self.kinds({"apps/ai/ai_app.cpp": "// NamedKey::G0 never reaches this app\n"})
        )

    def test_only_the_shell_names_the_status_toggle(self) -> None:
        line = 'actions.dispatch({"ui.status.toggle", "app", {}});\n'
        self.assertEqual([], self.kinds({"apps/shell/application_shell.cpp": line}))
        for relative_path in (
            "hardware/cardputer/cardputer_keyboard_adapter.cpp",
            "apps/led_gallery/led_gallery_app.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assertEqual(
                    ["status_toggle_outside_shell"], self.kinds({relative_path: line})
                )


if __name__ == "__main__":
    unittest.main()
