#!/usr/bin/env python3
"""Generate fixture data for the on-device ECDSA-P256 verify test.

Pairs with `marquee/HwVerifyTest.cpp` and `docs/HARDWARE_TEST_ECDSA_VERIFY.md`.
The on-device test exercises the same `verifyConfigUpdateSignature` path the
production firmware uses (`marquee/ConfigUpdateVerify.cpp`); this script just
produces the signed payload + signature + public-key constants the test feeds
into it.

What it does, in order:

    1. Generate a P-256 keypair under `scripts/test_keys/main_priv.pem` if one
       doesn't already exist (gitignored — never commit the private key).
    2. Generate a *second* keypair under `scripts/test_keys/alt_priv.pem` for
       the "wrong-public-key" negative case.
    3. Sign a canonical JSON payload with the main private key, plus a few
       deliberately-broken variants (mutated payload, mutated signature,
       wrong-prefix pubkey, etc.) used by the verify test's negative cases.
    4. Print a C source block to stdout, ready to paste into the
       `// === HARNESS-GENERATED FIXTURES ===` marker in HwVerifyTest.cpp.
       Pass `--write` to overwrite the marker block in place instead.

The wire format matches what the server emits via
`app/services/config_signing.py` on jrwagz/wagfam-server: raw r||s (IEEE
P1363, 64 bytes), base64-padded; uncompressed public-key point (0x04 || X ||
Y, 65 bytes), 130 hex chars.

Run:
    python3 scripts/sign_test_payloads.py            # print to stdout
    python3 scripts/sign_test_payloads.py --write    # update HwVerifyTest.cpp in place
    python3 scripts/sign_test_payloads.py --rotate   # discard existing keys, regenerate

Requires `cryptography>=43` (already a dep of jrwagz/wagfam-server, so it's
on the dev workstation; install with `pip install cryptography` if not).
"""

from __future__ import annotations

import argparse
import base64
import re
import sys
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils as asym_utils

REPO_ROOT = Path(__file__).resolve().parent.parent
KEY_DIR = REPO_ROOT / "scripts" / "test_keys"
MAIN_PRIV_PEM = KEY_DIR / "main_priv.pem"
ALT_PRIV_PEM = KEY_DIR / "alt_priv.pem"
FIXTURE_FILE = REPO_ROOT / "marquee" / "HwVerifyTest.cpp"

# Canonical payload the positive-case fixture signs over. Kept short — just
# enough to look like a real config-update body — so the C-string literal in
# the fixture stays readable.
CANONICAL_PAYLOAD = '{"trollMessage":"hardware verify test","configUpdateVersion":42}'
# An unrelated payload signed by the same key. Used as the "wrong signature
# for THIS payload" case: the sig itself is well-formed and verifies fine
# against the OTHER payload, but should fail against CANONICAL_PAYLOAD.
OTHER_PAYLOAD = '{"trollMessage":"different message","configUpdateVersion":1}'


def load_or_create_priv(path: Path, *, rotate: bool) -> ec.EllipticCurvePrivateKey:
    if path.exists() and not rotate:
        return serialization.load_pem_private_key(path.read_bytes(), password=None)
    KEY_DIR.mkdir(parents=True, exist_ok=True)
    priv = ec.generate_private_key(ec.SECP256R1())
    path.write_bytes(
        priv.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption(),
        )
    )
    path.chmod(0o600)
    return priv


def pubkey_uncompressed_hex(priv: ec.EllipticCurvePrivateKey) -> str:
    """Return the 130-char hex of the uncompressed-point public key (04 || X || Y)."""
    raw = priv.public_key().public_bytes(
        encoding=serialization.Encoding.X962,
        format=serialization.PublicFormat.UncompressedPoint,
    )
    assert len(raw) == 65 and raw[0] == 0x04
    return raw.hex()


def sign_raw(priv: ec.EllipticCurvePrivateKey, payload: bytes) -> bytes:
    """Sign `payload` and return the 64-byte raw r||s form (IEEE P1363)."""
    der = priv.sign(payload, ec.ECDSA(hashes.SHA256()))
    r, s = asym_utils.decode_dss_signature(der)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def b64(buf: bytes) -> str:
    return base64.b64encode(buf).decode("ascii")


def c_string(s: str) -> str:
    """Render a Python str/bytes as a C-string literal — escapes quotes/backslashes only."""
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_fixture_block(main_priv: ec.EllipticCurvePrivateKey, alt_priv: ec.EllipticCurvePrivateKey) -> str:
    main_pub_hex = pubkey_uncompressed_hex(main_priv)
    alt_pub_hex = pubkey_uncompressed_hex(alt_priv)

    # Sanity: both pubkeys are 130 hex chars and start with "04".
    assert len(main_pub_hex) == len(alt_pub_hex) == 130
    assert main_pub_hex.startswith("04") and alt_pub_hex.startswith("04")

    canonical_bytes = CANONICAL_PAYLOAD.encode()
    other_bytes = OTHER_PAYLOAD.encode()

    sig_canonical_raw = sign_raw(main_priv, canonical_bytes)
    sig_for_other_payload = sign_raw(main_priv, other_bytes)

    # Mutated-payload case: same sig, payload with one byte flipped.
    mutated_payload = CANONICAL_PAYLOAD.replace("hardware", "Hardware")
    assert mutated_payload != CANONICAL_PAYLOAD

    # Truncated-payload case: drop the last byte of the canonical payload.
    truncated_payload = CANONICAL_PAYLOAD[:-1]

    # Mutated-signature case: flip one byte in the raw 64-byte sig, re-encode.
    sig_mutated_raw = bytearray(sig_canonical_raw)
    sig_mutated_raw[0] ^= 0x01
    sig_mutated_raw = bytes(sig_mutated_raw)

    # Wrong-prefix pubkey case: replace 0x04 prefix with 0x02 (compressed-form
    # marker) — the fixture's early-reject check should fire.
    wrong_prefix_pubkey_hex = "02" + main_pub_hex[2:]

    lines = []
    lines.append("// === HARNESS-GENERATED FIXTURES — do not edit by hand ===")
    lines.append("// Regenerate via: python3 scripts/sign_test_payloads.py --write")
    lines.append("// Source private keys live in scripts/test_keys/ (gitignored).")
    lines.append("// Each constant pairs with a TestCase row in kCases[].  No")
    lines.append("// PROGMEM marker — on ESP8266 Arduino, static const char[] already")
    lines.append("// lives in flash without it, and skipping FPSTR() keeps the test")
    lines.append("// path identical to how production code calls verifyConfigUpdate-")
    lines.append("// Signature (which takes String& and const char*).")
    lines.append("")
    lines.append(f'static const char kPubKeyMainHex[]      = "{main_pub_hex}";')
    lines.append(f'static const char kPubKeyAltHex[]       = "{alt_pub_hex}";')
    lines.append(f'static const char kPubKeyWrongPrefix[]  = "{wrong_prefix_pubkey_hex}";')
    lines.append("")
    lines.append(f"static const char kPayloadCanonical[]   = {c_string(CANONICAL_PAYLOAD)};")
    lines.append(f"static const char kPayloadMutated[]     = {c_string(mutated_payload)};")
    lines.append(f"static const char kPayloadTruncated[]   = {c_string(truncated_payload)};")
    lines.append(f"// kSigForOtherB64 was made over: {OTHER_PAYLOAD!r}")
    lines.append("//   (recorded as a comment, not a runtime constant — the verify")
    lines.append("//    test only uses the SIGNATURE of this payload, not the bytes.)")
    lines.append("")
    lines.append(f"static const char kSigCanonicalB64[]    = {c_string(b64(sig_canonical_raw))};")
    lines.append(f"static const char kSigForOtherB64[]     = {c_string(b64(sig_for_other_payload))};")
    lines.append(f"static const char kSigMutatedB64[]      = {c_string(b64(sig_mutated_raw))};")
    lines.append("")
    lines.append("// === END HARNESS-GENERATED FIXTURES ===")
    return "\n".join(lines)


def write_in_place(block: str) -> int:
    if not FIXTURE_FILE.exists():
        print(f"error: {FIXTURE_FILE} does not exist — cannot --write", file=sys.stderr)
        return 1
    src = FIXTURE_FILE.read_text()
    pattern = re.compile(
        r"// === HARNESS-GENERATED FIXTURES.*?// === END HARNESS-GENERATED FIXTURES ===",
        re.DOTALL,
    )
    if not pattern.search(src):
        print(
            "error: marker block not found in HwVerifyTest.cpp; expected "
            "'// === HARNESS-GENERATED FIXTURES ===' ... '// === END HARNESS-GENERATED FIXTURES ==='",
            file=sys.stderr,
        )
        return 1
    new_src = pattern.sub(lambda _m: block, src)
    FIXTURE_FILE.write_text(new_src)
    print(f"updated {FIXTURE_FILE.relative_to(REPO_ROOT)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--write", action="store_true", help="overwrite the marker block in HwVerifyTest.cpp in place")
    parser.add_argument("--rotate", action="store_true", help="discard existing test keys and regenerate")
    args = parser.parse_args()

    main_priv = load_or_create_priv(MAIN_PRIV_PEM, rotate=args.rotate)
    alt_priv = load_or_create_priv(ALT_PRIV_PEM, rotate=args.rotate)
    block = emit_fixture_block(main_priv, alt_priv)

    if args.write:
        return write_in_place(block)
    print(block)
    return 0


if __name__ == "__main__":
    sys.exit(main())
