#!/usr/bin/env python3

import re
import sys


SEMANTIC_TAG = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
FIRST_RELEASE = (0, 1, 0)
NO_RELEASE = 3
BRANCH_BUMPS = {
    "breaking": "major",
    "major": "major",
    "feat": "minor",
    "minor": "minor",
    "fix": "patch",
    "patch": "patch",
    "perf": "patch",
}


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: next_release_version.py BRANCH")

    branch_type = sys.argv[1].partition("/")[0]
    bump = BRANCH_BUMPS.get(branch_type)
    if bump is None:
        raise SystemExit(NO_RELEASE)

    versions = []
    for line in sys.stdin:
        match = SEMANTIC_TAG.fullmatch(line.strip())
        if match:
            versions.append(tuple(int(component) for component in match.groups()))

    if not versions:
        major, minor, patch = FIRST_RELEASE
    else:
        major, minor, patch = max(versions)
        if bump == "major":
            major, minor, patch = major + 1, 0, 0
        elif bump == "minor":
            minor, patch = minor + 1, 0
        else:
            patch += 1

    print(f"{major}.{minor}.{patch}")


if __name__ == "__main__":
    main()
