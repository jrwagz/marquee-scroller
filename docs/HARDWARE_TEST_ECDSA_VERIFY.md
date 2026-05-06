# Hardware test — ECDSA-P256 config-update verify (PR #100)

This is the hardware-proof procedure jrwagz asked for in
[PR #100 review at 2026-05-06T19:52Z](https://github.com/jrwagz/marquee-scroller/pull/100):

> I want proof on actual esp8266 hardware that this new cryptography method
> behaves exactly as we expect it to.

The plan flashes a special build of the firmware to a real Wemos D1 mini
(ESP8266 / 4 MB flash) over USB-serial, runs 8 verify cases (1 positive, 7
negative) against the production `verifyConfigUpdateSignature` path, and
captures the per-case PASS/FAIL log. The test code lives in
`marquee/HwVerifyTest.cpp` and is gated by `-DWAGFAM_HW_VERIFY_TEST=1`;
without that flag the entire test fixture compiles to nothing and the
production firmware is bit-identical to a normal `pio run -e dev` build.

## Prerequisites

- macOS dev workstation with PlatformIO installed (`pio --version` works).
  This is the same setup used to produce the `dev` and `default` env builds
  already in `platformio.ini`.
- Wemos D1 mini connected over USB-serial. Detect the port with:
  ```sh
  ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART 2>/dev/null
  ```
  The CH340-based clones usually enumerate as `/dev/cu.wchusbserial-XXXX` or
  `/dev/cu.usbserial-XXXX`. There should be exactly one match while the
  clock is plugged in.
- `cryptography>=43` available to system Python 3 (already a dep of
  `jrwagz/wagfam-server`; if not present, `pip install cryptography`).

## Step 1 — generate fresh fixtures (optional)

The repo ships with a known-good fixture set baked into
`marquee/HwVerifyTest.cpp`. Skip this step unless you specifically want a
new keypair / new signed payloads (e.g. you just rotated keys, or you
suspect the committed fixtures are stale).

```sh
cd /path/to/marquee-scroller
python3 scripts/sign_test_payloads.py --rotate --write
```

`--rotate` discards the existing test keys under `scripts/test_keys/` and
generates a new pair. `--write` overwrites the marker block in
`HwVerifyTest.cpp` in place. The private keys stay in `scripts/test_keys/`
which is `.gitignore`d — never commit them.

## Step 2 — build with the test flag

PlatformIO doesn't accept `--build-flag` on the CLI; the working knob is
the `PLATFORMIO_BUILD_FLAGS` environment variable, which gets appended to
the `build_flags` of whichever env you build:

```sh
cd /path/to/marquee-scroller
PLATFORMIO_BUILD_FLAGS="-DWAGFAM_HW_VERIFY_TEST=1" pio run -e dev
```

Verified flash + RAM impact on this machine (Wemos D1 mini target):

| Build               | Flash             | RAM              |
|---------------------|-------------------|------------------|
| `pio run -e dev` (no flag)              | 560065 / 1044464 | 37240 / 81920 |
| `PLATFORMIO_BUILD_FLAGS=-DWAGFAM_HW_VERIFY_TEST=1 pio run -e dev` | 562201 / 1044464 | 38408 / 81920 |

Delta: +2136 bytes flash, +1168 bytes RAM. Well within budget.

Notes on the flag choice:

- We extend the `dev` env (not `default`) because `dev` already sets
  `WAGFAM_AUTO_UPDATE_DISABLED=1`. Without that, the device would auto-
  revert to the server's published `latestVersion` at the next calendar
  refresh — but in this build the device never gets to a calendar
  refresh, so it doesn't actually matter. Still, `dev` is the right
  default for "I'm running my own build."
- `-DWAGFAM_HW_VERIFY_TEST=1` causes:
  - `marquee/HwVerifyTest.cpp` to compile in the test runner
  - `marquee/marquee.ino` setup() to call `runHwVerifyTest()`
    immediately after `Serial.begin(115200)` and never return.
  - WiFi, LittleFS, displays, web server, calendar polling — none of it
    runs. The device only does the verify test then halts.

Expected flash-size impact vs the same `pio run -e dev` build without the
flag: about +800 bytes for the runner, +1 KB for the static fixture
constants. Stays well under the d1_mini sketch budget (~1 MB).

## Step 3 — flash + open serial monitor

```sh
PORT=$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART 2>/dev/null | head -1)
PLATFORMIO_BUILD_FLAGS="-DWAGFAM_HW_VERIFY_TEST=1" pio run -e dev --target upload --upload-port "$PORT"

# In a separate terminal — capture the serial log to a file for sharing
pio device monitor --port "$PORT" --baud 115200 | tee /tmp/ecdsa-test.log
```

If `pio device monitor` swallows the first few lines (it sometimes does
when the device boots before the monitor attaches), the `runHwVerifyTest`
implementation includes a 500 ms `delay(500)` at the top *and* re-prints
the summary line every 30 seconds while halted, so you should always be
able to scroll up or wait 30 s for a fresh summary.

Press the device's reset button if you want to re-run the test from a
clean boot.

## Step 4 — what to look for

Lines you should see on the serial port (in order):

```
[ECDSA-TEST] === ECDSA-P256 verify hardware test (PR #100) ===
[ECDSA-TEST] Fixtures from scripts/sign_test_payloads.py
[ECDSA-TEST] Each case calls verifyConfigUpdateSignature() — same path as the production poll.
[ECDSA-TEST] 01_positive: PASS (expected=true, got=true)
[ECDSA-TEST] 02_wrong_signature: PASS (expected=false, got=false)
[ECDSA-TEST] 03_wrong_public_key: PASS (expected=false, got=false)
[ECDSA-TEST] 04_truncated_payload: PASS (expected=false, got=false)
[ECDSA-TEST] 05_mutated_payload: PASS (expected=false, got=false)
[ECDSA-TEST] 06_mutated_signature: PASS (expected=false, got=false)
[ECDSA-TEST] 07_wrong_prefix_pubkey: PASS (expected=false, got=false)
[ECDSA-TEST] 08_empty_pubkey: PASS (expected=false, got=false)
[ECDSA-TEST] DONE: 8/8 passed in <N> ms
[ECDSA-TEST] === halting; reset the device to re-run ===
```

Then every ~30 seconds while the device sits halted:

```
[ECDSA-TEST] (still halted) DONE: 8/8 passed
```

`<N>` is the wall-clock duration of the 8 verify calls. Each P-256 verify
takes about 30 ms on the d1 mini, so expect roughly 200–250 ms total —
some cases hit the early-reject path before BearSSL is invoked at all.

You'll also see one `[CFG] config update rejected: <reason>` line per
negative case — that's the production reason-logging in
`verifyConfigUpdateSignature` working as designed. They are not failures.

## Step 5 — share the result

Filter the log for the test lines and paste them into PR #100:

```sh
grep -E '^\[ECDSA-TEST\]|^\[CFG\] config update rejected' /tmp/ecdsa-test.log
```

A passing run is `8/8 passed`. Anything else (a `FAIL` line, or a number
short of 8/8) means the verify path is misbehaving on real hardware and
the PR should NOT merge until investigated.

## Step 6 — back to a normal build

The verify-test firmware doesn't connect to WiFi, doesn't touch LittleFS,
and doesn't start the web server, so the device is unusable for normal
duty until you reflash without the flag. To get back to prod-like:

```sh
# Either flash the dev env without the flag…
pio run -e dev --target upload --upload-port "$PORT"

# …or flash the latest auto-update build that the calendar would push.
# Both of these paths leave LittleFS intact (the test build never
# formats it), so the device's persistent config is untouched.
```

If you flashed via OTA instead of USB-serial, the device is still on
WiFi internally, but `setup()` halted before reaching the OTA handlers,
so it can't accept an OTA update — you'll need USB-serial to recover.
That's why this whole procedure is documented as USB-serial-only.

## Failure modes the test is designed to catch

| Failure mode | Caught by case |
|---|---|
| Verify accepts a malformed signature | 06 (mutated 1 byte in raw r∥s) |
| Verify accepts a sig made for a different payload | 02, 04 (truncated), 05 (mutated payload) |
| Verify ignores the public key argument | 03 (wrong key) |
| Verify accepts a malformed key | 07 (wrong prefix), 08 (empty) |
| BearSSL/ESP8266 unaligned-access bugs | All cases — verify runs on real silicon |
| Stack/heap exhaustion in BearSSL on small RAM | 01 (still passes proves we have headroom) |

What the test does *not* catch:

- Wire-level encoding bugs in the server's signing path. Those are
  covered by the unit tests in `jrwagz/wagfam-server` PR #24 (147 tests
  pass, including specific ECDSA wire-format cases).
- End-to-end: device polls server, server signs a fresh payload, device
  verifies. That's the next step *after* this proves the verify path
  itself works. If the wagfam-server PR is deployed to staging, an E2E
  smoke test is straightforward; if it isn't deployed yet, this offline
  fixture-driven test is the fastest way to get jrwagz the proof he
  asked for without blocking on a deploy.
