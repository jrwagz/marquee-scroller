"""
Static-analysis regression test: every ConfigData field declared in
webui/src/types.ts must either be wired into webui/src/pages/SettingsPage.tsx
or be on the explicit READ_ONLY allowlist.

Why this exists:
  PR #59 shipped a Settings page that only covered ~12 of 19 mutable config
  keys — calendar URL, API keys, geo location, web password, and the PM
  toggle were silently missing. Anyone setting up a fresh device still had
  to use the legacy /configure page. This test pins the contract: if you
  add a new ConfigData key to types.ts, you also wire it into the form (or
  declare it read-only here, with a reason).
"""

import re
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent.parent
TYPES = ROOT / "webui" / "src" / "types.ts"
SETTINGS = ROOT / "webui" / "src" / "pages" / "SettingsPage.tsx"

# Fields that intentionally do NOT appear in the Settings form:
#   - ota_safe_url, device_name, wagfam_event_today: server-set, not user-editable
#     (see CLAUDE.md "Configuration Storage" section).
READ_ONLY: set[str] = {
    "ota_safe_url",
    "device_name",
    "wagfam_event_today",
}


@pytest.fixture(scope="module")
def config_fields() -> set[str]:
    """Parse field names out of the ConfigData interface block."""
    src = TYPES.read_text(encoding="utf-8")
    iface = re.search(
        r"export interface ConfigData\s*\{([^}]*)\}",
        src,
        re.DOTALL,
    )
    assert iface, "Could not locate ConfigData interface in types.ts"
    return set(re.findall(r"^\s*(\w+)\s*:", iface.group(1), re.MULTILINE))


@pytest.fixture(scope="module")
def settings_source() -> str:
    return SETTINGS.read_text(encoding="utf-8")


def test_every_config_field_is_either_wired_or_read_only(
    config_fields: set[str],
    settings_source: str,
) -> None:
    missing: list[str] = []
    for field in sorted(config_fields):
        if field in READ_ONLY:
            continue
        # Look for the field name as either a string literal (setBool("is_24hour", …))
        # or as a property access (payload.web_password = …) anywhere in the form.
        # Use a word boundary so "show_city" doesn't accidentally match "show_city_extra".
        pattern = rf"[\"'.]({re.escape(field)})\b"
        if not re.search(pattern, settings_source):
            missing.append(field)

    assert not missing, (
        "ConfigData fields with no SettingsPage wiring or READ_ONLY entry: "
        f"{missing}. Either add a form control in SettingsPage.tsx or, if the "
        "field is set elsewhere (server/config push/etc), add it to READ_ONLY "
        "with a comment explaining why."
    )


def test_read_only_set_only_lists_real_fields(config_fields: set[str]) -> None:
    """READ_ONLY must not drift out of sync with types.ts."""
    stale = READ_ONLY - config_fields
    assert not stale, f"READ_ONLY references fields not in ConfigData: {stale}"
