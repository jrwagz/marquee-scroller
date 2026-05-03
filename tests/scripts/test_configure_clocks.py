"""
Tests for scripts/configure_clocks.py — 100% line and branch coverage.

Strategy:
  - load_dotenv, build_payload, apply_config, _parse_args, and _cli_main are
    imported and tested directly.
  - HTTP calls in apply_config are fully mocked via unittest.mock.patch so no
    live network is required.
  - _cli_main is exercised end-to-end with mocked apply_config to cover the
    success, partial-failure, no-payload-supplied, and dry-run paths.
"""

import io
import json
import sys
import urllib.error
import urllib.request
from argparse import Namespace
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

import configure_clocks

ROOT = Path(__file__).parent.parent.parent
SCRIPT_PATH = ROOT / "scripts" / "configure_clocks.py"


# ── load_dotenv ───────────────────────────────────────────────────────────────


class TestLoadDotenv:
    def test_returns_empty_dict_when_file_missing(self, tmp_path):
        result = configure_clocks.load_dotenv(tmp_path / "nonexistent.env")
        assert result == {}

    def test_uses_default_path_when_none(self, tmp_path, monkeypatch):
        # Patch the module-level default so we control the file location.
        env_file = tmp_path / ".env"
        env_file.write_text("OWM_API_KEY=testkey\n")
        monkeypatch.setattr(configure_clocks, "_DEFAULT_ENV", env_file)
        assert configure_clocks.load_dotenv() == {"OWM_API_KEY": "testkey"}

    def test_parses_basic_key_value(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("WAGFAM_API_KEY=secret\n")
        assert configure_clocks.load_dotenv(env) == {"WAGFAM_API_KEY": "secret"}

    def test_parses_multiple_keys(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text(
            "WAGFAM_API_KEY=key1\n"
            "OWM_API_KEY=key2\n"
            "GEO_LOCATION=Chicago,US\n"
        )
        assert configure_clocks.load_dotenv(env) == {
            "WAGFAM_API_KEY": "key1",
            "OWM_API_KEY": "key2",
            "GEO_LOCATION": "Chicago,US",
        }

    def test_strips_whitespace_from_key_and_value(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("  OWM_API_KEY  =  spaced-value  \n")
        assert configure_clocks.load_dotenv(env) == {"OWM_API_KEY": "spaced-value"}

    def test_uppercases_keys(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("owm_api_key=lowercase\n")
        assert configure_clocks.load_dotenv(env) == {"OWM_API_KEY": "lowercase"}

    def test_skips_blank_lines(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("\n\nOWM_API_KEY=val\n\n")
        assert configure_clocks.load_dotenv(env) == {"OWM_API_KEY": "val"}

    def test_skips_comment_lines(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("# this is a comment\nOWM_API_KEY=val\n")
        assert configure_clocks.load_dotenv(env) == {"OWM_API_KEY": "val"}

    def test_skips_lines_without_equals(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("NOEQUALS\nOWM_API_KEY=val\n")
        assert configure_clocks.load_dotenv(env) == {"OWM_API_KEY": "val"}

    def test_value_may_contain_equals(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("WAGFAM_DATA_URL=https://example.com/path?a=1\n")
        assert configure_clocks.load_dotenv(env) == {
            "WAGFAM_DATA_URL": "https://example.com/path?a=1"
        }

    def test_empty_file_returns_empty_dict(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("")
        assert configure_clocks.load_dotenv(env) == {}


# ── build_payload ─────────────────────────────────────────────────────────────


def _args(**kwargs) -> Namespace:
    """Return a Namespace with all config attrs defaulted to None."""
    defaults = {
        "wagfam_api_key": None,
        "wagfam_data_url": None,
        "owm_api_key": None,
        "geo_location": None,
    }
    defaults.update(kwargs)
    return Namespace(**defaults)


class TestBuildPayload:
    def test_empty_env_and_no_cli_gives_empty_payload(self):
        assert configure_clocks.build_payload({}, _args()) == {}

    def test_env_value_included_in_payload(self):
        payload = configure_clocks.build_payload({"OWM_API_KEY": "abc"}, _args())
        assert payload == {"owm_api_key": "abc"}

    def test_all_env_values_mapped_to_api_fields(self):
        env = {
            "WAGFAM_API_KEY": "wk",
            "WAGFAM_DATA_URL": "https://example.com/cal.json",
            "OWM_API_KEY": "ok",
            "GEO_LOCATION": "Chicago,US",
        }
        payload = configure_clocks.build_payload(env, _args())
        assert payload == {
            "wagfam_api_key": "wk",
            "wagfam_data_url": "https://example.com/cal.json",
            "owm_api_key": "ok",
            "geo_location": "Chicago,US",
        }

    def test_cli_arg_overrides_env_value(self):
        env = {"OWM_API_KEY": "from-env"}
        args = _args(owm_api_key="from-cli")
        payload = configure_clocks.build_payload(env, args)
        assert payload["owm_api_key"] == "from-cli"

    def test_cli_arg_used_when_env_missing(self):
        args = _args(geo_location="London,GB")
        payload = configure_clocks.build_payload({}, args)
        assert payload == {"geo_location": "London,GB"}

    def test_empty_string_env_value_excluded(self):
        # An empty string in .env should be treated as "not set".
        payload = configure_clocks.build_payload({"OWM_API_KEY": ""}, _args())
        assert "owm_api_key" not in payload

    def test_cli_none_falls_back_to_env(self):
        env = {"WAGFAM_API_KEY": "env-key"}
        args = _args(wagfam_api_key=None)
        payload = configure_clocks.build_payload(env, args)
        assert payload["wagfam_api_key"] == "env-key"

    def test_partial_keys_only_sends_what_is_set(self):
        env = {"OWM_API_KEY": "ok"}
        args = _args(geo_location="NYC")
        payload = configure_clocks.build_payload(env, args)
        assert set(payload.keys()) == {"owm_api_key", "geo_location"}


# ── apply_config ──────────────────────────────────────────────────────────────


class TestApplyConfig:
    def _mock_response(self, status: int, body: str):
        """Return a mock context-manager response with given status + body."""
        mock_resp = MagicMock()
        mock_resp.status = status
        mock_resp.read.return_value = body.encode("utf-8")
        mock_resp.__enter__ = lambda s: s
        mock_resp.__exit__ = MagicMock(return_value=False)
        return mock_resp

    def test_success_returns_true(self):
        resp = self._mock_response(200, '{"status":"ok"}')
        with patch("urllib.request.urlopen", return_value=resp):
            ok, msg = configure_clocks.apply_config("192.168.1.1", {"owm_api_key": "x"})
        assert ok is True
        assert "OK" in msg

    def test_non_200_returns_false(self):
        resp = self._mock_response(500, "Internal Server Error")
        with patch("urllib.request.urlopen", return_value=resp):
            ok, msg = configure_clocks.apply_config("192.168.1.1", {})
        assert ok is False
        assert "500" in msg

    def test_http_error_returns_false(self):
        exc = urllib.error.HTTPError(
            url="http://x", code=403, msg="Forbidden",
            hdrs=None, fp=io.BytesIO(b"forbidden body"),
        )
        with patch("urllib.request.urlopen", side_effect=exc):
            ok, msg = configure_clocks.apply_config("192.168.1.1", {})
        assert ok is False
        assert "403" in msg

    def test_url_error_returns_false(self):
        exc = urllib.error.URLError(reason="Connection refused")
        with patch("urllib.request.urlopen", side_effect=exc):
            ok, msg = configure_clocks.apply_config("192.168.1.1", {})
        assert ok is False
        assert "Connection failed" in msg

    def test_unexpected_exception_returns_false(self):
        with patch("urllib.request.urlopen", side_effect=RuntimeError("boom")):
            ok, msg = configure_clocks.apply_config("192.168.1.1", {})
        assert ok is False
        assert "Unexpected error" in msg

    def test_request_url_includes_host_and_port(self):
        resp = self._mock_response(200, "{}")
        captured: list = []

        def fake_urlopen(req, timeout):
            captured.append(req.full_url)
            return resp

        with patch("urllib.request.urlopen", side_effect=fake_urlopen):
            configure_clocks.apply_config("10.0.0.5", {}, port=8080)
        assert captured[0] == "http://10.0.0.5:8080/api/config"

    def test_payload_is_json_encoded_in_body(self):
        resp = self._mock_response(200, "{}")
        captured: list = []

        def fake_urlopen(req, timeout):
            captured.append(req.data)
            return resp

        payload = {"owm_api_key": "testkey", "geo_location": "Chicago,US"}
        with patch("urllib.request.urlopen", side_effect=fake_urlopen):
            configure_clocks.apply_config("192.168.1.1", payload)
        sent = json.loads(captured[0])
        assert sent == payload


# ── resolve_hosts ─────────────────────────────────────────────────────────────


def _args_hosts(**kwargs) -> Namespace:
    """Return a Namespace with hosts defaulted to None."""
    defaults = {"hosts": None}
    defaults.update(kwargs)
    return Namespace(**defaults)


class TestResolveHosts:
    def test_cli_hosts_returned_directly(self):
        args = _args_hosts(hosts=["192.168.1.1", "192.168.1.2"])
        assert configure_clocks.resolve_hosts({}, args) == ["192.168.1.1", "192.168.1.2"]

    def test_clock_hosts_from_env_single(self):
        env = {"CLOCK_HOSTS": "192.168.1.100"}
        assert configure_clocks.resolve_hosts(env, _args_hosts()) == ["192.168.1.100"]

    def test_clock_hosts_from_env_multiple(self):
        env = {"CLOCK_HOSTS": "192.168.1.100,192.168.1.101"}
        assert configure_clocks.resolve_hosts(env, _args_hosts()) == [
            "192.168.1.100", "192.168.1.101"
        ]

    def test_clock_hosts_strips_whitespace(self):
        env = {"CLOCK_HOSTS": " 10.0.0.1 , 10.0.0.2 "}
        assert configure_clocks.resolve_hosts(env, _args_hosts()) == ["10.0.0.1", "10.0.0.2"]

    def test_clock_hosts_skips_blank_entries(self):
        env = {"CLOCK_HOSTS": "10.0.0.1,,10.0.0.2"}
        assert configure_clocks.resolve_hosts(env, _args_hosts()) == ["10.0.0.1", "10.0.0.2"]

    def test_cli_overrides_env_clock_hosts(self):
        env = {"CLOCK_HOSTS": "192.168.1.99"}
        args = _args_hosts(hosts=["10.0.0.1"])
        assert configure_clocks.resolve_hosts(env, args) == ["10.0.0.1"]

    def test_empty_env_and_no_cli_returns_empty(self):
        assert configure_clocks.resolve_hosts({}, _args_hosts()) == []

    def test_empty_clock_hosts_value_returns_empty(self):
        assert configure_clocks.resolve_hosts({"CLOCK_HOSTS": ""}, _args_hosts()) == []

    def test_whitespace_only_clock_hosts_returns_empty(self):
        assert configure_clocks.resolve_hosts({"CLOCK_HOSTS": "  ,  "}, _args_hosts()) == []


# ── _parse_args ───────────────────────────────────────────────────────────────


class TestParseArgs:
    def test_no_host_is_accepted_by_parser(self):
        # --host is now optional; hosts are resolved later via resolve_hosts.
        args = configure_clocks._parse_args([])
        assert args.hosts is None

    def test_single_host(self):
        args = configure_clocks._parse_args(["--host", "192.168.1.1"])
        assert args.hosts == ["192.168.1.1"]

    def test_multiple_hosts(self):
        args = configure_clocks._parse_args(
            ["--host", "192.168.1.1", "--host", "192.168.1.2"]
        )
        assert args.hosts == ["192.168.1.1", "192.168.1.2"]

    def test_default_port(self):
        args = configure_clocks._parse_args([])
        assert args.port == 80

    def test_custom_port(self):
        args = configure_clocks._parse_args(["--port", "8080"])
        assert args.port == 8080

    def test_default_env_file_is_none(self):
        args = configure_clocks._parse_args([])
        assert args.env_file is None

    def test_env_file_stored(self):
        args = configure_clocks._parse_args(["--env", "/tmp/k.env"])
        assert args.env_file == "/tmp/k.env"

    def test_all_config_flags(self):
        args = configure_clocks._parse_args([
            "--wagfam-api-key", "wk",
            "--wagfam-data-url", "https://x.com/c.json",
            "--owm-api-key", "ok",
            "--geo-location", "Chicago,US",
        ])
        assert args.wagfam_api_key == "wk"
        assert args.wagfam_data_url == "https://x.com/c.json"
        assert args.owm_api_key == "ok"
        assert args.geo_location == "Chicago,US"

    def test_dry_run_flag(self):
        args = configure_clocks._parse_args(["--dry-run"])
        assert args.dry_run is True

    def test_dry_run_default_false(self):
        args = configure_clocks._parse_args([])
        assert args.dry_run is False

    def test_timeout_default(self):
        args = configure_clocks._parse_args([])
        assert args.timeout == 10

    def test_timeout_custom(self):
        args = configure_clocks._parse_args(["--timeout", "30"])
        assert args.timeout == 30


# ── _cli_main ─────────────────────────────────────────────────────────────────


class TestCliMain:
    """End-to-end tests for _cli_main with HTTP mocked out."""

    def _make_apply_mock(self, ok: bool, message: str = "OK"):
        return patch(
            "configure_clocks.apply_config",
            return_value=(ok, message),
        )

    def test_success_returns_exit_0(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=testkey\n")
        with self._make_apply_mock(True, "OK"):
            rc = configure_clocks._cli_main([
                "--host", "192.168.1.1",
                "--env", str(env),
            ])
        assert rc == 0

    def test_failed_host_returns_exit_1(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=testkey\n")
        with self._make_apply_mock(False, "Connection refused"):
            rc = configure_clocks._cli_main([
                "--host", "192.168.1.1",
                "--env", str(env),
            ])
        assert rc == 1

    def test_partial_failure_returns_exit_1(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=testkey\n")
        results = [(True, "OK"), (False, "timeout")]
        with patch("configure_clocks.apply_config", side_effect=results):
            rc = configure_clocks._cli_main([
                "--host", "192.168.1.1",
                "--host", "192.168.1.2",
                "--env", str(env),
            ])
        assert rc == 1

    def test_all_hosts_succeed_returns_exit_0(self, tmp_path):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=testkey\n")
        with patch("configure_clocks.apply_config", return_value=(True, "OK")):
            rc = configure_clocks._cli_main([
                "--host", "192.168.1.1",
                "--host", "192.168.1.2",
                "--env", str(env),
            ])
        assert rc == 0

    def test_hosts_from_env_clock_hosts(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("CLOCK_HOSTS=192.168.1.50\nOWM_API_KEY=k\n")
        captured_hosts: list = []

        def fake_apply(host, payload, **kwargs):
            captured_hosts.append(host)
            return True, "OK"

        with patch("configure_clocks.apply_config", side_effect=fake_apply):
            rc = configure_clocks._cli_main(["--env", str(env)])
        assert rc == 0
        assert captured_hosts == ["192.168.1.50"]

    def test_cli_host_overrides_env_clock_hosts(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("CLOCK_HOSTS=192.168.1.99\nOWM_API_KEY=k\n")
        captured_hosts: list = []

        def fake_apply(host, payload, **kwargs):
            captured_hosts.append(host)
            return True, "OK"

        with patch("configure_clocks.apply_config", side_effect=fake_apply):
            configure_clocks._cli_main(["--host", "10.0.0.1", "--env", str(env)])
        assert captured_hosts == ["10.0.0.1"]

    def test_no_hosts_returns_exit_1_with_message(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=k\n")
        rc = configure_clocks._cli_main(["--env", str(env)])
        assert rc == 1
        assert "no hosts" in capsys.readouterr().err

    def test_no_payload_returns_exit_1_with_message(self, tmp_path, capsys):
        # Use an empty .env so no values leak in from a real scripts/.env on disk.
        env = tmp_path / ".env"
        env.write_text("")
        rc = configure_clocks._cli_main(["--host", "192.168.1.1", "--env", str(env)])
        assert rc == 1
        assert "no configuration values" in capsys.readouterr().err

    def test_dry_run_prints_payload_and_returns_0(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=drykey\n")
        rc = configure_clocks._cli_main([
            "--host", "192.168.1.1",
            "--env", str(env),
            "--dry-run",
        ])
        assert rc == 0
        out = capsys.readouterr().out
        assert "Dry run" in out
        assert "owm_api_key" in out
        assert "drykey" in out

    def test_cli_arg_overrides_env_in_full_flow(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=env-key\n")
        captured_payload: list = []

        def fake_apply(host, payload, **kwargs):
            captured_payload.append(payload)
            return True, "OK"

        with patch("configure_clocks.apply_config", side_effect=fake_apply):
            configure_clocks._cli_main([
                "--host", "192.168.1.1",
                "--env", str(env),
                "--owm-api-key", "cli-key",
            ])
        assert captured_payload[0]["owm_api_key"] == "cli-key"

    def test_missing_env_file_falls_through_gracefully(self, tmp_path, capsys):
        # If the .env doesn't exist but a CLI arg is provided, should still work.
        with patch("configure_clocks.apply_config", return_value=(True, "OK")):
            rc = configure_clocks._cli_main([
                "--host", "192.168.1.1",
                "--env", str(tmp_path / "nonexistent.env"),
                "--owm-api-key", "cli-only",
            ])
        assert rc == 0

    def test_output_includes_host_and_status(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=k\n")
        with patch("configure_clocks.apply_config", return_value=(True, "OK")):
            configure_clocks._cli_main([
                "--host", "10.0.0.1",
                "--env", str(env),
            ])
        out = capsys.readouterr().out
        assert "10.0.0.1" in out
        assert "OK" in out

    def test_failure_output_includes_fail_label(self, tmp_path, capsys):
        env = tmp_path / ".env"
        env.write_text("OWM_API_KEY=k\n")
        with patch("configure_clocks.apply_config", return_value=(False, "refused")):
            configure_clocks._cli_main([
                "--host", "10.0.0.1",
                "--env", str(env),
            ])
        out = capsys.readouterr().out
        assert "FAIL" in out


# ── module-level __main__ bootstrap ──────────────────────────────────────────


class TestModuleBootstrap:
    def test_main_block_calls_cli_main(self, tmp_path):
        """Exec the script with __name__='__main__' to cover the if __name__ guard."""
        source = SCRIPT_PATH.read_text()
        resp = MagicMock()
        resp.status = 200
        resp.read.return_value = b'{"status":"ok"}'
        resp.__enter__ = lambda s: s
        resp.__exit__ = MagicMock(return_value=False)
        argv = ["configure_clocks.py", "--host", "192.168.1.1", "--owm-api-key", "k"]
        with patch.object(sys, "argv", argv):
            with patch("urllib.request.urlopen", return_value=resp):
                with pytest.raises(SystemExit) as exc_info:
                    exec(  # noqa: S102
                        compile(source, str(SCRIPT_PATH), "exec"),
                        {"__name__": "__main__", "__file__": str(SCRIPT_PATH)},
                    )
        assert exc_info.value.code == 0
