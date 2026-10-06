"""Install a verified Companion bundle, keeping the old app on failure."""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def install_bundle(bundle: Path, directory: Path) -> Path:
    directory = directory.expanduser().resolve()
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / "Cardputer Companion.app"
    with tempfile.TemporaryDirectory(prefix=".companion-install-", dir=directory) as temporary:
        staging = Path(temporary) / destination.name
        previous = Path(temporary) / "previous.app"
        subprocess.run(["ditto", str(bundle.resolve()), str(staging)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(staging)], check=True)
        if destination.exists() or destination.is_symlink():
            destination.rename(previous)
        try:
            staging.rename(destination)
        except OSError:
            if previous.exists() or previous.is_symlink():
                previous.rename(destination)
            raise
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("Companion installation requires macOS")
    destination = install_bundle(args.bundle, args.directory)
    print(f"Installed: {destination}")
    print("Quit the running Companion, then open this installed application.")


if __name__ == "__main__":
    main()
