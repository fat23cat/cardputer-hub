import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "protocol" / "companion" / "fixtures"
REQUIRED = (
    "hello-v1.bin",
    "hello-ack-v1.bin",
    "ping-request-v1.bin",
    "ping-response-v1.bin",
    "capabilities-request-v1.bin",
    "capabilities-response-v1.bin",
    "app-active-request-v1.bin",
    "app-active-response-v1.bin",
    "app-activate-request-v1.bin",
    "app-activate-response-v1.bin",
    "app-active-changed-event-v1.bin",
    "malformed-length.bin",
    "unsupported-version.bin",
    "wrong-session.bin",
    "unknown-operation.bin",
)


class CompanionFixtureTests(unittest.TestCase):
    def test_required_binary_fixtures_exist(self) -> None:
        for name in REQUIRED:
            path = FIXTURES / name
            self.assertTrue(path.is_file(), path)
            self.assertGreater(path.stat().st_size, 0)
