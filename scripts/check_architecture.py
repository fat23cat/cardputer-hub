#!/usr/bin/env python3
"""Validate project-local C++ includes against the firmware layer boundaries."""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys
from collections.abc import Iterator, Sequence


LAYERS = ("core", "hardware", "connectivity", "services", "apps", "validation")
ALLOWED = {
    "core": {"core"},
    "hardware": {"core", "connectivity", "hardware"},
    "connectivity": {"core", "connectivity"},
    "services": {"core", "connectivity", "services"},
    "apps": {"core", "services", "apps"},
    "validation": set(LAYERS),
}
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp"}
LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')

# NFC inventory rules (plans 044 and 045). Layer boundaries alone do not keep
# the ST25R3916 library inside its adapter, confine tag writes to one reviewed
# Type 2 page write, or keep microSD and Companion access behind their owners.
NFC_TREES = ("core/nfc", "services/nfc", "apps/nfc", "hardware/nfc", "services/inventory")
# Reader, tag session and screen code keep no data; records belong to inventory.
NFC_STATELESS_TREES = ("core/nfc", "services/nfc", "apps/nfc", "hardware/nfc")
NFC_ADAPTER_DIRECTORY = "hardware/nfc/"
NFC_ADAPTER_SOURCE = "hardware/nfc/st25r3916_adapter.cpp"
NFC_ADAPTER_INCLUDE = re.compile(r'^\s*#\s*include\s*"hardware/nfc/')
# M5UnitUnified, M5UnitNFC, M5HAL, M5Utility and the library's own nfc/ and
# unit/ trees. M5Unified and M5GFX are the platform, not the NFC library.
NFC_VENDOR_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"]\s*(?:M5Unit(?!ed)|M5HAL|M5Utility|nfc/|unit/unit_ST25R3916)'
)
# The only tag write is the adapter's single-page Type 2 write.
NFC_PAGE_WRITE = re.compile(r"\bwrite4\b")
NFC_FORBIDDEN_API = re.compile(
    r"\b(?:write16|writeBlock|ndefWrite|ndefPrepare\w*|writeSupportNDEF"
    r"|writeCapabilityContainer|mifareClassicAuthenticate\w*|authenticateClassic"
    r"|readClassicBlock|NfcClassicKey|mifareClassicWrite\w+|mifareClassicIncrementValueBlock"
    r"|mifareClassicDecrementValueBlock|mifareClassicTransferValueBlock"
    r"|mifareClassicRestoreValueBlock|mifareUltralightChangeFormatToNDEF"
    r"|mifarePlusUpgradeSecurityLevel\d?|nfcaWriteBlock|writePtMemory\w*"
    r"|writeWithMAC16|externalAuthenticate|internalAuthenticate|clearSPAD|dump)\b"
)
# Key discovery, card emulation, cloning and raw card-data diagnostics.
# Substring matches, because the words hide inside camel-case identifiers.
NFC_FORBIDDEN_WORDS = re.compile(
    r"emulat|brute|dictionary|darkside|hardnested|nested_?attack|nested_?auth"
    r"|key_?search|key_?recovery|crack|clon(?:e|ing)|nfc\.diag",
    re.IGNORECASE,
)
NFC_APP_TREE = "apps/nfc/"
NFC_APP_FORBIDDEN = re.compile(
    r"\b(?:INfcReader|NfcService|setFieldEnabled|readPages|writePage|writeMessage"
    r"|startScanning|stopScanning)\b"
    r"|\b(?:detect|presence|initialize|shutdown)\s*\("
    r"|\bservice_\.update\s*\("
)
# The NFC Mini App renders inventory state; it reaches no storage or transport.
NFC_APP_FORBIDDEN_INCLUDE = re.compile(
    r'^\s*#\s*include\s*"(?:core/storage/|services/storage/|services/companion/'
    r'|connectivity/|core/nfc/|services/nfc/nfc_service)'
)
NFC_PERSISTENCE_INCLUDE = re.compile(
    r'^\s*#\s*include\s*"(?:core/storage/|services/configuration/|services/storage/)'
)
NFC_READER_HEADER = "core/nfc/nfc_reader.h"
# The reviewed reader surface. A new method, in particular any write, emulation
# or key-handling one, must change this list in the same reviewed diff.
NFC_READER_METHODS = frozenset(
    {
        "initialize",
        "shutdown",
        "setFieldEnabled",
        "detect",
        "presence",
        "readPages",
        "writePage",
    }
)
NFC_VIRTUAL_METHOD = re.compile(r"virtual\s+[\w:<>,\s&*]*?\b(\w+)\s*\(")
# Board filesystem and SD card APIs stay in the microSD adapter.
MICROSD_ADAPTER_DIRECTORY = "hardware/storage/microsd/"
BOARD_FILESYSTEM_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"]\s*(?:esp_vfs_fat\.h|sdmmc_cmd\.h|driver/sdspi_host\.h'
    r'|driver/sdmmc_host\.h|ff\.h|diskio\.h|dirent\.h)'
)
# The Companion protocol carries inventory records, never file operations.
COMPANION_TREES = ("connectivity/companion/", "services/companion/")
COMPANION_FILE_ENDPOINT = "services/inventory/inventory_companion_endpoint"
FILE_STORAGE_INCLUDE = re.compile(r'^\s*#\s*include\s*"(?:core/storage/|services/storage/)')


@dataclasses.dataclass(frozen=True)
class Violation:
    kind: str
    path: pathlib.Path
    line_number: int
    source_layer: str
    target_layer: str | None = None
    directive: str = ""


def source_files(source_root: pathlib.Path) -> Iterator[pathlib.Path]:
    for path in sorted(source_root.rglob("*")):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            yield path


def _is_runtime_source(path: pathlib.Path, source_root: pathlib.Path) -> bool:
    relative_path = path.relative_to(source_root)
    return relative_path.parts[:2] == ("apps", "runtime")


def _resolve_include(
    include: str, source_path: pathlib.Path, source_root: pathlib.Path
) -> tuple[pathlib.Path, pathlib.Path] | None:
    include_path = pathlib.PurePosixPath(include)
    if not include_path.parts or include_path.is_absolute():
        return None

    source_root = source_root.resolve()
    relative_include = pathlib.Path(*include_path.parts)
    root_candidate = source_root / relative_include
    if include_path.parts[0] == "..":
        resolved_path = (source_path.parent / relative_include).resolve()
    elif include_path.parts[0] in ALLOWED or root_candidate.exists():
        resolved_path = root_candidate.resolve()
    else:
        resolved_path = (source_path.parent / relative_include).resolve()

    try:
        return resolved_path, resolved_path.relative_to(source_root)
    except ValueError:
        return None


def _is_runtime_include(
    include: str, source_path: pathlib.Path, source_root: pathlib.Path
) -> bool:
    resolved = _resolve_include(include, source_path, source_root)
    return resolved is not None and resolved[1].parts[:2] == ("apps", "runtime")


def _source_layer(path: pathlib.Path, source_root: pathlib.Path) -> str:
    relative_path = path.relative_to(source_root)
    if relative_path == pathlib.Path("main.cpp"):
        return "main"
    if len(relative_path.parts) > 1:
        return relative_path.parts[0]
    return "<source root>"


def _target_layer(
    include: str, source_path: pathlib.Path, source_root: pathlib.Path
) -> str | None:
    resolved = _resolve_include(include, source_path, source_root)
    if resolved is None:
        return None
    resolved_path, relative_path = resolved
    if len(relative_path.parts) > 1:
        layer = relative_path.parts[0]
        if layer in ALLOWED or resolved_path.exists():
            return layer
        return None
    if relative_path.parts:
        return "<source root>" if resolved_path.exists() else None
    return None


def _without_comments(line: str, in_block_comment: bool) -> tuple[str, bool]:
    result: list[str] = []
    index = 0
    quote: str | None = None
    escaped = False

    while index < len(line):
        if in_block_comment:
            end = line.find("*/", index)
            if end < 0:
                return "".join(result), True
            index = end + 2
            in_block_comment = False
            continue

        character = line[index]
        following = line[index + 1] if index + 1 < len(line) else ""
        if quote is not None:
            result.append(character)
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = None
            index += 1
            continue

        if character in {'"', "'"}:
            quote = character
            result.append(character)
            index += 1
        elif character == "/" and following == "/":
            break
        elif character == "/" and following == "*":
            in_block_comment = True
            index += 2
        else:
            result.append(character)
            index += 1

    return "".join(result), in_block_comment


def find_violations(source_root: pathlib.Path) -> list[Violation]:
    violations: list[Violation] = []
    for path in source_files(source_root):
        source_layer = _source_layer(path, source_root)
        display_path = path.relative_to(source_root.parent)
        if source_layer != "main" and source_layer not in ALLOWED:
            violations.append(
                Violation(
                    kind="unknown_source_layer",
                    path=display_path,
                    line_number=1,
                    source_layer=source_layer,
                )
            )
            continue

        in_block_comment = False
        for line_number, raw_line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            line, in_block_comment = _without_comments(raw_line, in_block_comment)
            match = LOCAL_INCLUDE.match(line)
            if match is None:
                continue

            target_layer = _target_layer(match.group(1), path, source_root)
            if target_layer is None:
                continue
            if target_layer not in ALLOWED:
                violations.append(
                    Violation(
                        kind="unknown_target_layer",
                        path=display_path,
                        line_number=line_number,
                        source_layer=source_layer,
                        target_layer=target_layer,
                        directive=raw_line.strip(),
                    )
                )
                continue
            if _is_runtime_source(path, source_root) and target_layer != "core" and not (
                target_layer == "apps"
                and _is_runtime_include(match.group(1), path, source_root)
            ):
                violations.append(
                    Violation(
                        kind="forbidden_dependency",
                        path=display_path,
                        line_number=line_number,
                        source_layer=source_layer,
                        target_layer=target_layer,
                        directive=raw_line.strip(),
                    )
                )
                continue
            if source_layer == "main" or target_layer in ALLOWED[source_layer]:
                continue

            violations.append(
                Violation(
                    kind="forbidden_dependency",
                    path=display_path,
                    line_number=line_number,
                    source_layer=source_layer,
                    target_layer=target_layer,
                    directive=raw_line.strip(),
                )
            )
    return violations


def _code_lines(path: pathlib.Path) -> Iterator[tuple[int, str, str]]:
    """Yield (line number, raw line, line without comments)."""
    in_block_comment = False
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line, in_block_comment = _without_comments(raw_line, in_block_comment)
        yield line_number, raw_line, line


def find_nfc_violations(source_root: pathlib.Path) -> list[Violation]:
    """Check the NFC inventory rules that layer boundaries cannot express."""
    violations: list[Violation] = []

    def report(
        kind: str, path: pathlib.Path, line_number: int, layer: str, text: str
    ) -> None:
        violations.append(
            Violation(
                kind=kind,
                path=path.relative_to(source_root.parent),
                line_number=line_number,
                source_layer=layer,
                directive=text.strip(),
            )
        )

    for path in source_files(source_root):
        relative = path.relative_to(source_root).as_posix()
        layer = _source_layer(path, source_root)
        in_nfc_tree = any(relative.startswith(tree + "/") for tree in NFC_TREES)
        stateless = any(relative.startswith(tree + "/") for tree in NFC_STATELESS_TREES)
        in_companion = any(relative.startswith(tree) for tree in COMPANION_TREES) or (
            relative.startswith(COMPANION_FILE_ENDPOINT)
        )
        for line_number, raw_line, line in _code_lines(path):
            if relative != NFC_ADAPTER_SOURCE and NFC_VENDOR_INCLUDE.match(line):
                report("vendor_nfc_include", path, line_number, layer, raw_line)
            if (
                NFC_ADAPTER_INCLUDE.match(line)
                and relative != "main.cpp"
                and not relative.startswith(NFC_ADAPTER_DIRECTORY)
            ):
                report("nfc_adapter_include", path, line_number, layer, raw_line)
            if in_nfc_tree and not line.lstrip().startswith("#"):
                if NFC_FORBIDDEN_API.search(line) or NFC_FORBIDDEN_WORDS.search(line):
                    report("forbidden_nfc_api", path, line_number, layer, raw_line)
                if relative != NFC_ADAPTER_SOURCE and NFC_PAGE_WRITE.search(line):
                    report("forbidden_nfc_api", path, line_number, layer, raw_line)
            if relative.startswith(NFC_APP_TREE) and (
                NFC_APP_FORBIDDEN.search(line) or NFC_APP_FORBIDDEN_INCLUDE.match(line)
            ):
                report("app_drives_reader", path, line_number, layer, raw_line)
            if stateless and NFC_PERSISTENCE_INCLUDE.match(line):
                report("nfc_persists_data", path, line_number, layer, raw_line)
            if not relative.startswith(MICROSD_ADAPTER_DIRECTORY) and (
                BOARD_FILESYSTEM_INCLUDE.match(line)
            ):
                report("board_filesystem_include", path, line_number, layer, raw_line)
            if in_companion and FILE_STORAGE_INCLUDE.match(line):
                report("companion_file_access", path, line_number, layer, raw_line)

    reader_header = source_root / NFC_READER_HEADER
    if reader_header.is_file():
        body = "\n".join(line for _, _, line in _code_lines(reader_header))
        methods = {
            name
            for name in NFC_VIRTUAL_METHOD.findall(body)
            if not name.startswith("~")
        }
        if methods != NFC_READER_METHODS:
            added = ", ".join(sorted(methods - NFC_READER_METHODS)) or "none"
            removed = ", ".join(sorted(NFC_READER_METHODS - methods)) or "none"
            report(
                "reader_surface",
                reader_header,
                1,
                "core",
                f"added: {added}; removed: {removed}",
            )
    return violations


NFC_VIOLATION_HELP = {
    "vendor_nfc_include": (
        "the NFC vendor library may be included only by "
        "src/hardware/nfc/st25r3916_adapter.cpp"
    ),
    "nfc_adapter_include": (
        "only src/main.cpp and src/hardware/nfc/ may include the NFC adapter header"
    ),
    "forbidden_nfc_api": (
        "the only tag write is the adapter's single Type 2 page write (write4); no "
        "other tag writing, Classic authentication, emulation, cloning, key search "
        "or raw card diagnostics"
    ),
    "reader_surface": (
        "INfcReader's method set changed; update NFC_READER_METHODS only after "
        "review that it adds no write, emulation or key handling"
    ),
    "app_drives_reader": (
        "NfcApp renders InventoryService state and calls its commands; it never "
        "drives the reader, the NFC service, storage or the Companion transport"
    ),
    "nfc_persists_data": (
        "reader, tag-session and screen code keeps no data; records are stored "
        "only by services/inventory"
    ),
    "board_filesystem_include": (
        "board filesystem and SD card APIs may be included only by "
        "src/hardware/storage/microsd/"
    ),
    "companion_file_access": (
        "the Companion protocol carries inventory records through InventoryService; "
        "it never opens files or storage directly"
    ),
}


def _parser() -> argparse.ArgumentParser:
    repository_root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=pathlib.Path,
        default=repository_root / "src",
        help="source tree to inspect (default: repository src directory)",
    )
    return parser


def main(arguments: Sequence[str] | None = None) -> int:
    options = _parser().parse_args(arguments)
    source_root = options.source_root.resolve()
    violations = find_violations(source_root) + find_nfc_violations(source_root)
    if not violations:
        file_count = sum(1 for _ in source_files(source_root))
        print(f"Architecture dependencies OK ({file_count} files checked).")
        return 0

    print("Architecture violations:", file=sys.stderr)
    for violation in violations:
        location = f"\n{violation.path.as_posix()}:{violation.line_number}\n"
        if violation.kind == "unknown_source_layer":
            print(
                f"{location}\nUnknown architecture layer: {violation.source_layer}\n\n"
                "Add the layer to the documented dependency matrix before "
                "introducing production sources under it.",
                file=sys.stderr,
            )
        elif violation.kind == "unknown_target_layer":
            print(
                f"{location}\nUnknown architecture layer: {violation.target_layer}\n\n"
                f"{violation.directive}\n\n"
                "Add the layer to the documented dependency matrix before "
                "including project headers from it.",
                file=sys.stderr,
            )
        elif violation.kind in NFC_VIOLATION_HELP:
            print(
                f"{location}\n{violation.kind}: {NFC_VIOLATION_HELP[violation.kind]}\n\n"
                f"{violation.directive}",
                file=sys.stderr,
            )
        else:
            allowed = ", ".join(sorted(ALLOWED[violation.source_layer]))
            print(
                f"{location}\n"
                f"{violation.source_layer} -> {violation.target_layer} is forbidden\n\n"
                f"{violation.directive}\n\n"
                f"Allowed dependencies: {allowed}",
                file=sys.stderr,
            )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
