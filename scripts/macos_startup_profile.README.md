# macOS editor startup profiling

A stdlib-only helper (`scripts/macos_startup_profile.py`) that captures, parses
and compares macOS editor startup profiles, so the "the editor blocks the run
loop while it boots" measurements behind
[#2090](https://github.com/cafecito-games/Foundry/issues/2090) are reproducible
with one command instead of being re-derived by hand each session.

It answers three different questions with three different instruments, keeps the
raw artifacts, and enforces the acceptance thresholds from #2090
(`< 8%` of main-thread samples inside `-[NSApplication _sendFinishLaunchingNotification]`,
no uninterrupted non-servicing block longer than `250 ms`).

## Quick start

```sh
# Everything: 3 sample runs + 1 Instruments trace + 3 benchmark runs, then the gate.
python3 scripts/macos_startup_profile.py all --project /path/to/project --label baseline

# Just the headline number (magnitude, optimized binary, 3 runs, warm caches).
python3 scripts/macos_startup_profile.py capture --project /path/to/project

# Just the timeline / longest non-servicing block (Instruments).
python3 scripts/macos_startup_profile.py timeline --project /path/to/project

# Before/after, once a fix is built.
python3 scripts/macos_startup_profile.py compare baseline/summary.json fix/summary.json
```

Artifacts land in `~/.foundry-startup-profiles/<label>-<timestamp>/` unless
`--out` is given. Every subcommand writes/updates a single `summary.json` in
that directory, so `capture`, `timeline` and `phases` can be pointed at the same
`--out` and accumulate into one comparable record.

## Subcommands

| Command | Instrument | Answers |
|---|---|---|
| `capture` | `/usr/bin/sample -wait` | What share of main-thread samples is inside the blocked AppKit frame, and roughly how many milliseconds is that? |
| `timeline` | `xctrace` "Time Profiler" | When exactly does the block start and end, per 100 ms bucket, and what is the longest gap between main run loop iterations? |
| `phases` | engine `--benchmark` marks | How much of the block is accounted for by instrumented boot phases? |
| `all` | all three | One command; ends by running the acceptance gate. |
| `analyze` | — | Re-parse an existing `sample` `.txt`, exported `.xml`, or `.trace` without re-capturing. |
| `compare` | — | Baseline vs candidate, with a refusal to compare across binary flavors, cache states or projects. |
| `check` | — | Evaluate the #2090 acceptance criteria against a `summary.json`; non-zero exit on failure. |

## What each measurement actually means

**`capture` (sample).** `/usr/bin/sample` is armed with `-wait` *before* the
editor is launched, because attaching afterwards misses the whole startup block
and only sees the idle main loop. The parser builds the per-thread call tree,
then reports:

- `blocked_percent` — inclusive share of main-thread samples under
  `-[NSApplication _sendFinishLaunchingNotification]`, counting only the
  outermost occurrence on each stack so recursion is not double counted.
- `blocked_ms_estimate` — that share times the *measured* sample rate
  (`main-thread samples / window`), not the requested 1 ms interval. `sample`
  never reaches 1 kHz on this workload.
- `idle_percent` — samples parked in `nanosleep` / `__semwait_signal` /
  `mach_msg2_trap`, so a reader can tell a busy main thread from a parked one.
  The editor throttles when idle, so most of a 20 s window is idle by design.
- flat self time and the heaviest chain, for triage.

**`timeline` (Instruments).** Records a Time Profiler trace and exports two
tables:

- `runloop-events` → every main run loop iteration with a timestamp. The gap
  between consecutive iterations *is* the "time spent not servicing the run
  loop", which is exactly what #2090 bounds at 250 ms. This is the primary gate
  metric and it does not depend on symbolication at all.
- `time-profile` → per-sample timestamps and backtraces, bucketed into 100 ms
  (`--bucket-ms`) so the block can be shown as a timeline rather than asserted
  to be contiguous.

Use `--skip-time-profile` when only the run loop gaps are needed; the
`time-profile` export is the slow part.

Caveat carried in the JSON key name: Instruments only samples *running* threads,
so `blocked_percent_of_running_main_samples` is a share of running samples and
is **not** comparable with `sample`'s `blocked_percent`. The gate uses the
`sample` number for the percentage criterion and the run loop gaps for the
250 ms criterion.

**`phases` (benchmark marks).** Runs the editor with the engine's own
instrumentation. Note these are **global** flags and must precede the
subcommand, which is what the script does:

```sh
foundry --benchmark --benchmark-file /tmp/b.json --quit-after 300 editor open --project X
```

The file is only written at shutdown, so the run must exit cleanly — hence
`--quit-after`. Coverage is incomplete: `main_loop->initialize()`
(`SceneTree::initialize`) and the first-frame `CallQueue::flush()` carry no
marks, so the instrumented total is materially smaller than the measured block.
The tool prints that gap rather than hiding it.

## Binary selection

```sh
--binary release   # bin/foundry.macos.editor.arm64      (default)
--binary dev       # bin/foundry.macos.editor.dev.arm64
--binary /path/to/binary
```

- **release** is optimized and **stripped**: trust its timings, never its symbol
  names. Unresolved frames are normalized to `???+0x<offset>` so flat self time
  still aggregates per address.
- **dev** is fully symbolized but roughly an order of magnitude slower: treat its
  numbers as **proportions**, never wall-clock. Give it a longer window
  (`--duration 45` or more).
- For symbols *and* real magnitudes, build one:

  ```sh
  python3 scripts/agent_build.py --compiler-cache ccache \
    --scons-arg production=yes --scons-arg debug_symbols=yes
  ```

`analyze --phases` breaks the blocked window down by boot phase
(`Main::setup`, `Main::start`, `SceneTree::initialize`, ...). It only produces
meaningful output on a symbolized profile.

## Measurement hygiene the tool enforces

- **Three runs by default.** Startup timings vary by tens of milliseconds; a
  single pair of runs cannot support a percentage claim. The summary always
  carries median, min, max and the run count.
- **Cache state is recorded, never guessed.** `capture`/`timeline`/`phases`
  perform one throwaway launch first (`--no-warm` to skip) and record
  `--cache-state warm|cold|unknown`. The editor doc cache
  (`~/Library/Caches/Foundry/editor_doc_cache-<ver>.res`) is keyed on the
  ClassDB API hash and therefore misses on the first launch after every engine
  rebuild; the shader cache lives in
  `~/Library/Application Support/Foundry/shader_cache`.
- **`compare` refuses silently-wrong deltas.** Differing binary flavor, cache
  state or project prints a `WARNING` and exits `2`; a delta across those is not
  a delta.
- **The binary is fingerprinted.** Path, flavor, `--version`, size and build
  time go into the summary alongside the checkout revision, so a summary can
  never be mistaken for one produced by a different build.

## Acceptance gate

`check` (and the tail of `all` and `compare`) evaluates the #2090 criteria:

```
[PASS/FAIL] median main-thread samples under -[NSApplication _sendFinishLaunchingNotification] < 8%
[PASS/FAIL] longest main run loop non-servicing gap <= 250 ms
[PASS/FAIL] longest contiguous run inside the blocked frame <= 250 ms
```

Exit codes: `0` pass, `1` fail, `2` unusable input (no measurements, or a
comparison across incompatible conditions). Thresholds are overridable with
`--max-blocked-percent` / `--max-block-ms` for exploration, but the defaults are
the ones the issue commits to.

The gate deliberately does **not** cover the remaining #2090 criteria — no wait
cursor across ten launches, input suppression during init, and the progress
indication within 500 ms. Those need a screen capture and a regression test, not
a profiler.

## Requirements

macOS with the Xcode command line tools (`xcrun xctrace`) for `timeline`;
`capture` and `phases` need nothing beyond `/usr/bin/sample` and a built editor
binary in `bin/`. No third-party Python packages.
