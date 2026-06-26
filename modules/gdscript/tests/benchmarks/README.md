# GDScript Benchmark Corpus

Workloads and tooling for the GDScript performance harness. See
`docs/superpowers/specs/2026-06-25-gdscript-performance-harness-design.md` for the
design and `docs/superpowers/plans/2026-06-25-gdscript-performance-harness.md` for
the plan.

Each *case* is a directory holding `.gd` workloads plus a `case.cfg`. Feature
cases are A/B pairs — `feature.gd` exercises a fork feature (generics, traits,
proxies, validated writes) and `baseline.gd` does the same observable work in
plain GDScript — so the timing delta isolates the feature's overhead. The runner
discovers cases, controls warmup vs measured iteration counts, and emits a JSON
map of `gdscript:<case>/<variant>` to microseconds via `--gdscript-benchmark`.

## Sampling-profiler workflow (native-frame attribution)

The benchmark numbers tell you *which case* is slow; a sampling profiler tells you
*which native (C++) frames* make it slow — where the fork's overhead actually
lives (proxy resolution, `Variant` conversions, type-check helpers).

Run one case in isolation, with a high iteration count so the sampler gets enough
samples. Point `--gdscript-benchmark` at a single case directory:

### macOS (primary)

Time Profiler via `xctrace`:

    xctrace record --template 'Time Profiler' --output proxy.trace --launch -- \
      ./bin/godot.macos.editor.dev.* --headless \
      --gdscript-benchmark modules/gdscript/tests/benchmarks/proxy_dispatch

Open `proxy.trace` in Instruments and read the heaviest stack. Compare the
`feature` vs `baseline` runs (run each case dir alone, or temporarily move the
other variant aside) — the frames that appear only in `feature` are the cost.

Quick text alternative (no Instruments UI), sample a running invocation:

    ./bin/godot.macos.editor.dev.* --headless \
      --gdscript-benchmark modules/gdscript/tests/benchmarks/proxy_dispatch &
    sample $! 5 -file proxy.sample.txt   # 5-second sample of native stacks
    cat proxy.sample.txt

Bump the case's `iterations` in `case.cfg` if the run finishes before the sample
window.

### Linux (secondary)

    perf record -g -- ./bin/godot.linuxbsd.editor.dev.x86_64 --headless \
      --gdscript-benchmark modules/gdscript/tests/benchmarks/proxy_dispatch
    perf report            # or pipe through a flamegraph generator

### Reading results

A real optimization target shows as a native frame whose share is large in
`feature` and absent/small in `baseline`. Confirm it against the `report.py`
overhead percentage and the `--gdscript-benchmark-profile` per-function data:
the GDScript function profiler says which `.gd` function, the sampler says which
C++ work inside it.

## Deferred: opcode-level profiling

Function-level (the `--gdscript-benchmark-profile` pass) plus native sampling
covers attribution for now. Per-opcode timing is intentionally **not** built yet:
adding `get_ticks_usec()` around each VM dispatch in `gdscript_vm.cpp` distorts
the very loop it measures and over-reports cheap opcodes.

When function + sampling data point at a specific opcode worth characterizing,
add it behind a build flag (e.g. `gdscript_opcode_profile=yes`) so the dispatch
loop is untouched in normal builds:

- Gate a `uint64_t opcode_self_time[OPCODE_COUNT]` / `opcode_count[OPCODE_COUNT]`
  accumulator behind `#ifdef GDSCRIPT_OPCODE_PROFILE` in `GDScriptFunction::call`.
- Prefer counting (cheap) over timing where possible; reserve timing for a short
  list of suspect opcodes to limit distortion.
- Surface the totals through a new `--gdscript-benchmark-opcodes` output, parallel
  to the function-profiler sidecar.

Only build this once the cheaper layers have isolated the target.
