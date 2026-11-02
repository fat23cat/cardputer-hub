#!/usr/bin/env python3
"""Keep only the pinned versions of pinned PlatformIO tools in the core directory.

PlatformIO 6.1.19 installs a tool pinned in `platform_packages` and then keeps
only the newest installed version matching its own core requirement. A newer
copy left by another PlatformIO Core makes it delete the pinned one it has just
installed, and `pio check` fails. Removing the other versions first keeps the
pinned one the newest.
"""

from __future__ import annotations

import argparse
import configparser
import json
import pathlib
import shutil
import sys
from collections.abc import Sequence


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_project(ini: pathlib.Path) -> tuple[pathlib.Path, dict[str, str]]:
    """The core directory and the exact `name -> version` tool pins of `ini`."""
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(ini, encoding="utf-8")
    core_dir = ini.parent / parser.get("platformio", "core_dir")
    pins: dict[str, str] = {}
    for section in parser.sections():
        for spec in parser.get(section, "platform_packages", fallback="").split():
            name, separator, version = spec.partition("@")
            if separator:
                pins[name.rsplit("/", 1)[-1]] = version
    return core_dir, pins


def installed_version(package: pathlib.Path) -> tuple[str, str] | None:
    try:
        manifest = json.loads((package / ".piopm").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return manifest.get("name", ""), manifest.get("version", "")


def prune(core_dir: pathlib.Path, pins: dict[str, str]) -> list[pathlib.Path]:
    """Removes every installed version of a pinned tool but the pinned one."""
    packages = core_dir / "packages"
    if not packages.is_dir():
        return []
    removed: list[pathlib.Path] = []
    for package in sorted(packages.iterdir()):
        installed = installed_version(package)
        if installed is None:
            continue
        name, version = installed
        if name in pins and version != pins[name]:
            shutil.rmtree(package)
            removed.append(package)
    return removed


def main(arguments: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ini", type=pathlib.Path, default=ROOT / "platformio.ini")
    args = parser.parse_args(arguments)
    core_dir, pins = read_project(args.ini)
    for package in prune(core_dir, pins):
        print(f"Removed unpinned PlatformIO tool {package.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
