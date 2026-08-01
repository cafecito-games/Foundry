# Agent build wrapper

`scripts/agent_build.py` provides stable, worktree-specific logs, JSONL progress, and build summaries
for agent-driven builds. Native SCons remains the default backend and the required final-validation
backend.

## Native validation build

```sh
python3 scripts/agent_build.py
```

The default build enables both `dev_mode=yes` and `dev_build=yes`, builds tests, uses all host CPUs,
and shares SCons objects through `$HOME/.scons_cache`. `--dev-build` omits `dev_mode=yes` for faster
iteration only; rerun the default native strict build before handoff.

## Prerequisites

- SCons.
- ccache 4.x, installed separately through the platform's package manager or equivalent.
- Ninja 1.13 or newer.
- SCons' Python Ninja tool dependency. A local smoke test proved that an environment can have SCons
  and the `ninja` executable yet still fail with `Failed to import ninja`. Install it explicitly with
  `python3 -m pip install scons ninja`, or use the equivalent installation for the active Python
  environment.
- The benchmark runner targets POSIX macOS and Linux.

## Ninja + ccache pilot

```sh
python3 scripts/agent_build.py --backend ninja
```

In this pilot, Ninja implies and requires ccache. It disables SCons `CacheDir`, uses relative debug
paths, and defaults to the shared `$HOME/.cache/foundry-ccache`. Each configuration has ignored state
at `.ninja/agent-build/<hash>/build.ninja`. SCons generates cold state, then Ninja regeneration tracks
build-description dependencies.

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

## Logs and progress

Default log and progress-file names include a stable, safe hash of the resolved worktree path. Override
them with `--log` and `--progress-file`; use `--no-progress-file` when a persistent JSONL file is not
useful. Command events and the build summary carry the same invocation ID.

Use `--progress-format jsonl` when stdout must be machine-readable; human build output moves to stderr.
The wrapper collects ccache telemetry using a unique per-invocation `CCACHE_STATSLOG`, so concurrent
worktrees do not race on statistics. Telemetry is best-effort: summaries report explicit status/errors,
and telemetry never replaces the build result.

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
command-first test suite:

```sh
python3 scripts/agent_build.py
./bin/foundry.* --headless test run --force-colors
```

This pilot intentionally does not change the linker or add a global job coordinator. Those remain
measurement-driven follow-ups, not hidden features.
