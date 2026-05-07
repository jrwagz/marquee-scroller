#!/usr/bin/env python3
"""Helper for run_hw_verify.sh — capture serial output until a marker line.

Replaces `pio device monitor`, which hard-crashes when its stdout isn't a
TTY (`termios.tcgetattr` returns ENOTTY before the serial port is even
opened).  We need to background it from bash and tee to a log file, so a
direct pyserial reader is the right tool for the job.

Usage:
    python3 scripts/_capture_serial.py PORT BAUD TIMEOUT_S LOG_PATH

Behaviour:
    - opens PORT at BAUD, byte-stream read with 1s pyserial timeout
    - writes everything received to LOG_PATH (binary) AND echoes to stdout
      (utf-8 with replacement) so the operator can watch live progress
    - watches for "[ECDSA-TEST] DONE: N/M" or
      "[ECDSA-TEST] (still halted) DONE: N/M"
    - exits 0 on N == M, 2 on N < M, 1 on TIMEOUT_S elapsed without
      seeing the DONE line, 3 on serial-port error
    - SIGINT writes whatever it captured so far and exits 130

Fixture in marquee/HwVerifyTest.cpp re-emits the "(still halted)" summary
every 30s, so the timeout should comfortably exceed 30s if the goal is to
catch a re-emission cycle.  For a fresh-flash run, the first DONE line
shows up within a couple seconds of reset.
"""

from __future__ import annotations

import re
import signal
import sys
import time

DONE_RE = re.compile(rb"\[ECDSA-TEST\] (?:\(still halted\) )?DONE: (\d+)/(\d+)")


def main(argv: list[str]) -> int:
    if len(argv) != 5:
        print(f"usage: {argv[0]} PORT BAUD TIMEOUT_S LOG_PATH", file=sys.stderr)
        return 64
    port, baud_s, timeout_s, log_path = argv[1], argv[2], argv[3], argv[4]
    try:
        baud = int(baud_s)
        timeout = float(timeout_s)
    except ValueError as e:
        print(f"error: bad numeric arg: {e}", file=sys.stderr)
        return 64

    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        print(
            "error: pyserial not available. PlatformIO bundles it; if you're using a "
            "different python3 (try `which python3`), `pip install pyserial`.",
            file=sys.stderr,
        )
        return 3

    try:
        ser = serial.Serial(port, baud, timeout=1)
    except (serial.SerialException, OSError) as e:
        print(f"error: opening {port}@{baud}: {e}", file=sys.stderr)
        return 3

    interrupted = False

    def on_sigint(_signo: int, _frame) -> None:
        nonlocal interrupted
        interrupted = True

    signal.signal(signal.SIGINT, on_sigint)

    print(f"[capture] {port}@{baud} timeout={timeout:.0f}s log={log_path}", flush=True)
    deadline = time.monotonic() + timeout
    bytes_read = 0
    done: tuple[int, int] | None = None
    try:
        with open(log_path, "wb") as fh:
            while time.monotonic() < deadline and not interrupted:
                chunk = ser.read(4096)
                if not chunk:
                    continue
                fh.write(chunk)
                fh.flush()
                bytes_read += len(chunk)
                # Echo to stdout so the user can watch live; replace bytes that
                # aren't valid utf-8 (the postmortem dumps a lot of binary).
                sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                sys.stdout.flush()
                m = DONE_RE.search(chunk)
                if m:
                    done = (int(m.group(1)), int(m.group(2)))
                    # Drain a little more so any trailing context lands in the
                    # log, then exit.
                    drain_until = time.monotonic() + 1.0
                    while time.monotonic() < drain_until and not interrupted:
                        extra = ser.read(4096)
                        if not extra:
                            continue
                        fh.write(extra)
                        fh.flush()
                        bytes_read += len(extra)
                        sys.stdout.write(extra.decode("utf-8", errors="replace"))
                        sys.stdout.flush()
                    break
    finally:
        ser.close()

    elapsed = time.monotonic() - (deadline - timeout)
    print(
        f"\n[capture] read {bytes_read} bytes in {elapsed:.1f}s",
        file=sys.stderr,
        flush=True,
    )
    if interrupted:
        print("[capture] SIGINT — partial log saved", file=sys.stderr)
        return 130
    if done is None:
        print("[capture] TIMEOUT — DONE line not observed", file=sys.stderr)
        return 1
    passes, total = done
    print(f"[capture] DONE: {passes}/{total}", file=sys.stderr)
    return 0 if passes == total else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
