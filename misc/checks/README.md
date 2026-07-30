# Repository checks

A **check** inspects files or build artifacts and reports policy violations. It
is not a test: it has no assertions, it communicates by exit code, and it prints
every violation it finds so one run tells you everything that is wrong.

| File | Inspects | Invoked from |
| --- | --- | --- |
| `check_binary_naming.py` | `strings -a` / `nm -a` over a built `foundry` binary, for leaked upstream identifiers | build and release workflows (needs a compiled binary) |
| `check_homebrew_cask.py` | the generated Homebrew cask and the release workflow's tap wiring | release tooling, run by hand |
| `check_ios_template_package.py` | the iOS template bundle produced by `misc/scripts/package_ios_templates.py` | release tooling, run by hand |
| `check_release_api_artifacts.py` | the release API artifact bundle produced by `misc/scripts/package_foundry_api_artifacts.py` | release tooling, run by hand |

## `check_*` versus `test_*`

- `check_*.py` lives here, takes an artifact or a path, reports **every**
  violation, and exits non-zero if there were any. It contains no `assert`.
- `test_*.py` executes code and asserts on the result. It never lives here. Its
  home is `tests/python_build/` (build-system behavior), `.github/scripts/tests/`
  (CI helper modules), `scripts/tests/` (developer tooling), or `tools/*/tests/`
  (standalone tools).

A `test_*.py` with no assertions is misnamed, and a `check_*.py` with assertions
is misnamed. The full policy, including the prohibited assertion patterns and the
self-check commands, is the `### Test authoring rules` section of the root
`AGENTS.md`; the directory-specific rules for adding files here are in
`misc/checks/AGENTS.md`.

## Writing a check that can be tested

Several of these checks need a real artifact — a compiled binary, a packaged
zip — that no unit test should have to produce. Keep the decision logic pure and
separate from the acquisition of the artifact:

```python
def find_forbidden_tokens(scan_text: str) -> list[str]:
    """Pure: the unit test feeds this synthetic `strings`/`nm` output."""
    return [token for token in FORBIDDEN_BINARY_STRINGS if token in scan_text]
```

The `argparse` entry point then shells out for the real artifact and passes the
text to the pure function. Each check here exposes at least one such
`find_*_violations` (or `find_forbidden_tokens`) function returning a list of
violation strings, and its unit test drives that function with synthetic input.

## Running

```sh
# The unit tests for the checks (also wired as the `foundry-repository-checks`
# pre-commit hook).
python3 -m unittest discover -s misc/checks/tests -t .

# A check against a real artifact.
python3 misc/checks/check_binary_naming.py bin/foundry.macos.editor.arm64
python3 misc/checks/check_homebrew_cask.py
python3 misc/checks/check_ios_template_package.py
python3 misc/checks/check_release_api_artifacts.py
```
