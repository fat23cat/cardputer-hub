import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts.install_companion import install_bundle


class CompanionInstallTest(unittest.TestCase):
    def test_verified_bundle_replaces_old_files_in_directory_with_spaces(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "build/Cardputer Companion.app"
            source.mkdir(parents=True)
            (source / "new").write_text("new app")
            directory = root / "My Applications"
            destination = directory / source.name
            destination.mkdir(parents=True)
            (destination / "obsolete").write_text("old app")

            def run(command, **kwargs):
                if command[0] == "ditto":
                    shutil.copytree(command[1], command[2])

            with mock.patch("scripts.install_companion.subprocess.run", side_effect=run):
                self.assertEqual(install_bundle(source, directory), destination.resolve())
            self.assertEqual((destination / "new").read_text(), "new app")
            self.assertFalse((destination / "obsolete").exists())

    def test_failed_signature_keeps_previous_installation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "build.app"
            source.mkdir()
            destination = root / "Applications/Cardputer Companion.app"
            destination.mkdir(parents=True)
            (destination / "old").write_text("working app")

            def run(command, **kwargs):
                if command[0] == "ditto":
                    shutil.copytree(command[1], command[2])
                else:
                    raise subprocess.CalledProcessError(1, command)

            with mock.patch("scripts.install_companion.subprocess.run", side_effect=run):
                with self.assertRaises(subprocess.CalledProcessError):
                    install_bundle(source, destination.parent)
            self.assertEqual((destination / "old").read_text(), "working app")
