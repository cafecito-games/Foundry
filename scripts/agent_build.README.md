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

and records it as `jobs` and `jobs_source` in the `run_start` and `build_summary` progress events. `jobs_source`
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
useful. Every record of one run carries the same invocation ID, which `--invocation-id` lets a caller
choose in advance.

Use `--progress-format jsonl` when stdout must be machine-readable; human build output moves to stderr.
The wrapper collects ccache telemetry using a unique per-invocation `CCACHE_STATSLOG`, so concurrent
worktrees do not race on statistics. Telemetry is best-effort: summaries report explicit status/errors,
and telemetry never replaces the build result.

Progress records are `run_start`, wrapper command start, output, heartbeat and completion,
`build_summary`, and `run_end`. They do not provide detailed visibility into every internal SCons or
Ninja phase. `build_summary` describes the build step only: under `--test` it stays `success` when the
build succeeded even if the tests then fail.

`run_start` is the first thing the process does, before any tooling check, and it replaces the
progress file's contents unless `--append-progress` is given, so a stale record from a previous
invocation can never be read as this one's. It carries `invocation_id`, `argv`, `pid`, `worktree`,
`backend`, `compiler_cache`, `platform`, `arch`, `jobs`, `jobs_source`, `git_commit`, `log_path`,
`binary_path`, and `binary_before`.

`run_end` is the last record of every invocation, emitted exactly once alongside the `RESULT:` line
and with the same `status` and `exit_code`, including when the run aborts during startup and when it
runs tests. It carries `invocation_id`, `status`, `step`, `exit_code`, `duration_ms`, `binary_path`,
`binary_before`, `binary_after`, `binary_changed`, `child_disposition`, and `child_exit_code`.

A binary identity block — `binary_before`, `binary_after`, and the `binary_after` on `build_summary` —
is `{"path": ..., "size": ..., "mtime_ns": ...}`, or `null` when the file is absent. `binary_changed`
is `binary_after != binary_before`, and both are read from the *same* path: build settings the wrapper
cannot reconstruct rename the binary, so the identity a run reports is compared against what that same
file was before the build, not against the predicted name. `run_start`'s `binary_before` is the
predicted path's pre-build identity; `run_end`'s is the reported path's. It is not a failure when
`binary_changed` is `false`: a no-op incremental build legitimately leaves the binary untouched, and
the field exists so a caller can decide.

### Waiting for a build from another process

`--invocation-id` makes a run addressable, and the wrapper announces the id and its paths on its first
line of output:

```
[agent-build] invocation: <id> progress: <path|none> log: <path>
```

Wait on `run_end` for that specific id. Keying on an event alone matches a previous invocation's
records, which is how a caller ends up testing a stale binary while believing its build finished:

```sh
# Wait for one specific agent_build.py invocation, then use the binary it produced.
invocation="$(uuidgen)"
progress=/tmp/foundry-build-wait.jsonl
python3 scripts/agent_build.py --invocation-id "$invocation" --progress-file "$progress" &
build_pid=$!
wait "$build_pid"; build_status=$?
python3 - "$progress" "$invocation" <<'PY'
import json, sys
path, invocation = sys.argv[1], sys.argv[2]
end = [r for r in map(json.loads, open(path))
       if r.get("event") == "run_end" and r.get("invocation_id") == invocation]
assert end, "no run_end for this invocation"
assert end[-1]["status"] == "success", end[-1]["status"]
PY
[ "$build_status" -eq 0 ] || exit "$build_status"
```

### The RESULT line

Every invocation writes exactly one terminal verdict line, always last, to both the human stream and
the build log:

```
[agent-build] RESULT: <status> step=<step> exit_code=<n> binary=<path> binary_present=<yes|no> child=<child> invocation=<uuid> log=<path>
```

`<status>` is one of `success`, `build-failure`, `generation-failure`, `binary-missing`,
`test-failure`, `tooling-missing`, `interrupted`; `<step>` is one of `startup`, `generate`, `build`,
`test`.

`<child>` is how the last build or test child ended, and it is the same value as `run_end`'s
`child_disposition`: `none` when the run never started one, `exited` when it finished on its own,
`terminated` or `killed` when an interrupt stopped it, and `escaped` when it survived even `SIGKILL`.
Only `escaped` leaves a process that may still be writing the build tree; the wrapper also logs a
warning for it. Everything else guarantees the artifacts described by the verdict are final.

Piping the wrapper into another command, launching it in the background, or appending any trailing
command in the same shell invocation discards its exit code, so such callers must read the log's final
`RESULT:` line (`tail -1 <log>`) rather than trusting an observed status of 0.

The exit codes are exhaustive:

| Exit | Meaning |
|------|---------|
| `0` | every step ran successfully and the expected editor binary exists |
| SCons/Ninja child status | the build or Ninja-generation step failed |
| test child status | the build succeeded and the test step failed |
| `1` | the build reported success but the expected editor binary is absent |
| `127` | required tooling (SCons, Ninja, ccache) or platform support is missing |
| `130` | interrupted |

A missing binary is a failure unconditionally, not only under `--test`: the wrapper always requests
`target=editor`, so an invocation that produced nothing to run must not be mistaken for a validated
build. Build settings rename the binary (`precision`, `extra_suffix`, sanitizers, alternate
toolchains), so when the expected name is absent the wrapper falls back to the sole editor binary in
the same directory and reports that path; only a directory with no editor binary, or an ambiguous one,
is `binary-missing`. An unchanged binary timestamp is never a failure, because a no-op incremental
build legitimately leaves the binary untouched.

A test command that cannot be launched at all reports `test-failure` with exit code `127`.

### Interrupting a run

The build and test children run in their own session, so an interrupt delivered to the wrapper does
not reach them. On `KeyboardInterrupt` the wrapper therefore signals the child's whole process group
with `SIGTERM`, waits for it, escalates to `SIGKILL` after a bounded grace period, and only then
writes its terminal verdict. Interrupting a build consequently ends it: nothing is left compiling into
`bin/` or the object tree behind a run that has already been declared over, so the identity the
verdict reports stays true and a restarted build cannot race a cancelled one. Further interrupts
arriving during the shutdown do not abandon the wait.

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
