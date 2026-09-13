import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AudioAssetTests(unittest.TestCase):
    def test_precomputed_audio_asset_is_reproducible(self):
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "generate_audio_clips.py"),
                "--check",
                str(ROOT / "src" / "services" / "audio" / "assets" / "interface_clips.h"),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(0, result.returncode, result.stderr or result.stdout)


if __name__ == "__main__":
    unittest.main()
