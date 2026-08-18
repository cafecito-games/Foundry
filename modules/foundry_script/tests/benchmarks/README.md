# Foundry Script Benchmark Corpus

Workloads and tooling for the Foundry Script performance harness. The supported entry
point is `foundry test benchmark`, which discovers the corpus, runs every workload
variant, and emits a JSON map of measured microseconds. The command needs a binary built
with `tests=yes`; it is available in editor and export-template builds alike.

## Layout

Each *case* is a directory. A feature case is an A/B pair:

- `feature.fs`  — uses a fork feature (generics, traits, proxies, validated writes).
- `baseline.fs` — equivalent plain Foundry Script doing the same observable work.
- `case.cfg`    — per-case config (see schema below).

`_baseline/` is special: a single `empty_loop.fs` measuring harness overhead.

## Entry convention

Every workload `.fs` extends `RefCounted` and defines exactly:

    func run_benchmark(iterations: int) -> void:
        # do the measured work `iterations` times; never time internally.

The runner discovers cases, controls warmup vs measured iteration counts and all
timing, and emits a JSON map of `foundry_script:<case>/<variant>` to microseconds.

## case.cfg schema

    [case]
    iterations=<int>                  ; measured iterations
    warmup=<int>                      ; discarded warmup iterations
    overhead_threshold_percent=<int>  ; report.py flags overhead above this; -1 = none
    note="<string>"                   ; optional

## Running

Binaries are named `bin/foundry.<platform>.<target>.<arch>`, so a macOS development
editor build is `bin/foundry.macos.editor.dev.arm64` and a Linux one is
`bin/foundry.linuxbsd.editor.dev.x86_64`. Substitute your own throughout.

Run the whole corpus (the directory argument defaults to this directory):

    ./bin/foundry.macos.editor.dev.arm64 --headless test benchmark

Write the machine-readable artifact instead of dumping to stdout:

    ./bin/foundry.macos.editor.dev.arm64 --headless test benchmark --output bench.json

Add the per-function profiling pass and its sidecar (`--profile-output` implies
`--profile`):

    ./bin/foundry.macos.editor.dev.arm64 --headless test benchmark --output bench.json --profile --profile-output profile.json

Restrict the run to one case directory:

    ./bin/foundry.macos.editor.dev.arm64 --headless test benchmark modules/foundry_script/tests/benchmarks/proxy_dispatch

The command exits `0` only when every discovered variant compiled and ran and every
requested artifact was written; discovery failures, an empty corpus, a failing variant,
and an unwritable output file all exit `1`.

## Concurrent runs

A benchmark run redirects `user://` to a per-process root under the shared test scratch
space and recreates it clean at startup, so two runs started at the same time — or a run
started next to the test suite — cannot erase each other's `user://` tree. The root is
removed when the run finishes, unless a requested artifact was written inside it (an
`--output user://...` path), in which case the artifact stays where it was asked to go.

## Output format

Results are a flat JSON object keyed by `foundry_script:<case>/<variant>`, valued in
microseconds:

    {
        "foundry_script:_baseline/empty_loop": 146293.0
    }

`--output` is the parseable channel: that file contains nothing but the JSON map.
Stdout is human-readable only — the engine has already printed its startup header there
by the time results exist, so a bare stdout dump can never be pure JSON. The profile
sidecar written by `--profile-output` is keyed by `<case>/<variant>` without the
language prefix, and each value is an array of per-function rows.

## Measuring a release build

Performance work should be confirmed against an export-template build, not just the
development editor build. Export-template targets default `foundry_script_frontend` to
`no`, and `tests=yes` is a hard error in that configuration, so the front-end has to be
requested explicitly:

    scons platform=macos target=template_release tests=yes foundry_script_frontend=yes
    ./bin/foundry.macos.template_release.arm64 --headless test benchmark --output bench-release.json

Note that `foundry test benchmark` times Foundry Script workloads. It is unrelated to
the engine's `--benchmark` startup flag, which records engine startup phase marks.

## Reporting and comparing

`report.py` turns a result file into a per-case overhead table (feature vs baseline, net
of the `_baseline/empty_loop` harness floor):

    python3 modules/foundry_script/tests/benchmarks/report.py bench.json

With `--compare` it diffs two runs and exits non-zero when any key regresses beyond the
tolerance percentage:

    python3 modules/foundry_script/tests/benchmarks/report.py --compare before.json after.json --tolerance 10

## Sampling-profiler workflow (native-frame attribution)

The benchmark numbers tell you *which case* is slow; a sampling profiler tells you
*which native (C++) frames* make it slow — where the fork's overhead actually
lives (proxy resolution, `Variant` conversions, type-check helpers).

Run one case in isolation, with a high iteration count so the sampler gets enough
samples. Point the corpus positional at a single case directory:

### macOS (primary)

Time Profiler via `xctrace`:

    xctrace record --template 'Time Profiler' --output proxy.trace --launch -- \
      ./bin/foundry.macos.editor.dev.arm64 --headless \
      test benchmark modules/foundry_script/tests/benchmarks/proxy_dispatch

Open `proxy.trace` in Instruments and read the heaviest stack. Compare the
`feature` vs `baseline` runs (run each case dir alone, or temporarily move the
other variant aside) — the frames that appear only in `feature` are the cost.

Quick text alternative (no Instruments UI), sample a running invocation:

    ./bin/foundry.macos.editor.dev.arm64 --headless \
      test benchmark modules/foundry_script/tests/benchmarks/proxy_dispatch &
    sample $! 5 -file proxy.sample.txt   # 5-second sample of native stacks
    cat proxy.sample.txt

Bump the case's `iterations` in `case.cfg` if the run finishes before the sample
window.

### Linux (secondary)

    perf record -g -- ./bin/foundry.linuxbsd.editor.dev.x86_64 --headless \
      test benchmark modules/foundry_script/tests/benchmarks/proxy_dispatch
    perf report            # or pipe through a flamegraph generator

### Reading results

A real optimization target shows as a native frame whose share is large in
`feature` and absent/small in `baseline`. Confirm it against the `report.py`
overhead percentage and the `--profile` per-function data: the Foundry Script
function profiler says which `.fs` function, the sampler says which C++ work
inside it.

## Deferred: opcode-level profiling

Function-level (the `--profile` pass) plus native sampling covers attribution for now.
Per-opcode timing is intentionally **not** built yet: adding `get_ticks_usec()` around
each VM dispatch in `modules/foundry_script/fs_vm.cpp` distorts the very loop it
measures and over-reports cheap opcodes.

When function + sampling data point at a specific opcode worth characterizing,
add it behind a build flag (e.g. `foundry_script_opcode_profile=yes`) so the dispatch
loop is untouched in normal builds:

- Gate a `uint64_t opcode_self_time[OPCODE_COUNT]` / `opcode_count[OPCODE_COUNT]`
  accumulator behind `#ifdef FOUNDRY_SCRIPT_OPCODE_PROFILE` in `FSFunction::call`.
- Prefer counting (cheap) over timing where possible; reserve timing for a short
  list of suspect opcodes to limit distortion.
- Surface the totals through a new `--opcodes` option on `test benchmark`, parallel
  to the function-profiler sidecar.

Only build this once the cheaper layers have isolated the target.
