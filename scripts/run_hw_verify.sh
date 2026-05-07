#!/usr/bin/env bash
#
# One-command runner for the on-device ECDSA-P256 verify test (PR #100).
# Build → flash → monitor → grep → exit 0 iff all 8 cases pass.
#
# Usage:
#   ./scripts/run_hw_verify.sh [SERIAL_PORT]
#   ./scripts/run_hw_verify.sh --dry-run             # build only, no flash/monitor
#   ./scripts/run_hw_verify.sh /dev/cu.usbserial-XX  # explicit port
#
# If SERIAL_PORT is omitted, the script auto-detects exactly one CH340 /
# CH9102 / CP2102 / FTDI USB-serial port. Multiple matches → error with the
# list (you have to pick); zero matches → error with troubleshooting tips.
#
# The captured log is saved to scripts/test_logs/ecdsa-verify-<ts>.log
# (gitignored). The script tails it live, exits 0 when it sees a fully-
# passing "[ECDSA-TEST] DONE: N/N passed" line, exits 1 if anything failed
# or the test didn't finish within the timeout.
#
# Pairs with: marquee/HwVerifyTest.cpp, scripts/sign_test_payloads.py,
# docs/HARDWARE_TEST_ECDSA_VERIFY.md.

set -u
set -o pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$REPO_ROOT/scripts/test_logs"
TIMEOUT_S=60          # how long to wait for the DONE line after flash completes
MONITOR_BAUD=115200
ENV_NAME="dev"        # build env (extends default, disables auto-update)

DRY_RUN=0
PORT_ARG=""

for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    -h|--help)
      sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      if [[ -z "$PORT_ARG" ]]; then
        PORT_ARG="$arg"
      else
        echo "error: unexpected extra argument: $arg" >&2
        exit 2
      fi
      ;;
  esac
done

# ── Pre-flight: pio in PATH ────────────────────────────────────────────────
if ! command -v pio >/dev/null 2>&1; then
  echo "error: pio not found on PATH. Install PlatformIO Core:" >&2
  echo "  https://docs.platformio.org/en/latest/core/installation/index.html" >&2
  exit 3
fi

# ── Pre-flight: source files exist ─────────────────────────────────────────
for f in "marquee/HwVerifyTest.cpp" "marquee/HwVerifyTest.h" "platformio.ini"; do
  if [[ ! -f "$REPO_ROOT/$f" ]]; then
    echo "error: expected file not found: $f (am I in the right repo?)" >&2
    exit 4
  fi
done

# ── Port detection ─────────────────────────────────────────────────────────
detect_port() {
  # macOS USB-serial endpoints land under /dev/cu.* with these prefixes
  # depending on which USB-UART chip the clock has. Globbing produces
  # newline-separated paths; non-matching globs vanish under nullglob so
  # we don't end up with literal "/dev/cu.usbserial-*" strings.
  shopt -s nullglob
  local matches=(/dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART /dev/cu.usbmodem*)
  shopt -u nullglob
  printf '%s\n' "${matches[@]}"
}

PORT=""
if [[ -n "$PORT_ARG" ]]; then
  if [[ "$PORT_ARG" != /dev/* ]]; then
    echo "error: explicit port must be an absolute path under /dev/, got: $PORT_ARG" >&2
    exit 5
  fi
  if [[ ! -e "$PORT_ARG" ]]; then
    echo "error: port $PORT_ARG does not exist" >&2
    exit 5
  fi
  PORT="$PORT_ARG"
elif (( DRY_RUN )); then
  PORT=""   # not needed for a build-only run
else
  mapfile -t PORTS < <(detect_port)
  if (( ${#PORTS[@]} == 0 )); then
    cat >&2 <<'EOF'
error: no USB-serial port found.

Looked under:
  /dev/cu.usbserial-*
  /dev/cu.wchusbserial*
  /dev/cu.SLAB_USBtoUART
  /dev/cu.usbmodem*

Troubleshooting:
  1. Is the clock plugged in? Try a different USB cable — many cheap
     USB-A→micro-USB cables are charge-only and don't carry data.
  2. Is the USB-UART driver installed? Most Wemos D1 mini clones use the
     CH340 (driver: https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html)
     or CH9102 chip; some use CP2102 (driver: Silicon Labs VCP).
  3. Run `pio device list` — if pio sees no devices, the OS isn't
     enumerating the clock. Try a different port, restart the
     workstation, or check System Settings → Privacy & Security for a
     blocked driver kext.
  4. Try `system_profiler SPUSBDataType | grep -i 'serial\|ch34\|cp210\|ftdi'`
     to see if the chip is enumerated at the USB layer.

Or pass an explicit port: ./scripts/run_hw_verify.sh /dev/cu.something
EOF
    exit 6
  elif (( ${#PORTS[@]} > 1 )); then
    {
      echo "error: multiple USB-serial ports detected — please pick one explicitly:"
      printf '  %s\n' "${PORTS[@]}"
      echo
      echo "Re-run with the right one, e.g.:"
      echo "  $0 ${PORTS[0]}"
    } >&2
    exit 7
  else
    PORT="${PORTS[0]}"
    echo "[hw-verify] auto-detected port: $PORT"
  fi
fi

# ── Set up log dir ─────────────────────────────────────────────────────────
mkdir -p "$LOG_DIR"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$LOG_DIR/ecdsa-verify-$TS.log"

# ── Build ──────────────────────────────────────────────────────────────────
echo "[hw-verify] building -e $ENV_NAME with -DWAGFAM_HW_VERIFY_TEST=1"
cd "$REPO_ROOT"
PLATFORMIO_BUILD_FLAGS="-DWAGFAM_HW_VERIFY_TEST=1" pio run -e "$ENV_NAME"
BUILD_RC=$?
if (( BUILD_RC != 0 )); then
  echo "[hw-verify] build failed (exit $BUILD_RC)" >&2
  exit $BUILD_RC
fi

if (( DRY_RUN )); then
  echo "[hw-verify] --dry-run: build OK, skipping flash + monitor"
  exit 0
fi

# ── Flash ──────────────────────────────────────────────────────────────────
echo "[hw-verify] flashing $PORT (build kept in .pio/build/$ENV_NAME)"
PLATFORMIO_BUILD_FLAGS="-DWAGFAM_HW_VERIFY_TEST=1" pio run -e "$ENV_NAME" -t upload --upload-port "$PORT"
FLASH_RC=$?
if (( FLASH_RC != 0 )); then
  echo "[hw-verify] flash failed (exit $FLASH_RC)" >&2
  exit $FLASH_RC
fi

# ── Monitor + capture ──────────────────────────────────────────────────────
# Originally this used `pio device monitor`, but pio's monitor wraps
# pyserial's miniterm which calls termios.tcgetattr(stdout_fd) at startup
# to save the host terminal state.  When we redirect stdout to a log file,
# that fd isn't a TTY → ENOTTY → monitor dies before opening the serial
# port at all.  Using a small pyserial reader (scripts/_capture_serial.py)
# avoids the issue entirely and gives us cleaner exit semantics.
echo "[hw-verify] capturing serial → $LOG (timeout ${TIMEOUT_S}s)"
trap 'echo; echo "[hw-verify] interrupted — partial log: $LOG"; exit 130' INT TERM
python3 "$REPO_ROOT/scripts/_capture_serial.py" "$PORT" "$MONITOR_BAUD" "$TIMEOUT_S" "$LOG"
CAPTURE_RC=$?
trap - INT TERM

# ── Parse + report ─────────────────────────────────────────────────────────
echo
echo "──────── [ECDSA-TEST] lines from $LOG ────────"
grep -E '^\[ECDSA-TEST\]' "$LOG" || echo "(no [ECDSA-TEST] lines captured — the test never ran)"
echo "─────────────────────────────────────────────────"
echo
# Also surface the firmware's own [CFG] reject lines — they explain WHY
# each negative case failed (and they're benign here, by design).
if grep -qE '^\[CFG\]' "$LOG"; then
  echo "[CFG] reject reasons emitted by verifyConfigUpdateSignature():"
  grep -E '^\[CFG\]' "$LOG" | sed 's/^/  /'
  echo
fi

# _capture_serial.py exit codes:
#   0 = saw "DONE: N/N" with N == total
#   1 = TIMEOUT_S elapsed without seeing any DONE line
#   2 = saw "DONE: N/M" with N < M
#   3 = couldn't open serial port
#   130 = SIGINT
case "$CAPTURE_RC" in
  0)
    DONE_LINE="$(grep -m1 '\[ECDSA-TEST\].*DONE:' "$LOG")"
    echo "[hw-verify] PASS — $DONE_LINE"
    echo "[hw-verify] log saved: $LOG"
    exit 0
    ;;
  1)
    echo "[hw-verify] FAIL — timeout after ${TIMEOUT_S}s, DONE line never appeared; full log: $LOG" >&2
    exit 8
    ;;
  2)
    DONE_LINE="$(grep -m1 '\[ECDSA-TEST\].*DONE:' "$LOG")"
    echo "[hw-verify] FAIL — partial pass: $DONE_LINE; full log: $LOG" >&2
    exit 9
    ;;
  3)
    echo "[hw-verify] FAIL — serial port error; full log: $LOG" >&2
    exit 11
    ;;
  *)
    echo "[hw-verify] FAIL — capture exited $CAPTURE_RC; full log: $LOG" >&2
    exit "$CAPTURE_RC"
    ;;
esac
