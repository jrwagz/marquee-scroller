"""
Regression test: `make uploadfs` must run scripts/write_spa_version.py
before invoking pio's uploadfs target.

Why this exists:
  PR #62 added `make uploadfs` as a thin wrapper around `pio run --target
  uploadfs`. PR #74 added scripts/write_spa_version.py, hooked into
  `make buildfs` so the LittleFS image embeds a version string read at
  boot and exposed via /api/status.spa_version.

  But `make uploadfs` was never updated to mirror that hook — so flashing
  via `make uploadfs` (or `pio run --target uploadfs` directly) ships a
  LittleFS image without /spa/version.json, and the device reports
  spa_version="unknown" until someone manually uploads the file. Caught
  during a hardware test on 2026-05-03.

  Pin the contract here so a future refactor of the makefile can't
  silently regress.
"""

import re
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent.parent
MAKEFILE = ROOT / "makefile"


@pytest.fixture(scope="module")
def uploadfs_recipe() -> str:
    """Return the body of the `uploadfs` rule (everything between the
    target line and the next blank line / next rule)."""
    src = MAKEFILE.read_text(encoding="utf-8")
    match = re.search(
        r"^uploadfs:.*?(?=\n\.PHONY|\n[a-zA-Z][a-zA-Z0-9_/-]*:|\n\Z)",
        src,
        re.MULTILINE | re.DOTALL,
    )
    assert match, "could not locate `uploadfs` rule in makefile"
    return match.group(0)


def test_uploadfs_runs_write_spa_version(uploadfs_recipe: str) -> None:
    """The recipe must invoke write_spa_version.py somewhere before pio
    runs. Otherwise /spa/version.json is missing from the flashed image
    and /api/status reports spa_version='unknown'."""
    assert "write_spa_version" in uploadfs_recipe, (
        "make uploadfs must run scripts/write_spa_version.py to mirror "
        "the same step in `make buildfs`. Without it, the LittleFS image "
        "ships without /spa/version.json and the device falls back to "
        "spa_version='unknown'."
    )


def test_uploadfs_runs_version_script_before_pio(uploadfs_recipe: str) -> None:
    """Order matters: the version file has to land in data/spa/ *before*
    pio packs the LittleFS image. If the script runs after pio, the
    version is on disk locally but not in the flashed bytes."""
    spa_pos = uploadfs_recipe.find("write_spa_version")
    pio_pos = uploadfs_recipe.find("pio run")
    assert spa_pos != -1 and pio_pos != -1
    assert spa_pos < pio_pos, (
        "write_spa_version.py must run BEFORE the pio uploadfs invocation "
        "so the version.json is in data/spa/ when pio packs the image"
    )


def test_uploadfs_dry_run_includes_version_script() -> None:
    """End-to-end: `make -n uploadfs` should print the script invocation.
    Catches makefile-syntax issues that the source-string match above
    can't (e.g. the line is conditional, in a sub-make, or commented out)."""
    result = subprocess.run(
        ["make", "-n", "uploadfs"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, (
        f"make -n uploadfs failed: stderr={result.stderr}"
    )
    assert "write_spa_version.py" in result.stdout, (
        "make -n uploadfs should expand the recipe to include "
        f"write_spa_version.py. Output was:\n{result.stdout}"
    )
