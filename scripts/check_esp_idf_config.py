#!/usr/bin/env python3
"""Validate the effective ESP-IDF configuration of a production build."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections.abc import Sequence


ENABLED_SETTINGS = ("CONFIG_COMPILER_OPTIMIZATION_SIZE",)
DISABLED_SETTINGS = (
    "CONFIG_ESP_WIFI_SOFTAP_SUPPORT",
    "CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT",
)
DEFINE = re.compile(r"^\s*#define\s+([A-Z0-9_]+)(?:\s+(.+))?$")


def read_defines(header: pathlib.Path) -> dict[str, str]:
    defines: dict[str, str] = {}
    for line in header.read_text(encoding="utf-8").splitlines():
        match = DEFINE.match(line)
        if match is not None:
            defines[match.group(1)] = (match.group(2) or "").strip()
    return defines


def validate(header: pathlib.Path) -> list[str]:
    defines = read_defines(header)
    errors = [
        f"{setting} must be enabled in the effective ESP-IDF configuration"
        for setting in ENABLED_SETTINGS
        if defines.get(setting) != "1"
    ]
    errors.extend(
        f"{setting} must be disabled in the effective ESP-IDF configuration"
        for setting in DISABLED_SETTINGS
        if setting in defines
    )
    return errors


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate Plan-026 settings in generated sdkconfig.h."
    )
    parser.add_argument("header", type=pathlib.Path)
    arguments = parser.parse_args(argv)

    if not arguments.header.is_file():
        print(f"Generated ESP-IDF configuration not found: {arguments.header}", file=sys.stderr)
        return 2

    errors = validate(arguments.header)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"Effective ESP-IDF configuration OK ({arguments.header}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
