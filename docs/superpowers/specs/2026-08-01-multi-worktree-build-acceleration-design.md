# Multi-Worktree Build Acceleration Design

## Summary

Foundry will add an opt-in local fast-build path based on SCons-generated Ninja files, ccache, and a machine-wide
parallelism budget. Native SCons remains the validation and fallback backend until the fast path demonstrates clean,
incremental, generated-source, debugger, and full-suite parity.

The work begins with reproducible benchmarks. Defaults change only when measurements show a material improvement and
the parity checks pass. This design targets multiple local agents building macOS worktrees first, while keeping the
compiler-cache and linker choices portable to Linux cloud agents.

## Current State

Foundry already contains several relevant mechanisms:

- `env.Decider("MD5-timestamp")` is enabled unconditionally. This is the deprecated spelling of SCons'
  `content-timestamp` decider.
- `ninja=yes` enables the experimental SCons Ninja generator, with optional automatic execution.
- `c_compiler_launcher` and `cpp_compiler_launcher` can prefix compiler commands with ccache or sccache.
- `scripts/agent_build.py` passes the shared `$HOME/.scons_cache` to SCons by default.
- Linux supports the `bfd`, `gold`, `lld`, and `mold` linkers. macOS has no equivalent Foundry linker option.

The current setup does not produce reliable cross-worktree reuse:

- macOS compiler commands contain an absolute worktree-specific `thirdparty/metal-cpp` include path, changing SCons
  build signatures between worktrees.
- debug objects record an absolute compilation directory.
- the SCons Ninja generator emits ordinary compiler actions as direct Ninja commands, so SCons `CacheDir` is not the
  compiler cache for that backend.
- every agent build defaults to all host CPUs. Concurrent agents can therefore oversubscribe the same machine.
- default agent logs and progress files use global `/tmp` names, so concurrent invocations overwrite one another.

Observed local data provides the baseline motivation:

- warm native-SCons evaluations with no C/C++ compilation took approximately 23 to 30 seconds;
- a recent macOS build took approximately 10 minutes 38 seconds, compiled 617 translation units, and retrieved only
  71 artifacts from the 20 GiB SCons cache;
- the final executable link in that build took approximately 2.3 seconds;
- build outputs across active worktrees occupied approximately 29 GiB in addition to the 20 GiB SCons cache.

These observations make compiler reuse, dependency evaluation, and aggregate scheduling higher priorities than a
macOS linker replacement.

## Goals

1. Reduce warm no-op and small-edit build latency in an existing worktree.
2. Reuse unchanged compilation results when an agent creates or switches to another worktree.
3. Improve total throughput when two or more agents build concurrently without exhausting CPU or memory.
4. Preserve correct dependency rebuilding, generated-source behavior, debug information, and test results.
5. Make cache behavior and build phases measurable enough to diagnose misses and regressions.
6. Retain a documented native-SCons fallback throughout rollout.

## Non-Goals

- Replacing SCons as Foundry's authoritative build description language.
- Sharing object or binary output directories directly between worktrees.
- Introducing a remote cache service during the initial local-macOS rollout.
- Making an alternative linker part of release or distribution builds without independent validation.
- Enabling unsafe SCons dependency shortcuts by default.
- Optimizing Android's multi-cell template build in the first implementation cycle.

## Considered Approaches

### Native SCons with `CacheDir`

This has the smallest compatibility risk and caches arbitrary derived targets, including generated files and static
libraries. It does not address SCons graph-evaluation latency, stores large linked artifacts, and currently has
worktree-specific command signatures. Correct debug-source identity across worktrees also needs explicit treatment.

This remains the fallback and comparison backend, not the primary fast path.

### Ninja with ccache

SCons continues to define the dependency graph and generates a per-worktree Ninja file. Ninja performs fast timestamp
evaluation and schedules ordinary commands. ccache supplies content-addressed compiler reuse across worktrees. A
machine-wide job pool prevents independent Ninja processes from multiplying the host's available parallelism.

This is the selected approach because ccache is already installed in the primary environment, is mature for local
C/C++ compilation, exposes detailed statistics, supports worktree path normalization, and can use safe APFS file
cloning when a benchmark justifies the storage trade-off.

### Ninja with sccache

This is architecturally similar to the selected approach and is a good future extension for remote or multi-level
caches shared across machines. It adds installation and service configuration without a clear advantage for multiple
worktrees on one Mac. The local rollout will keep the compiler-launcher interface generic so sccache can be evaluated
later without redesigning the backend selection.

## Architecture

### Build Backend Selection

`scripts/agent_build.py` will own a documented backend choice:

- `scons`: native SCons scheduling and the existing derived-file cache behavior;
- `ninja`: generate or refresh the Ninja description through SCons, then execute Ninja directly.

The native backend remains the default during the pilot. After the acceptance criteria pass and the fast path has been
used successfully on real agent work, making Ninja the local default will be a separate, explicit decision. CI stays
on native SCons initially so it continues exercising the authoritative execution backend.

Ninja state must be local to a worktree and distinct for incompatible configurations. The state identity will include
at least platform, architecture, target, development mode, tests setting, and other flags that change outputs. A build
must regenerate the Ninja description when SCons inputs, `SConstruct`, `SCsub` files, or the configuration identity
change.

### Compiler Cache

The Ninja backend will use exactly one compiler cache launcher. Its initial implementation uses ccache through
`c_compiler_launcher=ccache` and `cpp_compiler_launcher=ccache`.

The cache configuration will:

- use one Foundry-specific cache directory shared by local worktrees;
- set the base directory to the current worktree root for each invocation so equivalent absolute paths normalize to
  equivalent relative paths;
- use a Foundry namespace so statistics and eviction can be managed independently;
- retain ccache's compiler-identity safety checks;
- expose hit, miss, uncacheable, read, write, and cache-size statistics in the build progress record;
- start with normal compressed storage.

SCons `CacheDir` will be disabled for the Ninja benchmark and fast path. Layering it over ccache would duplicate object
storage and hashing while not serving ordinary direct Ninja compiler commands. Native-SCons benchmark variants may
continue using `CacheDir` for comparison.

APFS `file_clone` mode will be a separate benchmark variant. It may reduce the cost of materializing large debug
objects in a new worktree, but it will not become the default unless its wall-time improvement justifies the larger
uncompressed cache.

### Path-Stable Debug Information

Cross-worktree compiler reuse must not silently return an object whose debug metadata points at another agent's
worktree. The fast path will enable path normalization supported by the existing `debug_paths_relative` option and
will verify the resulting DWARF compilation directory and source names.

Acceptance includes resolving a representative source breakpoint or symbol from the worktree that invoked the build.
If relative debug paths do not resolve reliably when the binary is launched outside the repository root, the
implementation must use a canonical debug prefix plus an explicit debugger source map, or keep debug-symbol builds
worktree-specific. Disabling ccache's working-directory hash without an equivalent debug-path solution is not allowed.

Absolute compiler flags that vary only because of the checkout root, including the metal-cpp system include, will be
made cache-stable or normalized by the compiler cache. Any build-script change must preserve system-header warning
behavior and include ordering.

### Machine-Wide Parallelism

A build launched in one worktree must not assume it owns every host CPU. The fast path will use a machine-wide slot
pool shared by independent agent processes. On POSIX hosts with Ninja 1.13 or later, the preferred mechanism is the
GNU FIFO jobserver protocol. Ninja must not receive an explicit `-j` value when it is consuming jobserver tokens.

The coordinator will:

- create a user-specific, machine-local pool with a configurable capacity;
- clean up stale ownership without deleting another active build's state;
- default capacity from the host CPU count, with a user override;
- reserve the ability to define a lower link pool if memory measurements require it;
- fail safely by using a conservative local job count when the shared coordinator is unavailable.

Native SCons cannot be assumed to consume the Ninja jobserver. During the pilot, concurrent native builds will use a
coarser wrapper-level limit or a divided per-build job count. The benchmark will compare one full-width build, two
half-width builds, and four quarter-width builds before choosing the fallback policy.

### Observability

Each invocation will use worktree-specific log and progress paths by default. Their names will be stable enough for an
agent to discover, but distinct enough that two worktrees cannot overwrite each other.

Progress records will distinguish at least:

- SCons/Ninja generation;
- dependency evaluation;
- compilation/cache materialization;
- archive and link work;
- tests;
- compiler-cache statistics before and after the build;
- selected backend, cache mode, configuration identity, and global job-pool capacity.

The wrapper will print a concise summary containing wall time, backend, cache hit rate, and output paths. Detailed
compiler output remains in the existing human-readable log.

### Linkers

Linker work follows the cache and scheduler rollout.

- Linux fast builds will benchmark the existing `mold` and `lld` options against the default linker. The faster option
  may become a local fast-mode default only when available and after tests pass.
- macOS will support an opt-in `ld64.lld` experiment if the tool is installed. The current Apple linker remains the
  default unless the single-source-edit benchmark shows a material end-to-end improvement.
- zld will not be supported because the project is archived in favor of LLD.
- alternative linkers will not become release defaults as part of this work.

## Build Flow

For a Ninja/ccache build:

1. The wrapper resolves the worktree root and canonical build configuration.
2. It selects worktree-specific logs and Ninja state.
3. It configures the shared compiler cache using the current worktree as the normalization base.
4. It joins or starts the machine-wide job pool.
5. It runs SCons only when the Ninja description is absent or stale.
6. Ninja evaluates timestamps and executes required actions.
7. Compiler commands query ccache; a hit materializes the cached result, while a miss compiles and populates the
   cache.
8. Ninja performs required generated-file, archive, and link actions locally in the worktree.
9. The wrapper records final cache deltas and timing data.
10. If requested, the worktree's resulting Foundry binary runs the selected tests.

## Failure Handling and Fallback

- Missing Ninja or ccache produces an actionable diagnostic naming the missing dependency and the native-SCons
  fallback command.
- Ninja generation failure stops the build; the wrapper does not execute an older description whose configuration is
  uncertain.
- Compiler-cache failures must not return stale output. Recoverable cache-service errors may fall back to uncached
  compilation; correctness or corruption errors fail the build and identify the cache diagnostic path.
- Job-pool failure falls back to a conservative bounded job count and records that degraded mode.
- A native-SCons backend flag remains available even after any future default change.
- A documented cache-disable option supports correctness comparison and recovery without deleting shared data.
- Cache cleanup is explicit and bounded to the resolved Foundry cache directory. It never targets a worktree or broad
  home-directory path.

## Benchmark Plan

The initial matrix compares:

1. native SCons with the current shared `CacheDir`;
2. native SCons with ccache and no `CacheDir`;
3. Ninja with ccache and no `CacheDir`;
4. Ninja with ccache file cloning on APFS;
5. the selected fast backend under one, two, and four concurrent worktrees.

Each variant runs these scenarios from equivalent repository states:

- cold output and cold compiler cache;
- cold output and warm compiler cache at the same revision;
- warm no-op build;
- one implementation-file change;
- one representative shared-header change;
- one generated-source input change;
- one build-description change;
- full test execution after the build.

The report records median and worst wall time over repeated runs, CPU time, peak resident memory, bytes read/written,
cache hit categories, uncacheable calls, output size, cache size, and link duration. It must retain raw commands and
progress records so results can be reproduced.

## Acceptance Criteria

The fast path may be recommended for general local use only when:

- clean and incremental builds complete successfully on macOS;
- the full C++ and Foundry Script suite has the same pass/fail outcome as the native backend;
- source, header, generated-input, and `SCsub` changes rebuild the expected observable artifacts;
- a new worktree at the same revision obtains substantial compiler-cache reuse without incorrect debug paths;
- repeated warm no-op builds materially outperform the observed native-SCons baseline;
- concurrent builds respect the aggregate job capacity and improve total throughput without unacceptable memory or
  interactive-system pressure;
- switching supported configurations cannot reuse an incompatible Ninja description or cached object;
- cache corruption and missing-tool fallback paths are exercised;
- cache and output growth have documented, bounded defaults;
- the native-SCons fallback remains green.

No fixed percentage is imposed before the benchmark because the first measurements must separate graph evaluation,
cache materialization, compilation, archiving, and linking. The benchmark report will recommend quantitative default
thresholds based on observed variance.

## Rollout

1. Add benchmark instrumentation and per-worktree log identities without changing the build backend.
2. Benchmark native SCons with ccache against the current `CacheDir` baseline.
3. Resolve path-stable debug information and verify cross-worktree cache correctness.
4. Add the opt-in Ninja/ccache backend and run the parity matrix.
5. Add the shared parallelism coordinator and run concurrent-worktree measurements.
6. Pilot the fast path with local agents while native SCons remains the default.
7. Decide separately whether to make the fast path the local default.
8. Benchmark Linux linkers and optional Mach-O LLD after compiler and scheduler gains are established.
9. Evaluate sccache only when sharing results across machines becomes a concrete requirement.
