# Foundry Version JSON Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `foundry --version --json` with embedded release metadata and wire GitHub release builds to provide and verify that metadata.

**Architecture:** SCons resolves release metadata once and emits it beside the existing generated version macros. A focused `FoundryVersionInfo` helper serializes those macros, while the top-level CLI parser and `Main::setup()` handle the standalone version query before normal startup.

**Tech Stack:** C++17, Godot/Foundry `Dictionary` and `JSON`, SCons/Python build generation, doctest, GitHub Actions.

---

### Task 1: Add failing CLI parser coverage

**Files:**
- Modify: `tests/core/os/test_foundry_cli_parser.h`

- [ ] **Step 1: Add tests for both option orders and invalid arguments**

Add these cases near the other top-level parser tests:

```cpp
TEST_CASE("[FoundryCLIParser] Version query accepts JSON in either option order") {
	FoundryCLIParser::ParseResult before = FoundryCLIParser::parse(make_args({
			"foundry", "--version", "--json",
	}));
	REQUIRE_MESSAGE(before.ok, before.error);
	CHECK(before.version_requested);
	CHECK(before.json);
	CHECK(before.command_path.is_empty());

	FoundryCLIParser::ParseResult after = FoundryCLIParser::parse(make_args({
			"foundry", "--json", "--version",
	}));
	REQUIRE_MESSAGE(after.ok, after.error);
	CHECK(after.version_requested);
	CHECK(after.json);
}

TEST_CASE("[FoundryCLIParser] Version query rejects command arguments") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry", "--version", "script", "format",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("version"));
}

TEST_CASE("[FoundryCLIParser] Version-like user arguments stay behind separator") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry", "project", "run", "--project", "demo", "--", "--version",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_FALSE(result.version_requested);
	CHECK_EQ(result.user_args, make_args({ "--version" }));
}
```

- [ ] **Step 2: Run the focused test to verify the new tests fail for the missing parser field**

Run:

```sh
python3 scripts/agent_build.py --dev-build --test --case "*FoundryCLIParser*"
```

Expected: the build/test command fails because `ParseResult` does not yet expose `version_requested`.

### Task 2: Add failing JSON contract coverage

**Files:**
- Create: `tests/core/os/test_foundry_version_info.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Register the new test header and write the contract test**

Create the standard test header and add this test body:

```cpp
#include "core/io/json.h"
#include "core/version.h"
#include "main/version_info.h"
#include "tests/test_macros.h"

namespace TestFoundryVersionInfo {

TEST_CASE("[FoundryVersionInfo] Version JSON contains the stable release schema") {
	const Variant parsed = JSON::parse_string(FoundryVersionInfo::get_json());
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary root = parsed;

	CHECK_EQ(root["product"], String(FOUNDRY_VERSION_NAME));
	CHECK_EQ(root["version"], vformat("%d.%d.%d", FOUNDRY_VERSION_MAJOR, FOUNDRY_VERSION_MINOR, FOUNDRY_VERSION_PATCH));
	CHECK(root.has("release_tag"));
	CHECK(root.has("channel"));
	CHECK(root.has("git_commit"));
	CHECK(root["git_dirty"].get_type() == Variant::BOOL);
	CHECK(root.has("build_id"));
	CHECK(root.has("target"));

	const Dictionary extension_api = root["extension_api"];
	CHECK_EQ(int(extension_api["interface_format"]), FOUNDRY_EXTENSION_INTERFACE_FORMAT);
	CHECK_EQ(int(extension_api["abi_revision"]), FOUNDRY_EXTENSION_ABI_REVISION);
}

} // namespace TestFoundryVersionInfo
```

Register it beside the other `tests/core/os` headers in `tests/test_main.cpp`.

- [ ] **Step 2: Run the focused test to verify the JSON test fails before implementation**

Run:

```sh
python3 scripts/agent_build.py --dev-build --test --case "*FoundryVersionInfo*"
```

Expected: compilation fails because `main/version_info.h` and the new generated extension metadata macros do not exist.

### Task 3: Generate compile-time release metadata

**Files:**
- Modify: `methods.py`
- Modify: `core/core_builders.py`
- Modify: `core/SCsub`

- [ ] **Step 1: Add build metadata resolution in `methods.py`**

Add helpers that read non-empty `FOUNDRY_*` overrides, parse boolean/integer overrides strictly, derive an exact Git tag with `git describe --tags --exact-match HEAD`, derive dirty state with `git status --porcelain`, and fall back to `unknown`, `local`, `dev`, and `false` as specified in the design. Add `get_build_metadata(env, version_info)` returning:

```python
{
    "release_tag": release_tag,
    "channel": channel,
    "git_commit": git_commit,
    "git_dirty": git_dirty,
    "build_id": build_id,
    "target": target,
    "extension_interface_format": extension_interface_format,
    "extension_abi_revision": extension_abi_revision,
}
```

Use `env["platform"]` and `env["arch"]` for the target fallback. Preserve the existing `get_git_info()` output and version status behavior.

- [ ] **Step 2: Pass metadata into the version generator**

Change the `core/SCsub` version command input from:

```python
env.Value(methods.get_version_info(env.module_version_string))
```

to a merged dictionary containing `methods.get_version_info(env.module_version_string)` and `methods.get_build_metadata(env, version_info)`. Keep `version_hash.gen.cpp` generation unchanged so existing hash consumers remain compatible.

- [ ] **Step 3: Emit escaped macros and numeric/bool macros**

Extend `core_builders.version_info_builder` with these generated definitions:

```cpp
#define FOUNDRY_VERSION_RELEASE_TAG "{release_tag}"
#define FOUNDRY_VERSION_CHANNEL "{channel}"
#define FOUNDRY_VERSION_GIT_COMMIT "{git_commit}"
#define FOUNDRY_VERSION_GIT_DIRTY {git_dirty}
#define FOUNDRY_VERSION_BUILD_ID "{build_id}"
#define FOUNDRY_VERSION_TARGET "{target}"
#define FOUNDRY_EXTENSION_INTERFACE_FORMAT {extension_interface_format}
#define FOUNDRY_EXTENSION_ABI_REVISION {extension_abi_revision}
```

Use the existing generated-file escaping wrapper so release metadata cannot produce invalid C++ string literals.

### Task 4: Implement the version JSON serializer

**Files:**
- Create: `main/version_info.h`
- Create: `main/version_info.cpp`

- [ ] **Step 1: Define the focused public helper**

Declare:

```cpp
#pragma once

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

namespace FoundryVersionInfo {

Dictionary get_dictionary();
String get_json();

} // namespace FoundryVersionInfo
```

- [ ] **Step 2: Serialize generated macros**

Implement `get_dictionary()` with the exact public keys and nested `extension_api` dictionary. Build `version` with `vformat("%d.%d.%d", FOUNDRY_VERSION_MAJOR, FOUNDRY_VERSION_MINOR, FOUNDRY_VERSION_PATCH)`, and implement `get_json()` as `JSON::stringify(get_dictionary(), "", false)`. Include `core/io/json.h` and `core/version.h`; do not read runtime environment or Git.

- [ ] **Step 3: Re-run the JSON test and verify it passes**

Run:

```sh
python3 scripts/agent_build.py --dev-build --test --case "*FoundryVersionInfo*"
```

Expected: the JSON schema test passes, including generated API defaults `1` and `7`.

### Task 5: Wire `--version` into the parser and main startup

**Files:**
- Modify: `main/cli_parser.h`
- Modify: `main/cli_parser.cpp`
- Modify: `main/main.cpp`

- [ ] **Step 1: Add the parse result flag**

Add `bool version_requested = false;` to `FoundryCLIParser::ParseResult`.

- [ ] **Step 2: Parse standalone version queries**

Recognize `--version` in the top-level parse loop before normal command routing. When it is seen, set `version_requested`, mark the new CLI as used, and allow only a following `--json`. If any other token follows a version query, fail with a message identifying the standalone version query. Add an end-of-loop return for a successful version query so `foundry --json --version` does not fall through to the existing “expected command” error. Keep `collect_user_args()` first so command user arguments are unchanged.

- [ ] **Step 3: Add parser green coverage**

Run:

```sh
python3 scripts/agent_build.py --dev-build --test --case "*FoundryCLIParser*"
```

Expected: all `FoundryCLIParser` tests pass.

- [ ] **Step 4: Dispatch before normal startup**

Include `main/version_info.h` in `main/main.cpp`. Immediately after the successful parser/error handling and before help or global argument application, add:

```cpp
if (cli_parse.version_requested) {
	if (cli_parse.json) {
		OS::get_singleton()->print("%s\n", FoundryVersionInfo::get_json().utf8().get_data());
	} else {
		print_line(get_full_version_string());
	}
	exit_err = ERR_HELP;
	goto error;
}
```

Remove the later raw `else if (arg == "--version")` branch so only the parsed query emits output.

- [ ] **Step 5: Verify the C++ JSON contract and CLI behavior**

Run:

```sh
python3 scripts/agent_build.py --dev-build --test --case "*FoundryCLIParser*"
python3 scripts/agent_build.py --dev-build
```

Then run the single editor binary reported by the wrapper:

```sh
FOUNDRY_VERSION_BIN="$(find bin -maxdepth 1 -type f -name 'foundry.*.editor.*' -print -quit)"
"$FOUNDRY_VERSION_BIN" --version --json
"$FOUNDRY_VERSION_BIN" --json --version
```

Expected: each invocation exits zero and emits exactly one parseable JSON object; `--version` without `--json` retains the existing human-readable output.

### Task 6: Wire the release resolver and GitHub Actions

**Files:**
- Modify: `.github/scripts/resolve_release.py`
- Modify: `misc/scripts/test_release_resolver.py`
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Add a channel field to release resolution**

Add `channel` to `Release`, include it in `as_outputs()`, pass `alpha`/`beta`/`rc` from prerelease tags, and pass `stable` for stable tags. Manual releases use the selected manual channel. Preserve `status` values such as `alpha7` for existing engine-version behavior.

- [ ] **Step 2: Add resolver and workflow wiring tests**

Extend `misc/scripts/test_release_resolver.py` to assert `release.channel == "alpha"` for prerelease cases and `release.channel == "stable"` for stable cases. Require these workflow snippets:

```text
channel: ${{ steps.resolve.outputs.channel }}
FOUNDRY_RELEASE_TAG: ${{ needs.resolve.outputs.tag }}
FOUNDRY_CHANNEL: ${{ needs.resolve.outputs.channel }}
FOUNDRY_BUILD_ID: gh
FOUNDRY_GIT_COMMIT: ${{ github.sha }}
FOUNDRY_GIT_DIRTY: false
```

- [ ] **Step 3: Export metadata from every release build job**

Add the five `FOUNDRY_*` environment entries next to `FOUNDRY_VERSION_STATUS` in the Linux, Windows, macOS editor, macOS template, Web, Android, and iOS release jobs. Keep the target derived from each job’s existing platform and `arch=` SCons flags.

- [ ] **Step 4: Validate packaged desktop binaries**

In the release `package` job, after downloading artifacts, execute the Linux editor binary and the executable inside the macOS app bundle with `--version --json`. Parse each result with Python and assert the expected tag, channel, build ID, Git SHA, `git_dirty is False`, and extension API values `1` and `7`. Fail if either command emits invalid JSON or any value differs.

- [ ] **Step 5: Run the release resolver test**

Run:

```sh
python3 misc/scripts/test_release_resolver.py
```

Expected: `release resolver tests passed`.

### Task 7: Document and verify the public contract

**Files:**
- Modify: `doc/tools/foundry_cli.md`

- [ ] **Step 1: Document the command and schema**

Add a version section showing `foundry --version --json`, the stable JSON shape, the full semantic version behavior, and the compile-time override variables. State that release jobs provide explicit metadata while local builds use Git/platform fallbacks.

- [ ] **Step 2: Run focused verification**

Run:

```sh
git diff --check
python3 misc/scripts/test_release_resolver.py
python3 scripts/agent_build.py --dev-build --test --case "*FoundryCLIParser*"
python3 scripts/agent_build.py --dev-build --test --case "*FoundryVersionInfo*"
```

Expected: all commands exit zero, the resolver prints its success line, and both doctest filters report success.

- [ ] **Step 3: Run final CI-style validation**

Run the final non-shortcut build and focused suite with the repository’s required wrapper/defaults:

```sh
python3 scripts/agent_build.py --test --case "*FoundryCLIParser*"
python3 scripts/agent_build.py --test --case "*FoundryVersionInfo*"
```

Inspect `git diff --check` and `git status --short`; confirm only the intended implementation files are changed and unrelated pre-existing untracked files remain untouched.

- [ ] **Step 4: Commit the implementation**

```sh
git add methods.py core/core_builders.py core/SCsub main/version_info.h main/version_info.cpp main/cli_parser.h main/cli_parser.cpp main/main.cpp tests/core/os/test_foundry_cli_parser.h tests/core/os/test_foundry_version_info.h tests/test_main.cpp .github/scripts/resolve_release.py misc/scripts/test_release_resolver.py .github/workflows/release.yml doc/tools/foundry_cli.md
git commit -m "feat: add JSON version metadata"
```
