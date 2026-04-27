"""
Tests for scripts/build_version.py — 100% line and branch coverage.

Strategy:
  - Functions (get_git_hash, compute_suffix, get_base_version, _pio_main,
    _cli_main) are imported and tested directly via the `build_version` module.
  - Two exec-based tests cover the two module-level lines that are only reachable
    in special runtime contexts:
      * `_pio_main(env)` inside the try block — only reachable when Import() is
        defined (PlatformIO/SCons context).  Tested by injecting a no-op Import
        and a mock env into the exec namespace.
      * `_cli_main()` inside `if __name__ == "__main__"` — only reachable when
        the script is run directly.  Tested by exec-ing with __name__="__main__".
"""

import subprocess
import sys
from pathlib import Path
from unittest.mock import MagicMock, patch

import build_version
import pytest

ROOT = Path(__file__).parent.parent.parent
SCRIPT_PATH = ROOT / "scripts" / "build_version.py"


# ── get_git_hash ─────────────────────────────────────────────────────────────


class TestGetGitHash:
    def test_returns_hash_from_GIT_HASH_env(self, monkeypatch):
        monkeypatch.setenv("GIT_HASH", "env1234")
        assert build_version.get_git_hash() == "env1234"

    def test_returns_hash_on_success(self, monkeypatch):
        monkeypatch.delenv("GIT_HASH", raising=False)
        with patch("subprocess.check_output", return_value=b"abc1234\n"):
            assert build_version.get_git_hash() == "abc1234"

    def test_returns_unknown_on_subprocess_failure(self, monkeypatch):
        monkeypatch.delenv("GIT_HASH", raising=False)
        with patch(
            "subprocess.check_output",
            side_effect=subprocess.CalledProcessError(1, "git"),
        ):
            assert build_version.get_git_hash() == "unknown"


# ── get_base_version ──────────────────────────────────────────────────────────


class TestGetBaseVersion:
    def test_parses_version_from_sketch(self, tmp_path):
        sketch = tmp_path / "marquee.ino"
        sketch.write_text('#define BASE_VERSION "1.2.3-test"\n')
        assert build_version.get_base_version(sketch_path=sketch) == "1.2.3-test"

    def test_raises_if_define_missing(self, tmp_path):
        sketch = tmp_path / "marquee.ino"
        sketch.write_text("// no version define here\n")
        with pytest.raises(ValueError, match="BASE_VERSION not found"):
            build_version.get_base_version(sketch_path=sketch)

    def test_real_sketch_returns_expected(self):
        assert build_version.get_base_version() == "3.08.0-wagfam"


# ── compute_suffix ────────────────────────────────────────────────────────────


class TestComputeSuffix:
    def test_ci_with_provided_hash(self):
        assert build_version.compute_suffix(is_ci=True, git_hash="abc1234") == "-abc1234"

    def test_ci_calls_get_git_hash_when_hash_omitted(self):
        with patch.object(build_version, "get_git_hash", return_value="deadbee"):
            assert build_version.compute_suffix(is_ci=True) == "-deadbee"

    def test_local_all_args_provided(self):
        result = build_version.compute_suffix(
            is_ci=False, user="alice", date="20260101", git_hash="abc1234"
        )
        assert result == "-alice-20260101-abc1234"

    def test_local_hash_calls_get_git_hash_when_omitted(self):
        with patch.object(build_version, "get_git_hash", return_value="f00cafe"):
            result = build_version.compute_suffix(is_ci=False, user="alice", date="20260101")
        assert result == "-alice-20260101-f00cafe"

    def test_local_user_from_USER_env(self, monkeypatch):
        monkeypatch.setenv("USER", "bob")
        monkeypatch.delenv("USERNAME", raising=False)
        result = build_version.compute_suffix(is_ci=False, date="20260101", git_hash="abc")
        assert result == "-bob-20260101-abc"

    def test_local_user_from_USERNAME_env_when_USER_unset(self, monkeypatch):
        monkeypatch.delenv("USER", raising=False)
        monkeypatch.setenv("USERNAME", "carol")
        result = build_version.compute_suffix(is_ci=False, date="20260101", git_hash="abc")
        assert result == "-carol-20260101-abc"

    def test_local_user_falls_back_to_dev(self, monkeypatch):
        monkeypatch.delenv("USER", raising=False)
        monkeypatch.delenv("USERNAME", raising=False)
        result = build_version.compute_suffix(is_ci=False, date="20260101", git_hash="abc")
        assert result == "-dev-20260101-abc"

    def test_local_date_defaults_to_today(self):
        with patch("build_version.datetime") as mock_dt:
            mock_dt.now.return_value.strftime.return_value = "20990101"
            result = build_version.compute_suffix(is_ci=False, user="alice", git_hash="abc")
        assert result == "-alice-20990101-abc"


# ── _pio_main ─────────────────────────────────────────────────────────────────


class TestPioMain:
    def _get_cppdefines(self, mock_env):
        """Extract CPPDEFINES from the first env.Append call."""
        mock_env.Append.assert_called_once()
        return mock_env.Append.call_args.kwargs["CPPDEFINES"]

    def test_ci_mode_injects_hash_only_suffix(self, monkeypatch, tmp_path):
        monkeypatch.setenv("CI", "true")
        mock_env = MagicMock()
        output_file = tmp_path / "VERSION.txt"
        with patch.object(build_version, "get_git_hash", return_value="abc1234"):
            build_version._pio_main(mock_env, output_file=output_file)
        [(key, val)] = self._get_cppdefines(mock_env)
        assert key == "BUILD_SUFFIX"
        assert val == '\\"-abc1234\\"'
        assert output_file.read_text() == "3.08.0-wagfam-abc1234"

    def test_local_mode_injects_user_date_hash_suffix(self, monkeypatch, tmp_path):
        monkeypatch.delenv("CI", raising=False)
        monkeypatch.setenv("USER", "tester")
        mock_env = MagicMock()
        output_file = tmp_path / "VERSION.txt"
        with patch.object(build_version, "get_git_hash", return_value="abc1234"):
            with patch("build_version.datetime") as mock_dt:
                mock_dt.now.return_value.strftime.return_value = "20260101"
                build_version._pio_main(mock_env, output_file=output_file)
        [(key, val)] = self._get_cppdefines(mock_env)
        assert key == "BUILD_SUFFIX"
        assert "tester" in val
        assert "20260101" in val
        assert "abc1234" in val
        assert output_file.read_text() == "3.08.0-wagfam-tester-20260101-abc1234"


# ── _cli_main ─────────────────────────────────────────────────────────────────


class TestCliMain:
    def test_help_long_flag_prints_docstring(self, monkeypatch, capsys):
        monkeypatch.setattr(sys, "argv", ["build_version.py", "--help"])
        build_version._cli_main()
        assert "BUILD_SUFFIX" in capsys.readouterr().out

    def test_help_short_flag_prints_docstring(self, monkeypatch, capsys):
        monkeypatch.setattr(sys, "argv", ["build_version.py", "-h"])
        build_version._cli_main()
        assert "BUILD_SUFFIX" in capsys.readouterr().out

    def test_ci_mode_prints_hash_only_suffix(self, monkeypatch, capsys):
        monkeypatch.setattr(sys, "argv", ["build_version.py"])
        monkeypatch.setenv("CI", "true")
        with patch.object(build_version, "get_git_hash", return_value="abc1234"):
            build_version._cli_main()
        output = capsys.readouterr().out.strip()
        assert output == "-abc1234"

    def test_local_mode_prints_user_date_hash_suffix(self, monkeypatch, capsys):
        monkeypatch.setattr(sys, "argv", ["build_version.py"])
        monkeypatch.delenv("CI", raising=False)
        monkeypatch.setenv("USER", "tester")
        with patch.object(build_version, "get_git_hash", return_value="abc1234"):
            with patch("build_version.datetime") as mock_dt:
                mock_dt.now.return_value.strftime.return_value = "20260101"
                build_version._cli_main()
        output = capsys.readouterr().out.strip()
        assert output == "-tester-20260101-abc1234"


# ── Module-level bootstrap (exec-based) ──────────────────────────────────────


class TestModuleBootstrap:
    def _exec_script(self, globs: dict) -> None:
        """Compile and exec build_version.py in the given globals namespace."""
        source = SCRIPT_PATH.read_text()
        exec(compile(source, str(SCRIPT_PATH), "exec"), globs)  # noqa: S102

    def test_pio_entry_point_calls_pio_main(self, monkeypatch):
        """The try block calls _pio_main(env) when Import() is defined (SCons context)."""
        monkeypatch.setenv("CI", "true")
        mock_env = MagicMock()
        # Inject Import() as a no-op; env is pre-populated in the namespace.
        # Patch out filesystem writes so the test has no side effects.
        with patch("pathlib.Path.mkdir"), patch("pathlib.Path.write_text"):
            self._exec_script(
                {
                    "__builtins__": __builtins__,
                    "__file__": str(SCRIPT_PATH),
                    "__name__": "pio_script",  # prevent __main__ block from running
                    "env": mock_env,
                    "Import": lambda _: None,
                }
            )
        mock_env.Append.assert_called_once()
        [(key, val)] = mock_env.Append.call_args.kwargs["CPPDEFINES"]
        assert key == "BUILD_SUFFIX"
        assert val.startswith('\\"') and val.endswith('\\"')

    def test_main_block_calls_cli_main(self, monkeypatch, capsys):
        """The if __name__ == '__main__' guard calls _cli_main() when run as a script."""
        monkeypatch.setenv("CI", "true")
        monkeypatch.setattr(sys, "argv", ["build_version.py"])
        # No Import defined → NameError → except branch; __name__ == "__main__" → _cli_main().
        self._exec_script(
            {
                "__builtins__": __builtins__,
                "__file__": str(SCRIPT_PATH),
                "__name__": "__main__",
            }
        )
        output = capsys.readouterr().out
        suffix_lines = [ln for ln in output.splitlines() if ln.startswith("-")]
        assert len(suffix_lines) == 1
