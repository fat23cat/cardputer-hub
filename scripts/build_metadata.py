import os
import subprocess

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


def git_commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=env.subst("$PROJECT_DIR"),
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def string_macro(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


version = os.environ.get("CARDPUTER_HUB_VERSION", "0.1.0-dev")
commit = os.environ.get("CARDPUTER_HUB_COMMIT", git_commit())
build_type = os.environ.get("CARDPUTER_HUB_BUILD_TYPE", env.subst("$PIOENV"))

env.Append(
    CPPDEFINES=[
        ("CARDPUTER_HUB_VERSION", string_macro(version)),
        ("CARDPUTER_HUB_COMMIT", string_macro(commit)),
        ("CARDPUTER_HUB_BUILD_TYPE", string_macro(build_type)),
    ]
)
