"""
build_version.py — PlatformIO pre-build extra_script

Injects a BUILD_SUFFIX C preprocessor define so the firmware's VERSION macro
includes build provenance at compile time:

  Local builds:  BASE_VERSION-<username>-<YYYYMMDD>-<hash>
                 e.g. 3.08.0-wagfam-justin-20260426-a1b2c3d
  CI builds:     BASE_VERSION-<hash>
                 e.g. 3.08.0-wagfam-a1b2c3d

BUILD_SUFFIX is consumed by marquee/marquee.ino via adjacent C string concatenation:

    #define BASE_VERSION "3.08.0-wagfam"
    #ifdef BUILD_SUFFIX
    #define VERSION BASE_VERSION BUILD_SUFFIX
    #else
    #define VERSION BASE_VERSION   // fallback when script is not run (e.g. Arduino IDE)
    #endif

Environment variables read at build time:
  CI              Set to any non-empty value for a CI-style suffix (hash only).
                  GitHub Actions sets this automatically; the Makefile passes it
                  into the Docker build container via '-e CI'.
  USER / USERNAME Developer username for local-build suffixes.  Passed into the
                  Docker build container via '-e USER' in the Makefile.

Usage as a standalone script (for debugging or scripting):
  python scripts/build_version.py          # print the computed suffix to stdout
  python scripts/build_version.py --help   # show this help text

PlatformIO invocation (automatic — do not call directly):
  Declared as 'extra_scripts = pre:scripts/build_version.py' in platformio.ini.
  PlatformIO's SConstruct runner injects Import() and the env object before
  executing this script.
"""

import os
import subprocess
import sys
from datetime import datetime
from typing import Any

__all__ = ["get_git_hash", "compute_suffix"]

_HELP: str = __doc__ or ""


def get_git_hash() -> str:
    """Return the 7-char short git commit hash, or 'unknown' if git is unavailable."""
    try:
        return (
            subprocess.check_output(
                ["git", "rev-parse", "--short", "HEAD"],
                stderr=subprocess.DEVNULL,
            )
            .decode()
            .strip()
        )
    except Exception:
        return "unknown"


def compute_suffix(
    is_ci: bool,
    user: str | None = None,
    date: str | None = None,
    git_hash: str | None = None,
) -> str:
    """Return the BUILD_SUFFIX string for the given build context.

    Args:
        is_ci:    True when building on CI; produces a hash-only suffix.
        user:     Username for local builds.  Defaults to USER / USERNAME env var,
                  then 'dev' if neither is set.
        date:     Build date as YYYYMMDD.  Defaults to today.
        git_hash: Short commit hash.  Defaults to the output of get_git_hash().

    Returns:
        A string beginning with '-', e.g. '-a1b2c3d' or '-justin-20260426-a1b2c3d'.
    """
    if git_hash is None:
        git_hash = get_git_hash()
    if is_ci:
        return f"-{git_hash}"
    if user is None:
        user = os.environ.get("USER") or os.environ.get("USERNAME") or "dev"
    if date is None:
        date = datetime.now().strftime("%Y%m%d")
    return f"-{user}-{date}-{git_hash}"


def _pio_main(env: Any) -> None:
    """Inject BUILD_SUFFIX into the PlatformIO / SCons build environment.

    Called by the module-level bootstrap when running inside PlatformIO.
    Not intended for direct use.
    """
    is_ci = bool(os.environ.get("CI"))
    suffix = compute_suffix(is_ci=is_ci)
    print(f"build_version: VERSION suffix = {suffix}")
    env.Append(CPPDEFINES=[("BUILD_SUFFIX", f'\\"{suffix}\\"')])


def _cli_main() -> None:
    """Entry point when the script is run directly from the command line."""
    if "--help" in sys.argv or "-h" in sys.argv:
        print(_HELP)
        return
    is_ci = bool(os.environ.get("CI"))
    suffix = compute_suffix(is_ci=is_ci)
    print(suffix)


# ── PlatformIO entry point ────────────────────────────────────────────────────
# Import() is a SCons built-in injected into the script's namespace at runtime.
# It is not available in standard Python; the NameError is expected when this
# module is imported outside a PlatformIO/SCons context (tests, CLI invocation).
try:
    Import("env")  # noqa: F821 — SCons built-in, not a standard Python name
    _pio_main(env)  # noqa: F821 — env populated by Import() above
except NameError:
    pass

if __name__ == "__main__":
    _cli_main()
