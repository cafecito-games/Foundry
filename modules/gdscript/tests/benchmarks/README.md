# GDScript Benchmark Corpus

Workloads for `--gdscript-benchmark`. See
`docs/superpowers/specs/2026-06-25-gdscript-performance-harness-design.md`.

## Layout

Each *case* is a directory. A feature case is an A/B pair:

- `feature.gd`  — uses a fork feature (generics, traits, proxies, validated writes).
- `baseline.gd` — equivalent vanilla GDScript doing the same observable work.
- `case.cfg`    — per-case config (see schema below).

`_baseline/` is special: a single `empty_loop.gd` measuring harness overhead.

## Entry convention

Every workload `.gd` extends `RefCounted` and defines exactly:

    func run_benchmark(iterations: int) -> void:
        # do the measured work `iterations` times; never time internally.

The runner controls warmup vs measured counts and all timing.

## case.cfg schema

    [case]
    iterations=<int>                  ; measured iterations
    warmup=<int>                      ; discarded warmup iterations
    overhead_threshold_percent=<int>  ; report.py flags overhead above this; -1 = none
    note="<string>"                   ; optional

## Sampling-profiler workflow

(Added in Task 6.)
