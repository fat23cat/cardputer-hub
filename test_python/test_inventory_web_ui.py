"""Exercise the WEB ACCESS page's inventory description list continuation."""

import pathlib
import shutil
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class InventoryListContinuationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "Node.js is needed for the browser behavior checks")
    def test_list_continuation(self) -> None:
        result = subprocess.run(
            ["node", "test_web/inventory_list.cjs"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=15,
            check=False,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("inventory list checks passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
