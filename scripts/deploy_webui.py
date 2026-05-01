#!/usr/bin/env python3
"""
deploy_webui.py — push the built SPA bundle (data/spa/) to a clock device
via /api/fs/upload, file by file.

This is the OTA-friendly alternative to `pio run --target uploadfs`, which
wipes the entire LittleFS partition (including /conf.txt). With this script
we keep config + OTA state intact and just refresh the SPA assets.

Usage:
    python3 scripts/deploy_webui.py http://<device-ip> [--password PW]
                                    [--source data/spa] [--dest-prefix /spa]
                                    [--skip-unchanged]

Auth: HTTP Basic with username "admin". Password may be given via --password,
the WAGFAM_CLOCK_PW environment variable, or stdin if neither is set (the
default; the script prompts and reads without echo).

The script walks `--source` recursively. Each file maps to a target path on
the device by relativizing against `--source` and prepending `--dest-prefix`.
For example, with the defaults, `data/spa/assets/index.js.gz` uploads to
`/spa/assets/index.js.gz`.

Stdlib-only — no requests/httpx dependency. Multipart bodies are assembled
by hand because urllib doesn't provide a high-level helper.
"""

from __future__ import annotations

import argparse
import base64
import getpass
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path


def build_multipart_body(filename: str, content: bytes) -> tuple[bytes, str]:
    """
    Return (body, content_type). One-file multipart/form-data with the
    bytes carried verbatim — no encoding, suitable for binary files.
    """
    boundary = f"----marquee-deploy-{uuid.uuid4().hex}"
    pre = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode("utf-8")
    post = f"\r\n--{boundary}--\r\n".encode("utf-8")
    return pre + content + post, f"multipart/form-data; boundary={boundary}"


def basic_auth_header(password: str) -> str:
    creds = base64.b64encode(f"admin:{password}".encode("utf-8")).decode("ascii")
    return f"Basic {creds}"


def fetch_existing(host: str, password: str) -> dict[str, int]:
    """
    Return {path: size} for files already on the device, via /api/fs/list.
    Used by --skip-unchanged. On failure, returns {} (best-effort).
    """
    url = f"{host.rstrip('/')}/api/fs/list"
    req = urllib.request.Request(url, headers={"Authorization": basic_auth_header(password)})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read())
        return {entry["name"]: entry["size"] for entry in data.get("files", [])}
    except (urllib.error.URLError, urllib.error.HTTPError, json.JSONDecodeError) as e:
        print(f"  warn: /api/fs/list failed ({e}); will not skip any files", file=sys.stderr)
        return {}


def upload_one(host: str, password: str, dest_path: str, content: bytes) -> dict:
    """POST one file to /api/fs/upload?path=<dest_path>. Returns parsed JSON response."""
    url = f"{host.rstrip('/')}/api/fs/upload?path={urllib.parse.quote(dest_path)}"
    body, content_type = build_multipart_body(Path(dest_path).name, content)
    req = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={
            "Authorization": basic_auth_header(password),
            "X-Requested-With": "XMLHttpRequest",
            "Content-Type": content_type,
            "Content-Length": str(len(body)),
        },
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read())


def discover_files(source_dir: Path, dest_prefix: str) -> list[tuple[Path, str]]:
    """Walk source_dir, return [(local_path, device_path), ...]."""
    pairs: list[tuple[Path, str]] = []
    for local in sorted(source_dir.rglob("*")):
        if not local.is_file():
            continue
        rel = local.relative_to(source_dir)
        device_path = f"{dest_prefix.rstrip('/')}/{rel.as_posix()}"
        pairs.append((local, device_path))
    return pairs


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", help="Device URL, e.g. http://192.168.168.66")
    p.add_argument("--password", help="admin password (or set WAGFAM_CLOCK_PW)")
    p.add_argument("--source", default="data/spa", type=Path, help="Local source dir (default: data/spa)")
    p.add_argument("--dest-prefix", default="/spa", help="On-device path prefix (default: /spa)")
    p.add_argument("--skip-unchanged", action="store_true",
                   help="Skip files where on-device size matches local size")
    args = p.parse_args(argv)

    password = args.password or os.environ.get("WAGFAM_CLOCK_PW") or getpass.getpass("Clock password: ")

    if not args.source.is_dir():
        print(f"error: source dir {args.source} does not exist", file=sys.stderr)
        return 2

    pairs = discover_files(args.source, args.dest_prefix)
    if not pairs:
        print(f"error: no files found in {args.source}", file=sys.stderr)
        return 2

    existing: dict[str, int] = {}
    if args.skip_unchanged:
        existing = fetch_existing(args.host, password)

    total_bytes = 0
    uploaded = 0
    skipped = 0
    failed = 0
    for local, device_path in pairs:
        content = local.read_bytes()
        if args.skip_unchanged and existing.get(device_path) == len(content):
            print(f"  skip {device_path:50s}  ({len(content):6d} B, unchanged)")
            skipped += 1
            continue
        try:
            result = upload_one(args.host, password, device_path, content)
        except urllib.error.HTTPError as e:
            print(f"  fail {device_path:50s}  HTTP {e.code} {e.reason}")
            failed += 1
            continue
        except urllib.error.URLError as e:
            print(f"  fail {device_path:50s}  {e.reason}")
            failed += 1
            continue
        size = int(result.get("size", -1))
        print(f"  ok   {device_path:50s}  ({size:6d} B)")
        uploaded += 1
        total_bytes += size

    print()
    print(f"  {uploaded} uploaded ({total_bytes} bytes), {skipped} skipped, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
