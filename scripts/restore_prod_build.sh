#!/usr/bin/env bash
#
# Restore the clock to a prod-equivalent build after running the verify
# fixture (PR #100). Companion to scripts/run_hw_verify.sh — run this when
# you're done capturing the test log and want the clock back to normal duty.
#
# Builds the `default` env (auto-update enabled, files-jrwagz.azurewebsites.net
# trusted) WITHOUT the WAGFAM_HW_VERIFY_TEST flag, then flashes it. The
# device will reboot into normal firmware, connect to WiFi, and let the
# calendar's auto-update path take over from there.
#
# Usage:
#   ./scripts/restore_prod_build.sh [SERIAL_PORT]
#   ./scripts/restore_prod_build.sh --dry-run            # build only
#   ./scripts/restore_prod_build.sh --env dev [PORT]     # use dev env instead of default
#                                                       # (dev keeps auto-update OFF)

set -u
set -o pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_NAME="default"
DRY_RUN=0
PORT_ARG=""

while (( $# > 0 )); do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    --env)     ENV_NAME="$2"; shift 2 ;;
    -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *)
      if [[ -z "$PORT_ARG" ]]; then PORT_ARG="$1"; shift
      else echo "error: unexpected extra arg: $1" >&2; exit 2
      fi
      ;;
  esac
done

if ! command -v pio >/dev/null 2>&1; then
  echo "error: pio not found on PATH." >&2
  exit 3
fi

# Same port-detection logic as run_hw_verify.sh — keep them aligned.
detect_port() {
  shopt -s nullglob
  local matches=(/dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART /dev/cu.usbmodem*)
  shopt -u nullglob
  printf '%s\n' "${matches[@]}"
}

PORT=""
if (( ! DRY_RUN )); then
  if [[ -n "$PORT_ARG" ]]; then
    [[ -e "$PORT_ARG" ]] || { echo "error: port $PORT_ARG does not exist" >&2; exit 5; }
    PORT="$PORT_ARG"
  else
    mapfile -t PORTS < <(detect_port)
    if (( ${#PORTS[@]} == 0 )); then
      echo "error: no USB-serial port found. See scripts/run_hw_verify.sh for troubleshooting." >&2
      exit 6
    elif (( ${#PORTS[@]} > 1 )); then
      echo "error: multiple USB-serial ports — pick one explicitly:" >&2
      printf '  %s\n' "${PORTS[@]}" >&2
      exit 7
    else
      PORT="${PORTS[0]}"
      echo "[restore] auto-detected port: $PORT"
    fi
  fi
fi

cd "$REPO_ROOT"
echo "[restore] building -e $ENV_NAME (no WAGFAM_HW_VERIFY_TEST flag)"
# Explicitly clear PLATFORMIO_BUILD_FLAGS so a leftover env var from the
# verify-test run can't leak in.
PLATFORMIO_BUILD_FLAGS="" pio run -e "$ENV_NAME"

if (( DRY_RUN )); then
  echo "[restore] --dry-run: build OK, skipping flash"
  exit 0
fi

echo "[restore] flashing $PORT"
PLATFORMIO_BUILD_FLAGS="" pio run -e "$ENV_NAME" -t upload --upload-port "$PORT"
echo "[restore] done — device should boot into normal firmware shortly."
echo "[restore] (if env=default, calendar auto-update may pull a newer version on its next poll.)"
