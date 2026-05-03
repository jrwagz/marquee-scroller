#!/usr/bin/env python3
"""
configure_clocks.py — apply configuration to one or more WagFam CalClock devices

Reads configuration values from a .env file and/or command-line arguments, then
POSTs them to each device's REST API (/api/config).  Only keys explicitly supplied
(via .env or CLI) are included in the request body; unset keys are omitted so
existing device settings are preserved.

.env file format (one KEY=value per line; # comments and blank lines are ignored):

    CLOCK_HOSTS=192.168.1.100,192.168.1.101
    WAGFAM_API_KEY=my-secret-token
    WAGFAM_DATA_URL=https://example.com/calendar.json
    OWM_API_KEY=abc123def456
    GEO_LOCATION=Chicago,US

Supported keys:

    CLOCK_HOSTS       Comma-separated list of device IPs or host names to configure
    WAGFAM_API_KEY    WagFam calendar API key (Authorization token)
    WAGFAM_DATA_URL   WagFam calendar data source URL (HTTPS)
    OWM_API_KEY       OpenWeatherMap API key
    GEO_LOCATION      City name+country, city ID, or "lat,lon"

CLI --host flags override CLOCK_HOSTS from .env; all other CLI arguments
override their corresponding .env values.

Usage examples:

    # Configure clocks listed in scripts/.env CLOCK_HOSTS
    python scripts/configure_clocks.py

    # Override hosts on the command line (replaces CLOCK_HOSTS from .env)
    python scripts/configure_clocks.py --host 192.168.1.100 --host 192.168.1.101

    # Override one config key on the command line
    python scripts/configure_clocks.py --owm-api-key abc123

    # Use a custom .env file location
    python scripts/configure_clocks.py --env ~/secret.env

    # Preview the payload without sending anything
    python scripts/configure_clocks.py --dry-run
"""

import json
import sys
import urllib.error
import urllib.request
from argparse import ArgumentParser, Namespace
from pathlib import Path

__all__ = ["load_dotenv", "resolve_hosts", "build_payload", "apply_config"]

# Default .env location: scripts/.env (same directory as this script).
_DEFAULT_ENV: Path = Path(__file__).parent / ".env"

# Mapping from .env key name → REST API JSON field name.
_ENV_KEY_TO_API_FIELD: dict[str, str] = {
    "WAGFAM_API_KEY": "wagfam_api_key",
    "WAGFAM_DATA_URL": "wagfam_data_url",
    "OWM_API_KEY": "owm_api_key",
    "GEO_LOCATION": "geo_location",
}


def load_dotenv(path: Path | None = None) -> dict[str, str]:
    """Parse a .env file and return its contents as a dictionary.

    Lines are processed as ``KEY=value``.  Leading/trailing whitespace is
    stripped from both key and value.  Lines that are blank or start with ``#``
    (after stripping) are ignored.  Lines without an ``=`` are silently skipped.

    Args:
        path: Path to the .env file.  Defaults to ``scripts/.env`` relative to
              this script.  If the file does not exist, an empty dict is returned
              (not an error — the caller can decide whether missing config is fatal).

    Returns:
        Dict mapping uppercased key names to their string values.
    """
    if path is None:
        path = _DEFAULT_ENV
    path = Path(path)
    if not path.exists():
        return {}
    result: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        result[key.strip().upper()] = value.strip()
    return result


def resolve_hosts(env_vars: dict[str, str], args: Namespace) -> list[str]:
    """Resolve the list of target hosts from CLI args and/or the .env file.

    ``--host`` flags on the command line take full precedence: if any are
    present they are returned as-is and ``CLOCK_HOSTS`` in the .env is
    ignored.  When no ``--host`` flags were given, ``CLOCK_HOSTS`` is parsed
    as a comma-separated list; blank entries are stripped.

    Args:
        env_vars: Dict returned by :func:`load_dotenv`.
        args:     Parsed :class:`~argparse.Namespace` from :func:`_parse_args`.

    Returns:
        Ordered list of host strings.  Empty if neither source supplies a host.
    """
    if args.hosts:
        return list(args.hosts)
    raw = env_vars.get("CLOCK_HOSTS", "")
    return [h.strip() for h in raw.split(",") if h.strip()]


def build_payload(env_vars: dict[str, str], args: Namespace) -> dict[str, str]:
    """Merge .env values with CLI arguments to produce the API request payload.

    CLI arguments take precedence over .env values.  Only keys that are
    non-empty after merging are included in the result (empty strings from
    the .env are treated as unset).

    Args:
        env_vars: Dict returned by :func:`load_dotenv`.
        args:     Parsed :class:`~argparse.Namespace` from :func:`_parse_args`.

    Returns:
        Dict ready to be JSON-serialised and POSTed to ``/api/config``.
    """
    # CLI arg name → .env key name
    cli_to_env: dict[str, str] = {
        "wagfam_api_key": "WAGFAM_API_KEY",
        "wagfam_data_url": "WAGFAM_DATA_URL",
        "owm_api_key": "OWM_API_KEY",
        "geo_location": "GEO_LOCATION",
    }

    payload: dict[str, str] = {}
    for cli_attr, env_key in cli_to_env.items():
        api_field = _ENV_KEY_TO_API_FIELD[env_key]
        # CLI arg wins; fall back to .env; skip if neither is set.
        cli_val: str | None = getattr(args, cli_attr, None)
        if cli_val is not None:
            payload[api_field] = cli_val
        elif env_vars.get(env_key):
            payload[api_field] = env_vars[env_key]
    return payload


def apply_config(
    host: str,
    payload: dict[str, str],
    port: int = 80,
    timeout: int = 10,
) -> tuple[bool, str]:
    """POST the payload to a device's /api/config endpoint.

    Args:
        host:    Device IP address or hostname (no scheme, no trailing slash).
        payload: Dict of API field names to values (from :func:`build_payload`).
        port:    HTTP port the device listens on (default 80).
        timeout: Request timeout in seconds (default 10).

    Returns:
        A ``(success, message)`` tuple where *success* is ``True`` on HTTP 200
        and *message* is a human-readable result string.
    """
    url = f"http://{host}:{port}/api/config"
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            status = resp.status
            text = resp.read().decode("utf-8", errors="replace")
        if status == 200:
            return True, f"OK ({text.strip()})"
        return False, f"HTTP {status}: {text.strip()}"
    except urllib.error.HTTPError as exc:
        body_text = exc.read().decode("utf-8", errors="replace")
        return False, f"HTTP {exc.code}: {body_text.strip()}"
    except urllib.error.URLError as exc:
        return False, f"Connection failed: {exc.reason}"
    except Exception as exc:  # noqa: BLE001
        return False, f"Unexpected error: {exc}"


def _parse_args(argv: list[str] | None = None) -> Namespace:
    parser = ArgumentParser(
        prog="configure_clocks.py",
        description=(
            "Apply configuration to one or more WagFam CalClock devices via the REST API. "
            "Values are read from a .env file and/or command-line arguments; "
            "CLI arguments override .env values."
        ),
        epilog=(
            "Only keys that are explicitly set (via .env or CLI) are sent to the device. "
            "Unset keys are omitted so existing device settings are preserved.\n\n"
            "Example .env file (scripts/.env):\n"
            "  CLOCK_HOSTS=192.168.1.100,192.168.1.101\n"
            "  WAGFAM_API_KEY=my-secret-token\n"
            "  WAGFAM_DATA_URL=https://example.com/calendar.json\n"
            "  OWM_API_KEY=abc123def456\n"
            "  GEO_LOCATION=Chicago,US"
        ),
    )
    parser.add_argument(
        "--host",
        dest="hosts",
        metavar="HOST",
        action="append",
        default=None,
        help=(
            "Device IP address or host name (e.g. 192.168.1.100). "
            "Repeat to configure multiple clocks. "
            "Overrides CLOCK_HOSTS in .env when provided."
        ),
    )
    parser.add_argument(
        "--port",
        type=int,
        default=80,
        metavar="PORT",
        help="HTTP port the device listens on (default: %(default)s).",
    )
    parser.add_argument(
        "--env",
        dest="env_file",
        metavar="FILE",
        default=None,
        help=(
            "Path to the .env file containing sensitive keys "
            f"(default: {_DEFAULT_ENV})."
        ),
    )
    parser.add_argument(
        "--wagfam-api-key",
        dest="wagfam_api_key",
        metavar="KEY",
        default=None,
        help="WagFam calendar API key (overrides WAGFAM_API_KEY in .env).",
    )
    parser.add_argument(
        "--wagfam-data-url",
        dest="wagfam_data_url",
        metavar="URL",
        default=None,
        help="WagFam calendar data source URL (overrides WAGFAM_DATA_URL in .env).",
    )
    parser.add_argument(
        "--owm-api-key",
        dest="owm_api_key",
        metavar="KEY",
        default=None,
        help="OpenWeatherMap API key (overrides OWM_API_KEY in .env).",
    )
    parser.add_argument(
        "--geo-location",
        dest="geo_location",
        metavar="LOCATION",
        default=None,
        help=(
            "City name+country code, city ID, or GPS coordinates "
            '(e.g. "Chicago,US", "4887398", "41.85,-87.65"). '
            "Overrides GEO_LOCATION in .env."
        ),
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=10,
        metavar="SECONDS",
        help="HTTP request timeout in seconds (default: %(default)s).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the payload that would be sent without actually sending it.",
    )
    return parser.parse_args(argv)


def _cli_main(argv: list[str] | None = None) -> int:
    """Entry point for command-line use.

    Returns:
        Exit code: 0 if all devices succeeded, 1 if any device failed.
    """
    args = _parse_args(argv)

    env_path = Path(args.env_file) if args.env_file else None
    env_vars = load_dotenv(env_path)

    hosts = resolve_hosts(env_vars, args)
    if not hosts:
        print(
            "error: no hosts specified — provide --host on the command line "
            "or set CLOCK_HOSTS in the .env file",
            file=sys.stderr,
        )
        return 1

    payload = build_payload(env_vars, args)

    if not payload:
        print(
            "error: no configuration values supplied — provide values via .env "
            "or command-line arguments",
            file=sys.stderr,
        )
        return 1

    if args.dry_run:
        print("Dry run — payload that would be sent:")
        print(json.dumps(payload, indent=2))
        return 0

    any_failed = False
    for host in hosts:
        ok, message = apply_config(host, payload, port=args.port, timeout=args.timeout)
        status_label = "OK  " if ok else "FAIL"
        print(f"[{status_label}] {host}:{args.port} — {message}")
        if not ok:
            any_failed = True

    return 1 if any_failed else 0


if __name__ == "__main__":
    sys.exit(_cli_main())
