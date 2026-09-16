import contextlib
import io
import pathlib
import tempfile
import unittest

from scripts import check_architecture


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ArchitectureDependencyTests(unittest.TestCase):
    def check_fixture(self, files: dict[str, str]) -> list[check_architecture.Violation]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_root = pathlib.Path(temporary_directory) / "src"
            for relative_path, contents in files.items():
                path = source_root / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            return check_architecture.find_violations(source_root)

    def assert_allowed(self, source: str, dependency: str) -> None:
        violations = self.check_fixture(
            {f"{source}/feature.cpp": f'#include "{dependency}/contract.h"\n'}
        )
        self.assertEqual([], violations)

    def assert_forbidden(self, source: str, dependency: str) -> None:
        violations = self.check_fixture(
            {f"{source}/feature.cpp": f'#include "{dependency}/contract.h"\n'}
        )
        self.assertEqual(1, len(violations))
        self.assertEqual(source, violations[0].source_layer)
        self.assertEqual(dependency, violations[0].target_layer)
        self.assertEqual(1, violations[0].line_number)

    def test_runtime_may_depend_on_core_and_its_own_headers(self) -> None:
        self.assertEqual(
            [],
            self.check_fixture(
                {
                    "apps/runtime/mini_app_runtime.cpp": (
                        '#include "core/app_registry/app_registry.h"\n'
                        '#include "apps/runtime/mini_app.h"\n'
                    )
                }
            ),
        )

    def test_runtime_may_not_depend_on_services_or_other_apps(self) -> None:
        service_violations = self.check_fixture(
            {"apps/runtime/mini_app_runtime.cpp": '#include "services/hosts/host_service.h"\n'}
        )
        self.assertEqual(1, len(service_violations))
        self.assertEqual("apps", service_violations[0].source_layer)
        self.assertEqual("services", service_violations[0].target_layer)

        app_violations = self.check_fixture(
            {
                "apps/runtime/mini_app_runtime.cpp": '#include "apps/shell/application_shell.h"\n'
            }
        )
        self.assertEqual(1, len(app_violations))
        self.assertEqual("apps", app_violations[0].source_layer)
        self.assertEqual("apps", app_violations[0].target_layer)

    def test_runtime_cannot_hide_a_shell_dependency_behind_parent_segments(self) -> None:
        violations = self.check_fixture(
            {
                "apps/runtime/mini_app_runtime.cpp": (
                    '#include "apps/runtime/../shell/application_shell.h"\n'
                )
            }
        )

        self.assertEqual(1, len(violations))
        self.assertEqual("apps", violations[0].source_layer)
        self.assertEqual("apps", violations[0].target_layer)

    def test_apps_may_depend_on_services(self) -> None:
        self.assert_allowed("apps", "services")

    def test_apps_may_not_depend_on_hardware(self) -> None:
        self.assert_forbidden("apps", "hardware")

    def test_relative_include_cannot_bypass_the_layer_boundary(self) -> None:
        violations = self.check_fixture(
            {"apps/weather/feature.cpp": '#include "../../hardware/device.h"\n'}
        )

        self.assertEqual(1, len(violations))
        self.assertEqual("apps", violations[0].source_layer)
        self.assertEqual("hardware", violations[0].target_layer)

    def test_root_style_include_is_normalized_before_layer_classification(self) -> None:
        violations = self.check_fixture(
            {"apps/feature.cpp": '#include "apps/../hardware/device.h"\n'}
        )

        self.assertEqual(1, len(violations))
        self.assertEqual("apps", violations[0].source_layer)
        self.assertEqual("hardware", violations[0].target_layer)

    def test_core_root_style_include_cannot_hide_a_services_dependency(self) -> None:
        violations = self.check_fixture(
            {"core/feature.cpp": '#include "core/../services/service.h"\n'}
        )

        self.assertEqual(1, len(violations))
        self.assertEqual("core", violations[0].source_layer)
        self.assertEqual("services", violations[0].target_layer)

    def test_services_may_depend_on_connectivity(self) -> None:
        self.assert_allowed("services", "connectivity")

    def test_services_may_not_depend_on_apps(self) -> None:
        self.assert_forbidden("services", "apps")

    def test_connectivity_may_not_depend_on_hardware(self) -> None:
        self.assert_forbidden("connectivity", "hardware")

    def test_core_may_not_depend_on_services(self) -> None:
        self.assert_forbidden("core", "services")

    def test_hardware_may_depend_on_connectivity(self) -> None:
        self.assert_allowed("hardware", "connectivity")

    def test_validation_may_depend_on_every_production_layer(self) -> None:
        for dependency in check_architecture.LAYERS:
            with self.subTest(dependency=dependency):
                self.assert_allowed("validation", dependency)

    def test_main_may_compose_every_layer(self) -> None:
        includes = "".join(
            f'#include "{dependency}/contract.h"\n'
            for dependency in check_architecture.LAYERS
        )
        self.assertEqual([], self.check_fixture({"main.cpp": includes}))

    def test_system_includes_and_comment_examples_are_ignored(self) -> None:
        violations = self.check_fixture(
            {
                "apps/feature.cpp": (
                    "#include <hardware/device.h>\n"
                    '#include "esp_timer.h"\n'
                    '#include "freertos/FreeRTOS.h"\n'
                    '// #include "hardware/device.h"\n'
                    '/* #include "hardware/device.h" */\n'
                    "/*\n"
                    '#include "hardware/device.h"\n'
                    "*/\n"
                )
            }
        )
        self.assertEqual([], violations)

    def test_unknown_production_layer_is_rejected(self) -> None:
        violations = self.check_fixture(
            {"shared/feature.cpp": '#include "core/actions/action.h"\n'}
        )

        self.assertEqual(1, len(violations))
        self.assertEqual("unknown_source_layer", violations[0].kind)
        self.assertEqual("shared", violations[0].source_layer)

    def test_include_resolving_to_unknown_layer_is_rejected(self) -> None:
        violations = self.check_fixture(
            {
                "apps/feature.cpp": '#include "shared/contract.h"\n',
                "shared/contract.h": "#pragma once\n",
            }
        )

        target_violations = [
            violation
            for violation in violations
            if violation.kind == "unknown_target_layer"
        ]
        self.assertEqual(1, len(target_violations))
        self.assertEqual("apps", target_violations[0].source_layer)
        self.assertEqual("shared", target_violations[0].target_layer)

    def test_cli_reports_location_dependency_and_allowed_targets(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_root = pathlib.Path(temporary_directory) / "src"
            path = source_root / "apps" / "feature.cpp"
            path.parent.mkdir(parents=True)
            path.write_text('#include "hardware/device.h"\n', encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stderr(output):
                result = check_architecture.main(["--source-root", str(source_root)])

        self.assertEqual(1, result)
        report = output.getvalue()
        self.assertIn("apps/feature.cpp:1", report)
        self.assertIn("apps -> hardware is forbidden", report)
        self.assertIn('#include "hardware/device.h"', report)
        self.assertIn("Allowed dependencies: apps, core, services", report)

    def test_cli_explains_how_to_declare_an_unknown_layer(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_root = pathlib.Path(temporary_directory) / "src"
            path = source_root / "shared" / "feature.cpp"
            path.parent.mkdir(parents=True)
            path.write_text('#include "core/actions/action.h"\n', encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stderr(output):
                result = check_architecture.main(["--source-root", str(source_root)])

        self.assertEqual(1, result)
        report = output.getvalue()
        self.assertIn("src/shared/feature.cpp:1", report)
        self.assertIn("Unknown architecture layer: shared", report)
        self.assertIn("Add the layer to the documented dependency matrix", report)

    def test_host_check_runs_architecture_check_before_other_source_checks(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

        self.assertIn(
            "host-check: lock-check architecture-check format-check lint test", makefile
        )


if __name__ == "__main__":
    unittest.main()
