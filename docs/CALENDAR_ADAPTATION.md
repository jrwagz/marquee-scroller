# Calendar Code: Source and Adaptations

## Source

The calendar event logic and tests in `server/app/services/calendar_event.py`,
`server/app/services/calendar_gen.py`, and `server/tests/test_calendar_event.py`
are adapted from the private repo
[`jrwagz/wagfam-clocks-data-source`](https://github.com/jrwagz/wagfam-clocks-data-source).

**Source commit pinned:** `ddd0f0509f74dc01f73a764c817b561e2f864afc` (the `main`
branch HEAD as of 2026-04-26).

| Source file | Adapted to | Behavioral changes? |
| --- | --- | --- |
| `calendar_event.py` | `server/app/services/calendar_event.py` | **Yes — one** (see below) |
| `generate_data_source.py` | `server/app/services/calendar_gen.py` | **Yes — fundamental** (CLI script → importable functions) |
| `tests/test_calendar_event.py` | `server/tests/test_calendar_event.py` | Test framework rewrite to drive the new `today=` parameter |

The adaptations are kept in this repo (rather than imported as a submodule)
because the source repo is shaped as a **CLI tool** (`argparse` + `main()` that
writes JSON to disk), not a Python library — see "Why not a submodule" below.

Full unified diffs are checked in under `docs/adaptations/`:

- `docs/adaptations/calendar_event.diff`
- `docs/adaptations/calendar_gen.diff`
- `docs/adaptations/test_calendar_event.diff`

To regenerate them locally and confirm nothing has drifted, see
"Verifying the diff" at the bottom of this doc.

---

## `calendar_event.py` — adapted

### The one behavioral change

Source repo `CalendarEvent.__init__` calls a module-level
`todays_date()` function each time anything date-related is needed, and
`todays_date()` returns `datetime.date.today()`. To test, the source repo
monkey-patches the module attribute via a pytest fixture in `conftest.py`:

```python
# from source repo conftest.py (paraphrased)
@pytest.fixture(autouse=True)
def fixed_today(monkeypatch):
    monkeypatch.setattr(calendar_event, "todays_date", lambda: date(2000, 1, 1))
```

That's fine in a single-process CLI test suite, but inside a FastAPI server with
a global request context and parallel test workers, monkey-patching a
module-level free function is fragile. The adaptation:

```python
# server/app/services/calendar_event.py:50
def __init__(self, data_dict: dict, today: datetime.date | None = None):
    ...
    self._today = today if today is not None else datetime.date.today()
```

`today` is now an explicit constructor parameter (defaulting to "real today" so
production usage is unchanged). All internal date math reads `self._today`
instead of calling `todays_date()` again. This makes the class trivially
testable without monkey-patching, and lets the server pass a fixed `today` when
generating responses for cache-warmer-style requests in the future.

This change is observable in the diff at every site that previously read
`todays_date()` — they now read `self._today` (or accept a `today=` kwarg in
helper methods).

### Cosmetic-only changes

- Module docstring added (one line at top)
- `MONTHS_IN_YEAR` reformatted from one-per-line to two lines of six (same
  string contents)
- Stripped Sphinx-style docstrings from internal methods that have obvious
  signatures (kept docstrings on public-facing methods)
- Reordered imports to PEP 8 grouping (`functools` moved up, `from enum` moved
  down to be alphabetically after stdlib top-level imports)

### Things NOT changed

- Class names, method names, public method signatures (other than the new
  optional `today=` kwarg), event sorting order, message-text format, or any
  date math.
- The `CalendarEventType` enum values (`"birth"`, `"marriage"`).
- Output format of `next_occurrence_description` strings.

---

## `calendar_gen.py` — adapted from `generate_data_source.py`

The source file is a **standalone CLI script** with this shape:

```python
def parse_args() -> argparse.Namespace: ...
def main() -> int:
    opts = parse_args()
    events = load(opts.data_structure)
    messages = generate(events, opts.max_events, opts.max_future_days)
    write(opts.out_file, messages)
    return 0
```

It reads command-line args, reads a JSON file, computes messages, writes
another JSON file. To use it as a server library, two things had to change:

1. **Drop the CLI plumbing.** `parse_args()` and `main()` are removed entirely.
   No `argparse`, `logging.basicConfig`, `sys.exit`. The server runtime owns
   logging and request lifecycle.
2. **Return values instead of writing to disk.** The new `generate_messages()`
   takes a list of `CalendarEvent` and returns `(messages, event_today)` as a
   tuple. The server's calendar router (`server/app/routers/calendar.py`)
   composes this into the JSON response body. The original wrote a
   `data_source.json` file to disk; the server doesn't need that intermediate.

A new helper, `load_calendar_data(path, today=None)`, was added to read the
on-disk events file and instantiate `CalendarEvent` objects with a shared
`today`. That's a server-side wrapper, not present in the source.

The actual message-generation algorithm (sort events, walk forward up to N
days, prefer "today" events, cap at max_events) is unchanged from the source —
just extracted into a reusable function.

---

## `tests/test_calendar_event.py` — adapted

The source test file relies on `conftest.py` to set today=2000-01-01 via
auto-use fixture monkey-patching. The adapted version:

- Removes that monkey-patch dependency
- Defines `TODAY = datetime.date(2000, 1, 1)` and a `make_event(data)` helper
  that explicitly passes `today=TODAY` to the constructor — exercising the new
  `today=` kwarg of `CalendarEvent.__init__`
- Drops most docstrings on test functions (the test name itself documents
  intent; the source also had verbose docstrings that didn't add information)
- Asserts on full `next_occurrence == datetime.date(...)` rather than the
  source's separate `.year`/`.month`/`.day` checks, which is equivalent but
  reads cleaner

**Test coverage is preserved 1:1** — every test from the source repo has a
corresponding test in the server file, asserting the same behavior.

---

## Why not a submodule?

The PR review (#25, comment 6) raised the option of pulling
`wagfam-clocks-data-source` in as a git submodule rather than copying and
adapting the files. We decided against that for three reasons:

1. **The source isn't a library.** Its top-level Python files are CLI scripts.
   `generate_data_source.py` does `import argparse` and runs `main()`. To use
   it as a library, the server would need to either:
   (a) `import generate_data_source` and then never call its `main()`, which
   leaves `argparse` in our dependency tree for no reason; or
   (b) duplicate the import path with a wrapper, which is what we already do.
   There's no clean importable public API to depend on.

2. **The source has no published version.** `wagfam-clocks-data-source` has
   no tags, no `pyproject.toml` package metadata for installation as a wheel,
   and no `__init__.py` packaging — it's a script repo, not a library repo.
   A submodule would pin a commit, but consuming the code would still require
   `sys.path` manipulation rather than a clean `pip install`.

3. **Behavioral fork is intentional.** The `today=` parameter on
   `CalendarEvent.__init__` is a real divergence we depend on for testability
   (see the section above). With a submodule, every upstream change would
   require re-applying that adaptation — exactly the manual rebase work that
   adapting-and-vendoring already paid for.

**Better long-term fix** (out of scope for this PR): refactor
`wagfam-clocks-data-source` upstream to expose `CalendarEvent` and
`generate_messages` from a `calendar` subpackage with `__init__.py`, package
it via `pyproject.toml`, and publish wheels (e.g. to a private GitHub Packages
index or as a Git-installable dependency: `pip install
git+ssh://git@github.com/jrwagz/wagfam-clocks-data-source.git@v1`). Then this
repo could declare it as a real dependency and drop the adapted copies.

---

## Verifying the diff

To prove the local files match what's documented here, against the pinned
source commit:

```bash
git clone git@github.com:jrwagz/wagfam-clocks-data-source.git /tmp/wcds
git -C /tmp/wcds checkout ddd0f0509f74dc01f73a764c817b561e2f864afc

diff -u /tmp/wcds/calendar_event.py server/app/services/calendar_event.py \
  | diff -u - docs/adaptations/calendar_event.diff
diff -u /tmp/wcds/generate_data_source.py server/app/services/calendar_gen.py \
  | diff -u - docs/adaptations/calendar_gen.diff
diff -u /tmp/wcds/tests/test_calendar_event.py server/tests/test_calendar_event.py \
  | diff -u - docs/adaptations/test_calendar_event.diff
```

All three should produce no output, confirming the adapted files are an exact
match for what the diffs in `docs/adaptations/` describe.

If a future PR updates one of these adapted files without updating both the
diff artifact and this doc, those `diff -u … | diff -u -` checks will fail —
which is the intended forcing function.
