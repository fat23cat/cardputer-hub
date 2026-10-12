"""The page and icons compiled into the firmware match their sources (plan 050)."""

from __future__ import annotations

import gzip
import json
import pathlib
import re
import struct
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "web" / "index.html"
HEADER = ROOT / "src" / "services" / "web_access" / "web_ui_asset.h"


def array_bytes(identifier: str) -> bytes:
    text = HEADER.read_text(encoding="utf-8")
    start = text.index(f"webUi{identifier}Data[] = {{")
    array = text[start : text.index("};", start)]
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", array))


def served_paths() -> dict[str, str]:
    text = HEADER.read_text(encoding="utf-8")
    return dict(re.findall(r'\{"(/[^"]*)", "([^"]+)", (?:true|false), webUiBytes', text))


def png_size(data: bytes) -> tuple[int, int]:
    assert data.startswith(b"\x89PNG\r\n\x1a\n"), "not a PNG"
    width, height = struct.unpack(">II", data[16:24])
    return width, height


class WebUiAssetTests(unittest.TestCase):
    def test_generated_header_is_current(self) -> None:
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/generate_web_ui.py"), "--check"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(0, result.returncode, result.stderr)

    def test_check_ignores_how_zlib_compressed_the_content(self) -> None:
        sys.path.insert(0, str(ROOT / "scripts"))
        try:
            import generate_web_ui as generator
        finally:
            sys.path.pop(0)
        header = HEADER.read_text(encoding="utf-8")
        page = gzip.decompress(array_bytes("Page"))
        # The same page compressed at another level: other bytes, same content.
        recompressed = gzip.compress(page, compresslevel=1, mtime=0)
        start = header.index("webUiPageData[] = {")
        end = header.index("};", start)
        rewritten = (
            header[:start]
            + "webUiPageData[] = {"
            + ", ".join(f"0x{byte:02x}" for byte in recompressed)
            + ",\n"
            + header[end:]
        )
        self.assertNotEqual(header, rewritten)
        self.assertEqual(generator.canonical(header), generator.canonical(rewritten))
        # A changed page is still caught.
        other = gzip.compress(page + b" ", compresslevel=9, mtime=0)
        stale = (
            header[:start]
            + "webUiPageData[] = {"
            + ", ".join(f"0x{byte:02x}" for byte in other)
            + ",\n"
            + header[end:]
        )
        self.assertNotEqual(generator.canonical(header), generator.canonical(stale))

    def test_generated_page_is_the_current_source(self) -> None:
        self.assertEqual(SOURCE.read_bytes(), gzip.decompress(array_bytes("Page")))

    def test_home_screen_icons_are_square_pngs(self) -> None:
        for identifier, size in (("AppleTouchIcon", 180), ("Icon192", 192), ("Icon512", 512)):
            with self.subTest(icon=identifier):
                self.assertEqual((size, size), png_size(array_bytes(identifier)))

    def test_manifest_names_served_icons(self) -> None:
        manifest = json.loads(array_bytes("Manifest"))
        paths = served_paths()
        self.assertEqual("standalone", manifest["display"])
        self.assertEqual("/", manifest["start_url"])
        for icon in manifest["icons"]:
            self.assertEqual("image/png", paths[icon["src"]])
        self.assertEqual("application/manifest+json", paths["/manifest.webmanifest"])
        self.assertEqual("image/png", paths["/apple-touch-icon.png"])

    def test_page_links_the_icons_and_manifest(self) -> None:
        page = SOURCE.read_text(encoding="utf-8")
        paths = served_paths()
        for link in re.findall(r'<link rel="(?:apple-touch-icon|icon|manifest)"[^>]*href="([^"]+)"', page):
            self.assertIn(link, paths)
        self.assertIn('rel="apple-touch-icon"', page)
        self.assertIn('rel="manifest"', page)
        self.assertIn('name="apple-mobile-web-app-capable" content="yes"', page)
        self.assertIn('name="apple-mobile-web-app-title" content="Hub"', page)

    def test_page_loads_nothing_from_outside_the_cardputer(self) -> None:
        page = SOURCE.read_text(encoding="utf-8")
        # The access point has no internet; the page is self-contained.
        self.assertNotRegex(page, r"(src|href)=\"https?://")
        self.assertNotIn("@import", page)
        self.assertNotIn("fonts.googleapis", page)

    def test_page_never_inserts_markup(self) -> None:
        page = SOURCE.read_text(encoding="utf-8")
        # Record text is set with textContent only.
        for sink in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write"):
            self.assertNotIn(sink, page)

    def test_mutating_requests_carry_the_request_marker(self) -> None:
        page = SOURCE.read_text(encoding="utf-8")
        self.assertIn('init.headers["X-Hub-Request"] = "1"', page)

    def test_assets_fit_comfortably_in_flash(self) -> None:
        total = sum(
            len(array_bytes(name))
            for name in ("Page", "AppleTouchIcon", "Icon192", "Icon512", "Manifest")
        )
        self.assertLess(total, 40 * 1024)


if __name__ == "__main__":
    unittest.main()
