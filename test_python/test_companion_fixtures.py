import hashlib
import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "protocol" / "companion" / "generate_fixtures.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("generate_fixtures", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CompanionFixtureTests(unittest.TestCase):
    def test_committed_outputs_match_the_generator(self) -> None:
        # Fixtures, the fixture header and both fingerprint files must be exactly
        # what the generator writes, so a wire change cannot skip the fingerprint.
        generator = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            import sys

            argv = sys.argv
            sys.argv = [str(GENERATOR), "--root", directory]
            try:
                generator.main()
            finally:
                sys.argv = argv
            generated = sorted(p.relative_to(directory) for p in pathlib.Path(directory).rglob("*")
                               if p.is_file())
            self.assertTrue(generated)
            for relative in generated:
                committed = ROOT / relative
                self.assertTrue(committed.is_file(), committed)
                self.assertEqual(committed.read_bytes(),
                                 (pathlib.Path(directory) / relative).read_bytes(), relative)
            committed_fixtures = {p.name for p in (ROOT / "protocol/companion/fixtures").glob("*.bin")}
            generated_fixtures = {p.name for p in generated
                                  if str(p).startswith("protocol/companion/fixtures/")}
            self.assertEqual(committed_fixtures, generated_fixtures)

    def test_fingerprint_is_the_generator_hash(self) -> None:
        fingerprint = hashlib.sha256(GENERATOR.read_bytes()).digest()[:8]
        literal = ", ".join(f"0x{byte:02X}" for byte in fingerprint)
        header = (ROOT / "src/connectivity/companion/companion_fingerprint.h").read_text()
        swift = (ROOT / "companion/macos/Sources/CompanionCore/ProtocolFingerprint.swift").read_text()
        self.assertIn(literal, header)
        self.assertIn(literal, swift)
        hello = (ROOT / "protocol/companion/fixtures/hello.bin").read_bytes()
        self.assertEqual(hello[8:16], fingerprint)


if __name__ == "__main__":
    unittest.main()
