import json
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_pruner():
    sys.path.insert(0, str(ROOT / "scripts"))
    try:
        import prune_platformio_tools as pruner
    finally:
        sys.path.pop(0)
    return pruner


def install(packages: pathlib.Path, directory: str, name: str, version: str) -> pathlib.Path:
    path = packages / directory
    path.mkdir(parents=True)
    (path / ".piopm").write_text(
        json.dumps({"type": "tool", "name": name, "version": version}), encoding="utf-8"
    )
    return path


class PlatformioToolPinTests(unittest.TestCase):
    def test_project_pins_are_read_from_platformio_ini(self) -> None:
        pruner = load_pruner()
        core_dir, pins = pruner.read_project(ROOT / "platformio.ini")

        self.assertEqual(ROOT / ".cache" / "platformio", core_dir)
        self.assertEqual({"tool-cppcheck": "1.21100.230717"}, pins)

    def test_only_other_versions_of_pinned_tools_are_removed(self) -> None:
        pruner = load_pruner()
        with tempfile.TemporaryDirectory() as directory:
            packages = pathlib.Path(directory) / "packages"
            newer = install(packages, "tool-cppcheck", "tool-cppcheck", "1.21100.241030")
            pinned = install(
                packages, "tool-cppcheck@1.21100.230717", "tool-cppcheck", "1.21100.230717"
            )
            scons = install(packages, "tool-scons", "tool-scons", "4.40801.0")
            unknown = packages / "tool-unknown"
            unknown.mkdir()

            removed = pruner.prune(
                pathlib.Path(directory), {"tool-cppcheck": "1.21100.230717"}
            )

            self.assertEqual([newer], removed)
            self.assertFalse(newer.exists())
            self.assertTrue(pinned.exists())
            self.assertTrue(scons.exists())
            self.assertTrue(unknown.exists())
            self.assertEqual(
                [], pruner.prune(pathlib.Path(directory), {"tool-cppcheck": "1.21100.230717"})
            )

    def test_missing_core_directory_is_not_an_error(self) -> None:
        pruner = load_pruner()
        with tempfile.TemporaryDirectory() as directory:
            missing = pathlib.Path(directory) / "absent"
            self.assertEqual([], pruner.prune(missing, {"tool-cppcheck": "1.21100.230717"}))

    def test_lint_prunes_before_the_platformio_check(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        lint = makefile.split("\nlint:\n", 1)[1].split("\n\n", 1)[0]

        self.assertLess(
            lint.index("scripts/prune_platformio_tools.py"), lint.index("pio check -e native")
        )


if __name__ == "__main__":
    unittest.main()
