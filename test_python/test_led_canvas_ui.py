"""Exercise the actual embedded page's model, queue and request lifecycle."""

import pathlib
import shutil
import subprocess
import unittest
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


class LedCanvasBrowserTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "Node.js is needed for the browser behavior checks")
    def test_browser_behavior(self) -> None:
        result = subprocess.run(
            ["node", "test_web/led_canvas.cjs"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=15,
            check=False,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("lifecycle checks passed", result.stdout)


    @unittest.skipUnless(shutil.which("node") and shutil.which("c++"), "Node.js and C++ compiler needed")
    def test_firmware_browser_color_parity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "led-canvas-parity"
            compiled = subprocess.run(
                ["c++", "-std=c++17", "-I", str(ROOT / "src"),
                 str(ROOT / "test_web/led_canvas_parity.cpp"),
                 str(ROOT / "src/apps/led_canvas/led_canvas_engine.cpp"), "-o", str(binary)],
                cwd=ROOT, text=True, capture_output=True, timeout=30, check=False,
            )
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            result = subprocess.run(
                ["node", "test_web/led_canvas_parity.cjs", str(binary)], cwd=ROOT,
                text=True, capture_output=True, timeout=15, check=False,
            )
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("color parity passed", result.stdout)
