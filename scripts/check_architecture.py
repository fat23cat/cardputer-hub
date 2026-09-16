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
    violations = find_violations(source_root)
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
