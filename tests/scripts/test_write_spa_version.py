"""
Tests for scripts/write_spa_version.py — full branch coverage.

Strategy:
  - write_spa_version() is imported and tested directly.
  - Tests cover: CI vs local suffix, missing data/spa/ directory creation,
    correct JSON output format, and the __main__ CLI entry point.
"""

import json
import sys
from pathlib import Path
from unittest.mock import patch

import pytest
import write_spa_version

ROOT = Path(__file__).parent.parent.parent
SCRIPT_PATH = ROOT / "scripts" / "write_spa_version.py"


# ── write_spa_version() ───────────────────────────────────────────────────────


class TestWriteSpaVersion:
    def test_creates_version_json_in_default_location(self, tmp_path, monkeypatch):
        monkeypatch.setenv("CI", "true")
        sketch = tmp_path / "marquee" / "marquee.ino"
        sketch.parent.mkdir(parents=True)
        sketch.write_text('#define BASE_VERSION "3.10.0-wagfam"\n')
        # data/spa/ should be created by the function
        out_file = tmp_path / "data" / "spa" / "version.json"

        with patch("build_version.get_git_hash", return_value="abc1234"):
            result = write_spa_version.write_spa_version(
                repo_root=tmp_path, out_file=out_file
            )

        assert result == "3.10.0-wagfam-abc1234"
        assert out_file.exists()
        data = json.loads(out_file.read_text())
        assert data == {"spa_version": "3.10.0-wagfam-abc1234"}

    def test_creates_parent_directory_if_missing(self, tmp_path, monkeypatch):
        monkeypatch.setenv("CI", "true")
        sketch = tmp_path / "marquee" / "marquee.ino"
        sketch.parent.mkdir(parents=True)
        sketch.write_text('#define BASE_VERSION "1.2.3-wagfam"\n')
        out_file = tmp_path / "deep" / "nested" / "dir" / "version.json"
        assert not out_file.parent.exists()

        with patch("build_version.get_git_hash", return_value="deadbee"):
            write_spa_version.write_spa_version(repo_root=tmp_path, out_file=out_file)

        assert out_file.exists()

    def test_ci_suffix_is_hash_only(self, tmp_path, monkeypatch):
        monkeypatch.setenv("CI", "true")
        sketch = tmp_path / "marquee" / "marquee.ino"
        sketch.parent.mkdir(parents=True)
        sketch.write_text('#define BASE_VERSION "3.10.0-wagfam"\n')
        out_file = tmp_path / "version.json"

        with patch("build_version.get_git_hash", return_value="abc1234"):
            result = write_spa_version.write_spa_version(
                repo_root=tmp_path, out_file=out_file
            )

        assert result == "3.10.0-wagfam-abc1234"

    def test_local_suffix_includes_user_date_hash(self, tmp_path, monkeypatch):
        monkeypatch.delenv("CI", raising=False)
        monkeypatch.setenv("USER", "alice")
        sketch = tmp_path / "marquee" / "marquee.ino"
        sketch.parent.mkdir(parents=True)
        sketch.write_text('#define BASE_VERSION "3.10.0-wagfam"\n')
        out_file = tmp_path / "version.json"

        with patch("build_version.get_git_hash", return_value="abc1234"):
            with patch("build_version.datetime") as mock_dt:
                mock_dt.now.return_value.strftime.return_value = "20260503"
                result = write_spa_version.write_spa_version(
                    repo_root=tmp_path, out_file=out_file
                )

        assert result == "3.10.0-wagfam-alice-20260503-abc1234"
        data = json.loads(out_file.read_text())
        assert data["spa_version"] == "3.10.0-wagfam-alice-20260503-abc1234"

    def test_output_file_ends_with_newline(self, tmp_path, monkeypatch):
        monkeypatch.setenv("CI", "true")
        sketch = tmp_path / "marquee" / "marquee.ino"
        sketch.parent.mkdir(parents=True)
        sketch.write_text('#define BASE_VERSION "3.10.0-wagfam"\n')
        out_file = tmp_path / "version.json"

        with patch("build_version.get_git_hash", return_value="abc1234"):
            write_spa_version.write_spa_version(repo_root=tmp_path, out_file=out_file)

        assert out_file.read_text().endswith("\n")

    def test_uses_real_sketch_when_repo_root_not_given(self, monkeypatch):
        monkeypatch.setenv("CI", "true")
        import re
        out_file = Path(pytest.importorskip("tempfile").mktemp(suffix=".json"))
        try:
            with patch("build_version.get_git_hash", return_value="abc1234"):
                result = write_spa_version.write_spa_version(out_file=out_file)
            assert re.match(r"^\d+\.\d+\.\d+-wagfam-abc1234$", result)
        finally:
            out_file.unlink(missing_ok=True)


# ── CLI entry point ───────────────────────────────────────────────────────────


class TestCliMain:
    def test_main_block_calls_cli_main(self, monkeypatch, capsys):
        monkeypatch.setenv("CI", "true")
        monkeypatch.setattr(sys, "argv", ["write_spa_version.py"])
        # Patch write_spa_version so we don't actually touch data/spa/
        with patch.object(write_spa_version, "write_spa_version", return_value="3.10.0-wagfam-abc1234"):
            write_spa_version._cli_main()
        out = capsys.readouterr().out
        assert "3.10.0-wagfam-abc1234" in out

    def test_module_main_guard_executes_cli_main(self, monkeypatch, capsys):
        """The if __name__ == '__main__' block calls _cli_main()."""
        monkeypatch.setenv("CI", "true")
        monkeypatch.setattr(sys, "argv", ["write_spa_version.py"])
        source = SCRIPT_PATH.read_text()
        # Suppress disk writes; the exec'd write_spa_version() redefines the
        # function in its own scope so we can't patch the module attribute —
        # instead suppress via Path.write_text.
        with patch("pathlib.Path.mkdir"), patch("pathlib.Path.write_text"):
            with patch("build_version.get_git_hash", return_value="abc1234"):
                exec(  # noqa: S102
                    compile(source, str(SCRIPT_PATH), "exec"),
                    {
                        "__builtins__": __builtins__,
                        "__file__": str(SCRIPT_PATH),
                        "__name__": "__main__",
                    },
                )
        out = capsys.readouterr().out
        assert "spa_version" in out
