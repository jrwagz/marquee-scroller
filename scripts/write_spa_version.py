"""
write_spa_version.py — write data/spa/version.json before `make buildfs`

Writes a small JSON file into data/spa/ so the LittleFS image carries the
SPA version string.  The device reads this at boot and exposes it via
GET /api/status as "spa_version", enabling the SPA update-detection flow
planned in issue #72.

The version string format mirrors the firmware VERSION macro:

  Local builds:  BASE_VERSION-<username>-<YYYYMMDD>-<hash>
                 e.g. 3.10.0-wagfam-justin-20260503-c0c7879
  CI builds:     BASE_VERSION-<hash>
                 e.g. 3.10.0-wagfam-c0c7879

Environment variables (same as build_version.py):
  CI              Non-empty → CI-style suffix (hash only).
  GIT_HASH        Short commit hash override.
  USER / USERNAME Developer username for local-build suffixes.

Intended usage (called by `make buildfs` before `pio run -t buildfs`):
  python3 scripts/write_spa_version.py

Also callable from CI steps or as a test helper.
"""

import json
import os
import sys
from pathlib import Path

__all__ = ["write_spa_version"]

try:
    _REPO_ROOT: Path | None = Path(__file__).parent.parent
except NameError:
    _REPO_ROOT = None


def write_spa_version(
    repo_root: Path | None = None,
    out_file: Path | None = None,
) -> str:
    """Write data/spa/version.json and return the version string.

    Args:
        repo_root: Root of the repository. Defaults to two directories above
                   this script.
        out_file:  Destination file. Defaults to
                   <repo_root>/data/spa/version.json.

    Returns:
        The full version string that was written, e.g.
        ``"3.10.0-wagfam-c0c7879"`` (CI) or
        ``"3.10.0-wagfam-justin-20260503-c0c7879"`` (local).
    """
    # Import helpers from the sibling build_version.py script.
    # sys.path already contains scripts/ when run from the project root
    # (conftest.py adds it for tests; the Makefile invokes directly from
    # the repo root where relative imports resolve correctly).
    _scripts_dir = Path(__file__).parent
    if str(_scripts_dir) not in sys.path:
        sys.path.insert(0, str(_scripts_dir))
    from build_version import compute_suffix, get_base_version  # noqa: PLC0415

    root = repo_root if repo_root is not None else _REPO_ROOT
    assert root is not None, "repo_root must be set"

    is_ci = bool(os.environ.get("CI"))
    suffix = compute_suffix(is_ci=is_ci)
    base = get_base_version(root / "marquee" / "marquee.ino")
    spa_version = base + suffix

    dest = out_file if out_file is not None else root / "data" / "spa" / "version.json"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps({"spa_version": spa_version}) + "\n")

    return spa_version


def _cli_main() -> None:
    root = _REPO_ROOT
    assert root is not None, "cannot resolve repo root"
    spa_version = write_spa_version(repo_root=root)
    print(f"write_spa_version: spa_version = {spa_version}")


if __name__ == "__main__":
    _cli_main()
