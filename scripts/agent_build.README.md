# Agent build wrapper

`scripts/agent_build.py` provides stable, worktree-specific logs, JSONL progress, and build summaries
for agent-driven builds. Native SCons remains the default backend and the required final-validation
backend.

## Native validation build

```sh
python3 scripts/agent_build.py
```

The default build enables both `dev_mode=yes` and `dev_build=yes`, builds tests, uses all CPUs the
workspace is actually allowed to use, and shares SCons objects through `$HOME/.scons_cache`.
`--dev-build` omits `dev_mode=yes` for faster iteration only; rerun the default native strict build
before handoff.

## Build concurrency

The job count is resolved in this order, highest precedence first:

1. `--jobs <n>`.
2. `FOUNDRY_BUILD_JOBS=<n>` in the environment.
3. The effective cgroup CPU quota, when the process runs under one (cgroup v2 `cpu.max` or cgroup v1
   CFS quota, whichever is tightest, rounded down and clamped to at least one job).
4. The host CPU count.

Step 3 exists because a container can see every host CPU through `nproc`/`os.cpu_count()` while being
allowed only a fraction of them. Defaulting to the host count there launches far more compilers than
the workspace can sustain, and builds fail to spawn processes instead of merely running slowly.

The wrapper prints the decision at startup:

```text
[agent-build] jobs: 4 (source: cgroup-cpu-quota)
```

and records it as `jobs` and `jobs_source` in the final `build_summary` progress event. `jobs_source`
is one of `--jobs`, `FOUNDRY_BUILD_JOBS`, `cgroup-cpu-quota`, or `host-cpu-count`. A non-positive or
non-numeric `FOUNDRY_BUILD_JOBS` is a hard error rather than a silent fallback.

## Prerequisites

- SCons.
- ccache 4.x, installed separately through the platform's package manager or equivalent.
- Ninja 1.13 or newer.
- SCons' Python Ninja tool dependency. A local smoke test proved that an environment can have SCons
  and the `ninja` executable yet still fail with `Failed to import ninja`. Install it explicitly with
  `python3 -m pip install scons ninja`, or use the equivalent installation for the active Python
  environment.
- The benchmark runner targets POSIX macOS and Linux.

## Focused test runs

`--case <pattern>` is repeatable and implies `--test`. Every occurrence is retained and
forwarded to `foundry test run` as its own `--case`, so the selected tests are the union
of all supplied patterns (a test runs when its name matches any of them):

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*FoundryCLIParser*" --case "*FoundryCLI*TestRun*"
```

`--suite <pattern>` behaves the same way for doctest suite names, and combines with `--case`:
the selected tests are the union of every supplied case and suite pattern. Use it when the
pattern names a suite rather than a case, since `--case` matches case names only:

```sh
python3 scripts/agent_build.py --backend ninja --test --suite "*[Modules][FoundryScript][Format]*"
```

A filter that matches no test is an error, so an empty result means the pattern is wrong, not
that the run was trivially clean.

## Ninja + ccache pilot

```sh
python3 scripts/agent_build.py --backend ninja
```

In this pilot, Ninja implies and requires ccache. It disables SCons `CacheDir`, uses relative debug
paths, and defaults to the shared `$HOME/.cache/foundry-ccache`. Each configuration has ignored state
at `.ninja/agent-build/<hash>/build.ninja`. SCons generates cold state, then Ninja regeneration tracks
build-description dependencies.

## Known limitations

This pilot supports normal fresh-worktree and incremental workflows. The wrapper never calls
`ninja -t clean`, and wrapper-only builds after an external `ninja -t clean` are unsupported. SCons
4.10.1's interactive Ninja `TEMPLATE` daemon can report generated `.inc` and `.cpp` targets complete
before their files exist. The measured 47.480-second APFS materialization therefore required a
synchronous 168-target generated-input preflight after cleaning compiled outputs; it is a qualified
cache-materialization result, not a clean wrapper-only build result.

Use a fresh worktree for a cold-output workflow. Do not clean an active Ninja graph and assume the
wrapper can reconstruct its generated inputs until the upstream daemon race is resolved.

For native SCons while still using ccache:

```sh
python3 scripts/agent_build.py --backend scons --compiler-cache ccache
```

The native fallback disables the compiler cache:

```sh
python3 scripts/agent_build.py --backend scons --compiler-cache none
```

On APFS, `--ccache-file-clone` enables ccache file cloning. It disables compression and increases
cache size:

```sh
python3 scripts/agent_build.py --backend ninja --ccache-file-clone
```

The hermetic ccache policy uses ccache's effective default maximum of 5 GiB. ccache evicts older
entries as needed, and file-clone mode reaches that limit faster because cached objects are not
compressed. Inspect a cache without deleting or mutating it:

```sh
CCACHE_DIR="$HOME/.cache/foundry-ccache" ccache --show-stats
CCACHE_DIR="$HOME/.cache/foundry-ccache" ccache --show-config
```

## Logs and progress

Default log and progress-file names include a stable, safe hash of the resolved worktree path. Override
them with `--log` and `--progress-file`; use `--no-progress-file` when a persistent JSONL file is not
useful. Command events and the build summary carry the same invocation ID.

Use `--progress-format jsonl` when stdout must be machine-readable; human build output moves to stderr.
The wrapper collects ccache telemetry using a unique per-invocation `CCACHE_STATSLOG`, so concurrent
worktrees do not race on statistics. Telemetry is best-effort: summaries report explicit status/errors,
and telemetry never replaces the build result.

Progress records cover an `invocation_start` marker, wrapper command start, output, heartbeat,
completion, and the final `build_summary`. They do not provide detailed visibility into every internal
SCons or Ninja phase.

Unless `--append-progress` is given, the progress file is truncated and `invocation_start` is written
before any other work, so a waiter can never match a `build_summary` left behind by an earlier
invocation. Match `invocation_id` between `invocation_start` and `build_summary` when a waiter must be
certain it is reading the run it launched.

## Build verdict

The wrapper's exit code reflects the build, not whatever the wrapper did last, and it never reports a
success `build_summary` for a build that failed. After a build command exits `0` the wrapper still
verifies the result and fails when either check trips:

- the build output for this invocation contains failure evidence (`N error(s) generated.`,
  `scons: *** `, `FAILED: `, `ninja: build stopped:`). This catches a build status masked by a shell
  or lost by a backend. Exit code `1`, `build_summary.status` `failed`, and the matching lines are
  recorded in `build_summary.build_failure_signals`.
- no editor binary is present in `bin/`, meaning no final link occurred. Exit code `127` and
  `build_summary.status` `failed`.

The link check matches `bin/foundry.<platform>.editor*`, so build settings that rename the binary
(`precision`, `extra_suffix`, sanitizers, alternate toolchains) are not mistaken for a failed build.
A `--scons-arg` invocation that deliberately builds something other than an editor binary is still
reported as a failure. When `--test` runs and the expected name is absent, the wrapper tests the sole
editor binary it finds, and fails only when the choice is ambiguous. Startup failures (missing SCons,
Ninja, or ccache) also emit a `build_summary` with status `error`, so a waiter is never left without a
verdict.

## Benchmarks

Write benchmark artifacts outside tracked fixtures, for example under a `mktemp` directory:

```sh
benchmark_dir="$(mktemp -d)"
scripts/benchmark_agent_build.py --label ninja-no-op --output "$benchmark_dir/benchmark.jsonl" --repeat 3 \\
  -- python3 scripts/agent_build.py --backend ninja
```

The runner stops at the first nonzero child result and appends one JSONL record per completed child with
wall, user, system, maximum RSS, and input/output I/O. Maximum RSS is normalized to KiB. It is a POSIX
macOS/Linux tool.

The wrapper's agent-build summary is written separately to its default progress file unless the child
also receives an explicit `--progress-file`. For a clean run with unique artifacts:

```sh
benchmark_dir="$(mktemp -d)"
scripts/benchmark_agent_build.py --label ninja-no-op --output "$benchmark_dir/benchmark.jsonl" --repeat 3 \\
  -- python3 scripts/agent_build.py --backend ninja --progress-file "$benchmark_dir/agent-progress.jsonl"
```

## Correctness and final validation

Inspect representative object debug info with `dwarfdump` or `llvm-dwarfdump`: paths must resolve in
the invoking worktree. Before handoff, run the default native strict SCons build and the full
command-first test suite for the host platform.

macOS arm64:

```sh
python3 scripts/agent_build.py
./bin/foundry.macos.editor.dev.arm64 --headless test run --force-colors
```

Linux x86_64, including GUI-dependent acceptance subprocesses through the configured X display:

```sh
python3 scripts/agent_build.py --platform linuxbsd
DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --force-colors
```

Substitute the architecture suffix when building for another host architecture.

This pilot intentionally does not change the linker or add a global job coordinator. Those remain
measurement-driven follow-ups, not hidden features.
