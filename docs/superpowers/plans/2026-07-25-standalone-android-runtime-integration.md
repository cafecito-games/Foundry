# Standalone Android Runtime Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Foundry consume the exact pinned standalone Android runtime from explicit, revision-matched native inputs
without retaining or publishing an in-tree runtime copy.

**Architecture:** A canonical pin identifies one Foundry-Android Git object. SCons emits SHA-addressed native cells with
canonical provenance. A stdlib-only driver validates a complete 3×4 matrix, exports the pinned standalone tree into
scratch, derives compatibility from the exact clean native-producing Foundry commit, and invokes only the pinned
standalone tools/Gradle build. Foundry's kept `:app` consumes the resulting AARs explicitly.

**Tech Stack:** Python 3 standard library and `unittest`, SCons, Gradle/Groovy, Git, GitHub Actions, Android NDK/SDK.

---

### Task 1: Pin and native-provenance contracts

**Files:**

- Create: `platform/android/foundry_android_runtime.json`
- Create: `platform/android/android_runtime_contract.py`
- Create: `tests/python_build/test_android_runtime_contract.py`

- [ ] **Step 1: Write the failing pin tests**

Add tests that import `platform/android/android_runtime_contract.py` by file path and assert:

```python
pin = contract.load_pin(REPO_ROOT / "platform/android/foundry_android_runtime.json")
self.assertEqual("b8c46c807d467fcd1667b7d4cb04d07a09a08860", pin.source_revision)
self.assertEqual("f22bec563cbb992e770c89688a2c761855102f82", pin.source_tree)
self.assertEqual("0.1.0-dev-SNAPSHOT", pin.bindings_version)
self.assertEqual(1, pin.jni_contract_version)
self.assertEqual("exact-native-source-revision", pin.engine_compatibility_policy)
```

Create noncanonical, missing-field, wrong-SHA, wrong-policy, and unexpected-field JSON fixtures under
`tempfile.TemporaryDirectory()` and assert `ContractError` includes the offending field.

- [ ] **Step 2: Write the failing matrix and provenance tests**

Create all twelve cells under a temporary native root. Each cell contains two byte fixtures and canonical provenance.
Assert `validate_native_matrix()` returns the exact sorted matrix and rejects each independently mutated condition:

```python
for mutation, diagnostic in (
    ("dirty", "dirty Foundry source"),
    ("mixed_revision", "engine revision mismatch"),
    ("mixed_tree", "engine tree mismatch"),
    ("wrong_build_flags", "build configuration mismatch"),
    ("wrong_abi", "ABI mapping mismatch"),
    ("hash_drift", "SHA-256 mismatch"),
    ("missing_cell", "missing native cell"),
    ("extra_cell", "unexpected native cell"),
    ("extra_file", "unexpected native cell file"),
    ("noncanonical", "provenance is not canonical JSON"),
):
    with self.subTest(mutation=mutation):
        root = make_native_matrix(self.workspace, mutation=mutation)
        with self.assertRaisesRegex(contract.ContractError, diagnostic):
            contract.validate_native_matrix(root, EXPECTED_REVISION, EXPECTED_TREE)
```

Also assert matrix order and exact mappings for debug/dev/release and all four SCons/Android ABIs.

- [ ] **Step 3: Run the tests to verify RED**

Run:

```sh
python3 -m unittest tests.python_build.test_android_runtime_contract -v
```

Expected: import/file failures because the pin and contract module do not exist.

- [ ] **Step 4: Add the canonical pin and minimal contract implementation**

Write `platform/android/foundry_android_runtime.json` as canonical JSON with the values from the design. Implement:

```python
@dataclass(frozen=True)
class RuntimePin:
    repository: str
    source_revision: str
    source_tree: str
    bindings_version: str
    jni_contract_version: int
    engine_compatibility_policy: str
    required_paths: tuple[str, ...]


@dataclass(frozen=True)
class NativeCell:
    build_type: str
    abi: str
    arch: str
    directory: Path
    libraries: tuple[Path, Path]


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def load_pin(path: Path) -> RuntimePin:
    raw = path.read_bytes()
    value = json.loads(raw)
    if raw != canonical_json(value):
        raise ContractError(f"runtime pin is not canonical JSON: {path}")
    validate_exact_pin_shape(value)
    return RuntimePin.from_json(value)


def source_identity(repository: Path, revision: str) -> tuple[str, str]:
    commit = git(repository, "rev-parse", "--verify", f"{revision}^{{commit}}")
    tree = git(repository, "rev-parse", "--verify", f"{commit}^{{tree}}")
    return commit, tree


def foundry_identity(repository: Path) -> tuple[str, str, bool]:
    revision, tree = source_identity(repository, "HEAD")
    dirty = bool(git(repository, "status", "--porcelain", "--untracked-files=no"))
    return revision, tree, dirty


def write_native_provenance(path: Path, identity: dict[str, object]) -> None:
    validate_exact_provenance_shape(identity)
    path.write_bytes(canonical_json(identity))


def validate_native_matrix(root: Path, revision: str, tree: str) -> tuple[NativeCell, ...]:
    cells = tuple(validate_cell(root, spec, revision, tree) for spec in MATRIX)
    reject_unexpected_cell_paths(root, cells)
    return cells


def stage_native_payload(cells: tuple[NativeCell, ...], output: Path) -> None:
    for cell in cells:
        for library in cell.libraries:
            copy_regular_file(library, output / cell.build_type / cell.abi / library.name)
```

Validation must use exact key sets, lowercase 40-character SHAs, canonical bytes, fixed matrix/build flags, regular
files, recomputed sizes/hashes, and no symlinks. `stage_native_payload` copies only the two libraries into the
standalone matrix layout.

- [ ] **Step 5: Run the tests to verify GREEN**

Run:

```sh
python3 -m unittest tests.python_build.test_android_runtime_contract -v
python3 -m py_compile platform/android/android_runtime_contract.py
git diff --check
```

Expected: all contract tests pass, compilation succeeds, and diff check is empty.

- [ ] **Step 6: Commit**

```sh
git add platform/android/foundry_android_runtime.json \
  platform/android/android_runtime_contract.py \
  tests/python_build/test_android_runtime_contract.py
git commit -m "Add Android runtime pin and provenance contract"
```

### Task 2: Exact standalone export and compatibility derivation

**Files:**

- Create: `platform/android/android_runtime_build.py`
- Create: `tests/python_build/test_android_runtime_build.py`

- [ ] **Step 1: Write failing exact-source tests**

Create temporary Git repositories with a minimal standalone layout and two commits. Assert:

```python
resolved = build.export_pinned_source(
    pin=pin,
    source_repository=standalone_repo,
    scratch=scratch,
    allow_fetch=False,
)
self.assertEqual(pin.source_tree, git(resolved, "rev-parse", "HEAD^{tree}"))
self.assertFalse((resolved / ".git").exists())
```

Assert the exported compatibility document and required tool paths are checked before modification. Cover wrong commit
tree, wrong bindings version, wrong JNI contract, missing tool, absent source without fetch, and a different checked-out
branch whose repository still contains the pinned object. The last case must export the pin, not working-tree contents.

- [ ] **Step 2: Write failing truthful-compatibility tests**

Use an exact clean temporary Foundry Git repository and a fake exported `sync_engine_pin.py` that writes its received
revision/version/contract. Assert `derive_compatibility()`:

```python
compatibility = build.derive_compatibility(exported, foundry, pin)
self.assertEqual(foundry_head, compatibility["engine"]["revision"])
self.assertEqual(
    foundry_tree,
    json.loads((exported / "build-input.json").read_bytes())["engine_tree"],
)
self.assertNotEqual(BASELINE_REVISION, compatibility["engine"]["revision"])
```

Assert dirty Foundry source, an unknown revision, wrong standalone contract, and native provenance for a different
revision/tree fail before any Gradle command runs.

- [ ] **Step 3: Write failing prebuilt-bundle compatibility test**

Provide a fake standalone `native_bundle.py validate` that compares its bundle manifest with the derived compatibility
file. Supply a bundle naming `3f1054e65f942375cb7f42f299505220fca990fd` while the clean Foundry test repository is
at a later commit. Assert the driver exits nonzero with `native bundle compatibility metadata mismatch` before invoking
the fake Gradle wrapper.

- [ ] **Step 4: Run the tests to verify RED**

Run:

```sh
python3 -m unittest tests.python_build.test_android_runtime_build -v
```

Expected: import failure because `android_runtime_build.py` does not exist.

- [ ] **Step 5: Implement source export and compatibility stages**

Implement a stdlib-only command-first CLI:

```text
python3 platform/android/android_runtime_build.py prepare
  --pin <canonical-pin>
  --engine-source <clean-foundry-checkout>
  --scratch <ignored-or-shared-scratch>
  --output-aars <ignored-output>
  (--source-repository <git-repository> | --allow-fetch)
  (--native-root <complete-cell-root> | --native-bundle <bundle.zip>)
```

The implementation must:

1. Refuse a scratch/output path that is the repository root or a tracked path.
2. Resolve the pin from an explicit Git repository, or initialize/fetch the exact SHA only when `--allow-fetch` is
   present.
3. Verify commit and tree, export with `git archive`, and validate checked-in compatibility/tool paths.
4. Validate the clean Foundry HEAD/tree and the complete native provenance before deriving metadata.
5. Invoke exported `tools/sync_engine_pin.py derive` with the exact expected revision, pinned bindings version, and JNI
   contract.
6. Record the Foundry source tree in a driver-owned sidecar and require it to match provenance.
7. Create a bundle from staged cells or validate the explicitly supplied bundle with the freshly derived compatibility.
8. Run the exported wrapper with `:runtime:assembleDebug`, `:runtime:assembleDev`, `:runtime:assembleRelease`, and
   `:runtime:verifyJniContract`.
9. Verify the three expected direct AAR outputs and promote them atomically to debug/dev/release output directories.

Subprocesses use argument arrays, captured output, checked return codes, and diagnostics that name the failing stage.
No parent/sibling path search is allowed.

- [ ] **Step 6: Run the tests to verify GREEN**

Run:

```sh
python3 -m unittest \
  tests.python_build.test_android_runtime_contract \
  tests.python_build.test_android_runtime_build -v
python3 platform/android/android_runtime_build.py --help
git diff --check
```

Expected: all tests pass and help lists the explicit source/fetch and native-root/bundle alternatives.

- [ ] **Step 7: Commit**

```sh
git add platform/android/android_runtime_build.py tests/python_build/test_android_runtime_build.py
git commit -m "Build pinned Android runtime from explicit inputs"
```

### Task 3: SCons native cells

**Files:**

- Modify: `platform/android/SCsub`
- Modify: `platform/android/platform_android_builders.py`
- Modify: `tests/python_build/test_android_runtime_contract.py`
- Update: `tests/python_build/test_android_runtime_surface.py`

- [ ] **Step 1: Add failing SCons/provenance contract tests**

Extend tests to parse `SCsub` and call the builder action with fake SCons nodes. Assert:

```python
self.assertNotIn("#platform/android/java/lib/libs/", scsub)
self.assertIn("#bin/android-native/", scsub)
self.assertIn("provenance.json", scsub)
```

Exercise `write_android_native_provenance()` with actual temporary library files and environment dictionaries for all
twelve configurations. Assert canonical JSON, exact revision/tree/build inputs, and byte hashes.

- [ ] **Step 2: Run the focused tests to verify RED**

Run:

```sh
python3 -m unittest tests.python_build.test_android_runtime_contract -v
python3 tests/python_build/test_android_runtime_surface.py
```

Expected: failures showing SCons still writes `java/lib/libs` and no provenance action exists.

- [ ] **Step 3: Redirect outputs and emit provenance**

Change `SCsub` to write:

```text
#bin/android-native/<actual-head>/<build-type>/<abi>/libfoundry_android.so
#bin/android-native/<actual-head>/<build-type>/<abi>/libc++_shared.so
#bin/android-native/<actual-head>/<build-type>/<abi>/provenance.json
```

Use `Copy`/`Move` consistently with existing linker output semantics. Make provenance depend on both libraries and
always refresh it so a new Git revision cannot reuse a stale record. Implement the builder action by calling the shared
contract module's canonical writer, not a second schema implementation.

Update separate-debug-symbol ZIP input paths to the new native root. Keep `generate_android_binaries=yes`, but make its
Gradle invocation forward explicit properties supplied through SCons environment variables and fail if neither a
complete matrix nor prebuilt bundle is provided.

- [ ] **Step 4: Run focused tests and one real cell**

Run:

```sh
python3 -m unittest tests.python_build.test_android_runtime_contract -v
python3 tests/python_build/test_android_runtime_surface.py
python3 misc/scripts/install_swappy_android.py
scons platform=android target=template_debug arch=arm64 production=no \
  dev_mode=no dev_build=no debug_symbols=no tests=no swappy=yes -j8
python3 -m json.tool \
  "bin/android-native/$(git rev-parse HEAD)/debug/arm64-v8a/provenance.json" >/dev/null
```

Expected: tests pass; SCons exits zero; provenance is canonical and hashes the two actual ELF files.

- [ ] **Step 5: Commit**

```sh
git add platform/android/SCsub platform/android/platform_android_builders.py \
  tests/python_build/test_android_runtime_contract.py \
  tests/python_build/test_android_runtime_surface.py
git commit -m "Emit versioned Android native cells"
```

### Task 4: Gradle application consumption

**Files:**

- Modify: `platform/android/java/settings.gradle`
- Modify: `platform/android/java/build.gradle`
- Modify: `platform/android/java/app/build.gradle`
- Modify: `platform/android/java/app/config.gradle`
- Modify: `platform/android/java/gradle/wrapper/gradle-wrapper.properties`
- Create: `tests/python_build/test_android_gradle_runtime_contract.py`

- [ ] **Step 1: Write failing Gradle contract tests**

Assert the configured project keeps only `:app` and `:nativeSrcsConfigs`, defines one explicit runtime-preparation task,
and maps each app variant to one generated AAR directory:

```python
self.assertNotIn("include ':lib'", settings)
self.assertIn("include ':app'", settings)
self.assertIn("include ':nativeSrcsConfigs'", settings)
self.assertIn("prepareFoundryAndroidRuntime", root_build)
for build_type in ("debug", "dev", "release"):
    self.assertIn(f"{build_type}Implementation", app_build)
    self.assertIn(f"foundryRuntimeAarRoot/{build_type}", app_build)
```

Assert no active Gradle file reads `../../../version.py`, invokes SCons through `../../../../`, configures Nexus/Maven
publication, or reads `lib/libs`. Assert `generateFoundryTemplates` depends on preparation and refuses missing explicit
source/fetch and native-root/bundle properties.

- [ ] **Step 2: Run the test to verify RED**

Run:

```sh
python3 -m unittest tests.python_build.test_android_gradle_runtime_contract -v
```

Expected: failures for `:lib`, `version.py`, SCons, and missing preparation.

- [ ] **Step 3: Implement explicit AAR preparation and app dependencies**

Remove the `:lib` include and publication plugins. Register an `Exec` task that invokes:

```groovy
commandLine(
    pythonExecutable,
    rootProject.file('../android_runtime_build.py'),
    'prepare',
    '--pin', rootProject.file('../foundry_android_runtime.json'),
    '--engine-source', foundryEngineSource,
    '--scratch', foundryRuntimeScratch,
    '--output-aars', foundryRuntimeAarRoot,
    sourceArgument,
    nativeArgument,
)
```

Properties are `foundryAndroidSource` or explicit `foundryAndroidFetch=true`, and exactly one of
`foundryNativeRoot`/`foundryNativeBundle`. The Gradle task declares precise inputs/outputs. App assemble/merge tasks
depend on preparation, and `app/build.gradle` uses variant-specific file dependencies.

Keep established template APK/source names. Copy direct standalone AARs into `bin` only as diagnostic artifacts with
their standalone names. Add `distributionSha256Sum` to the Foundry wrapper to match the pinned standalone wrapper.
Remove only the library-version/publication helpers from `app/config.gradle`.

- [ ] **Step 4: Verify Gradle configuration and missing-input diagnostics**

Run:

```sh
python3 -m unittest tests.python_build.test_android_gradle_runtime_contract -v
cd platform/android/java
./gradlew tasks --all
./gradlew generateFoundryTemplates
```

Expected: tests and Gradle configuration pass; template generation fails before assembly with one actionable diagnostic
requiring explicit standalone source/fetch and native root/bundle.

- [ ] **Step 5: Commit**

```sh
git add platform/android/java/settings.gradle platform/android/java/build.gradle \
  platform/android/java/app/build.gradle platform/android/java/app/config.gradle \
  platform/android/java/gradle/wrapper/gradle-wrapper.properties \
  tests/python_build/test_android_gradle_runtime_contract.py
git commit -m "Consume standalone runtime AARs in Android templates"
```

### Task 5: Workflow and release contracts

**Files:**

- Create: `.github/scripts/test_android_runtime_workflows.py`
- Modify: `.github/workflows/android_builds.yml`
- Modify: `.github/workflows/android_java_check.yml`
- Modify: `.github/workflows/release.yml`
- Modify: `.pre-commit-config.yaml`
- Modify: `misc/scripts/test_release_resolver.py`

- [ ] **Step 1: Write failing workflow tests**

Parse workflow text using the existing contract-test style and assert:

```python
EXPECTED_MATRIX = {
    ("debug", "armeabi-v7a"), ("debug", "arm64-v8a"),
    ("debug", "x86"), ("debug", "x86_64"),
    ("dev", "armeabi-v7a"), ("dev", "arm64-v8a"),
    ("dev", "x86"), ("dev", "x86_64"),
    ("release", "armeabi-v7a"), ("release", "arm64-v8a"),
    ("release", "x86"), ("release", "x86_64"),
}
self.assertEqual(EXPECTED_MATRIX, parsed_native_matrix)
```

Require each cell artifact to include provenance and the assembly job to depend on all native cells. Require an exact
standalone commit checkout/source input, explicit native-root input, template generation for all three build types, and
stable APK/source artifact names. Require release to use the same full matrix and forbid `:lib:publish`,
Sonatype/Nexus credentials, and Foundry-side Android Maven publication.

- [ ] **Step 2: Run the tests to verify RED**

Run:

```sh
python3 .github/scripts/test_android_runtime_workflows.py
python3 misc/scripts/test_release_resolver.py
```

Expected: workflow contract failures for the partial matrix, implicit Gradle path, and old Maven publication.

- [ ] **Step 3: Implement the full CI matrix and assembly gate**

Refactor `android_builds.yml` into:

1. static contract validation;
2. a twelve-cell SCons matrix with exact flags and Swappy;
3. per-cell artifact upload of libraries plus provenance;
4. an assembly job that resolves its checked-out `git rev-parse HEAD`, explicitly checks out or fetches the pinned
   standalone commit, downloads every cell, and passes explicit source/native-root/scratch properties;
5. template/AAR/bundle artifact upload.

Make `android_java_check.yml` call the reusable authoritative workflow for Android-affecting PRs instead of maintaining
a different one-cell path.

- [ ] **Step 4: Implement the release migration**

Replace the two partial Android release jobs with a full native matrix and one dependent assembly job. Preserve
`release-android-template-debug` and `release-android-template-release` artifact names expected by release packaging;
place `android_source.zip` in the release artifact. Remove the Maven publication step and secrets. Ensure release
packaging depends on the successful complete assembly gate.

- [ ] **Step 5: Wire and run workflow validation**

Add a focused pre-commit hook for the workflow contract. Run:

```sh
python3 .github/scripts/test_android_runtime_workflows.py
python3 misc/scripts/test_release_resolver.py
pre-commit run foundry-android-runtime-workflows --all-files
actionlint .github/workflows/android_builds.yml \
  .github/workflows/android_java_check.yml .github/workflows/release.yml
```

Expected: every command exits zero.

- [ ] **Step 6: Commit**

```sh
git add .github/scripts/test_android_runtime_workflows.py \
  .github/workflows/android_builds.yml .github/workflows/android_java_check.yml \
  .github/workflows/release.yml .pre-commit-config.yaml misc/scripts/test_release_resolver.py
git commit -m "Build standalone Android runtime in CI and releases"
```

### Task 6: Remove the duplicate runtime and publication

**Files:**

- Delete: `platform/android/java/lib/`
- Delete: `platform/android/java/scripts/publish-module.gradle`
- Delete: `platform/android/java/scripts/publish-root.gradle`
- Delete: `platform/android/java/PUBLISHING.md`
- Modify: `tests/python_build/test_android_runtime_surface.py`
- Modify: `misc/scripts/test_android_runtime_identifiers.py`
- Modify: `.pre-commit-config.yaml`

- [ ] **Step 1: Change source-surface tests first**

Update the tests to require:

```python
forbid_path("platform/android/java/lib")
forbid_path("platform/android/java/scripts/publish-module.gradle")
forbid_path("platform/android/java/scripts/publish-root.gradle")
require_path("platform/android/java/app")
require_path("platform/android/java/nativeSrcsConfigs")
require_path("platform/android/foundry_android_runtime.json")
```

The identifier test must validate only Foundry-owned `:app`, native JNI, exporter strings, and the canonical standalone
pin. Runtime package/plugin/manifest checks belong to the pinned standalone verification invoked by the build driver.
Update pre-commit paths so removal of `java/lib` cannot skip the remaining identifier contract.

- [ ] **Step 2: Run tests to verify RED**

Run:

```sh
python3 tests/python_build/test_android_runtime_surface.py
python3 misc/scripts/test_android_runtime_identifiers.py
```

Expected: failures because the duplicate library and publication files still exist.

- [ ] **Step 3: Delete only after all consumers have migrated**

Delete the listed runtime/publication paths. Search the repository and remove active references:

```sh
rg -n "platform/android/java/lib|include ':lib'|:lib:|lib/libs|foundry-lib\\.template|publish-module|publish-root" \
  --glob '!docs/superpowers/**'
```

Required remaining hits are limited to historical/provenance text that is explicitly exempted by tests. Do not delete
`:app`, `nativeSrcsConfigs`, C++/JNI, exporter, or SCons native production.

- [ ] **Step 4: Run tests to verify GREEN**

Run:

```sh
python3 tests/python_build/test_android_runtime_surface.py
python3 misc/scripts/test_android_runtime_identifiers.py
python3 -m unittest \
  tests.python_build.test_android_runtime_contract \
  tests.python_build.test_android_runtime_build \
  tests.python_build.test_android_gradle_runtime_contract -v
git diff --check
```

Expected: every command exits zero and the source contract reports the standalone boundary.

- [ ] **Step 5: Commit**

```sh
git add -A platform/android/java/lib platform/android/java/scripts \
  platform/android/java/PUBLISHING.md tests/python_build/test_android_runtime_surface.py \
  misc/scripts/test_android_runtime_identifiers.py .pre-commit-config.yaml
git commit -m "Remove duplicate Android runtime sources"
```

### Task 7: Documentation and artifact inspection

**Files:**

- Modify: `platform/android/README.md`
- Create: `platform/android/ANDROID_RUNTIME.md`
- Modify: `tests/python_build/test_android_gradle_runtime_contract.py`

- [ ] **Step 1: Add failing documentation/artifact assertions**

Assert documentation names the exact pin/update command, explicit source/fetch choice, explicit native-root/bundle
choice, full matrix, offline flow, scratch/output locations, compatibility failure behavior, and no-device boundary.
Add ZIP inspection helpers that require:

```text
android_source.zip:
  app/libs/debug/foundry-debug.aar
  app/libs/dev/foundry-dev.aar
  app/libs/release/foundry-release.aar
```

and forbid runtime Java/Kotlin/AIDL source outside the kept app template.

- [ ] **Step 2: Run tests to verify RED**

Run:

```sh
python3 -m unittest tests.python_build.test_android_gradle_runtime_contract -v
```

Expected: documentation and artifact-inspector requirements fail.

- [ ] **Step 3: Write migration/build documentation and inspector**

Document:

- authoritative full-matrix commands;
- explicit pre-fetched source and prebuilt bundle commands;
- opt-in exact fetch;
- pin update procedure with commit/tree/bindings/JNI review;
- clean-source and provenance requirements;
- stable template names and standalone AAR names;
- Foundry-Android as sole Maven publisher;
- narrower local builds as non-authoritative;
- no device/emulator claim and #1224 ownership.

Make template generation inspect the final source ZIP before promotion.

- [ ] **Step 4: Verify and commit**

Run:

```sh
python3 -m unittest tests.python_build.test_android_gradle_runtime_contract -v
git diff --check
```

Then:

```sh
git add platform/android/README.md platform/android/ANDROID_RUNTIME.md \
  tests/python_build/test_android_gradle_runtime_contract.py
git commit -m "Document standalone Android runtime inputs"
```

### Task 8: Full real Android gate

**Files:**

- No planned source changes; failures require systematic debugging and a new RED regression before fixes.

- [ ] **Step 1: Build the twelve native cells**

From the exact clean issue branch, install Swappy and run the exact matrix. Use a distinct shared scratch progress log
and the existing SCons cache. For each build type/ABI, run the flags from Task 3. Expected: twelve canonical cells under
`bin/android-native/$(git rev-parse HEAD)`.

- [ ] **Step 2: Prove determinism and mismatch rejection**

Run preparation twice into separate scratch directories from:

```text
standalone source: /Users/christian/CafecitoGames/Foundry-Android
native root: bin/android-native/<exact-head>
```

Expected: native bundles are byte-identical; all three AAR SHA-256 values match between runs. Then explicitly pass the
existing `3f1054e65f942375cb7f42f299505220fca990fd` bundle and expect compatibility mismatch before Gradle.

- [ ] **Step 3: Run the authoritative template gate**

Run:

```sh
cd platform/android/java
./gradlew --no-daemon clean generateFoundryTemplates \
  -PfoundryAndroidSource=/Users/christian/CafecitoGames/Foundry-Android \
  -PfoundryNativeRoot="$PWD/../../../bin/android-native/$(git -C ../../.. rev-parse HEAD)" \
  -PfoundryRuntimeScratch="$PWD/../../../.test_scratch/issue-1225-runtime"
```

Expected: debug/dev/release standalone AARs, Android APK templates, and `android_source.zip` pass compatibility/JNI/AAR
and source-ZIP inspection across all four ABIs.

- [ ] **Step 4: Run strict repository verification**

Run on this macOS host:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes
python3 -m unittest \
  tests.python_build.test_android_runtime_contract \
  tests.python_build.test_android_runtime_build \
  tests.python_build.test_android_gradle_runtime_contract -v
python3 tests/python_build/test_android_runtime_surface.py
python3 misc/scripts/test_android_runtime_identifiers.py
python3 .github/scripts/test_android_runtime_workflows.py
pre-commit run --all-files
./bin/foundry.* --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-1225-test-progress.jsonl \
  --force-colors
```

Use the built macOS binary matching `bin/foundry.*`. Trust the final doctest summary while separately reporting known
cleanup leak output. Expected: strict build, focused contracts, pre-commit, and full suite all pass.

- [ ] **Step 5: Record verification**

Add the exact commands, revisions, bundle/AAR hashes, task counts, and no-device boundary to the PR body draft. Do not
claim device smoke coverage.

### Task 9: Cursor convergence, PR, merge, and cleanup

**Files:**

- Modify only if a technically validated Cursor finding requires an in-scope fix; add a RED regression first.

- [ ] **Step 1: Rebase/refresh and commit final verification state**

Fetch `origin/develop`, confirm the branch diff and clean status, and commit any final documentation-only verification
updates. Do not rebase over changed Android integration without re-running the complete gate.

- [ ] **Step 2: Run one read-only Cursor round**

From the issue worktree, set `CURSOR_REVIEW_BASE=origin/develop` and run the exact foreground command from the
`cursor-review` skill. Require parseable output and an unchanged HEAD/status.

- [ ] **Step 3: Triage and converge**

For every finding, use `superpowers:receiving-code-review`; for real bugs, use systematic debugging and TDD. Re-run
focused plus relevant broad verification, commit, and repeat Cursor on the new HEAD until the exact latest result is:

```text
RESULT: clean
FINDINGS:
- none
```

- [ ] **Step 4: Open the ready PR and enable auto-merge**

Push `issue-1225`, open a ready PR against `develop`, and end the body with:

```text
Closes #1225
```

Enable squash auto-merge, monitor every required check to green, and inspect any failure before changing code.

- [ ] **Step 5: Verify closure and clean only owned state**

After merge, verify:

- PR state `MERGED`;
- issue #1225 `CLOSED` with completed reason;
- Experiment project status `Done`;
- merged commit on `origin/develop`.

Then remove only `.worktrees/issue-1225`, local branch `issue-1225`, and its remote branch if GitHub did not delete it.
Preserve every unrelated main-checkout file, worktree, branch, and standalone repository.
