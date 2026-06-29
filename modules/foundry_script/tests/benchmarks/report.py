#!/usr/bin/env python3
"""Render and compare FoundryScript benchmark JSON (see README.md).

The runner (`--foundry_script-benchmark <dir>`) emits a flat JSON object mapping
``"foundry_script:<case>/<variant>"`` to the measured microseconds, for example::

    {
        "foundry_script:_baseline/empty_loop": 412.0,
        "foundry_script:trait_call/baseline": 980.0,
        "foundry_script:trait_call/feature": 1320.0
    }

This tool turns that map into a per-case overhead table (feature vs baseline,
net of the ``_baseline/empty_loop`` harness floor) and, with ``--compare``,
diffs two runs and exits non-zero when any key regresses beyond a tolerance.

Stdlib-only: no third-party dependencies.
"""

import argparse
import configparser
import json
import sys
from pathlib import Path

# Key of the harness-overhead floor workload; its time is subtracted from both
# variants of every A/B case so the table reports work attributable to the case
# itself rather than the fixed cost of the measurement loop.
FLOOR_KEY = "foundry_script:_baseline/empty_loop"

# Fallback overhead percentage at or above which a row is flagged when a case
# has no `overhead_threshold_percent` in its `case.cfg`. This is purely a
# display cue; regression gating lives in `compare`.
DEFAULT_OVERHEAD_THRESHOLD_PERCENT = 50.0


def _split_key(key):
    """Split ``"foundry_script:trait_call/feature"`` into ``("trait_call", "feature")``.

    The ``foundry_script:`` prefix is optional so the helper also tolerates already
    stripped keys.
    """
    body = key.split(":", 1)[1] if ":" in key else key
    case, _, variant = body.partition("/")
    return case, variant


def load_thresholds(corpus_dir):
    """Read each case's ``overhead_threshold_percent`` from its ``case.cfg``.

    Returns a ``case -> int`` map. A value of ``-1`` means the case opts out of
    overhead flagging. Cases without a readable threshold are simply absent and
    fall back to :data:`DEFAULT_OVERHEAD_THRESHOLD_PERCENT`.
    """
    thresholds = {}
    base = Path(corpus_dir)
    for cfg_path in sorted(base.glob("*/case.cfg")):
        parser = configparser.ConfigParser()
        try:
            parser.read(cfg_path, encoding="utf-8")
            thresholds[cfg_path.parent.name] = parser.getint("case", "overhead_threshold_percent")
        except (configparser.Error, ValueError):
            continue
    return thresholds


def _is_flagged(overhead_percent, threshold):
    """Whether a row's overhead warrants a display flag.

    ``threshold`` is the case's configured percent, ``None`` to fall back to the
    default, or negative to disable flagging entirely.
    """
    if threshold is None:
        threshold = DEFAULT_OVERHEAD_THRESHOLD_PERCENT
    if threshold < 0:
        return False
    return overhead_percent >= threshold


def build_rows(data, thresholds=None):
    """Build per-case overhead rows from a runner JSON map.

    Each A/B case contributes one row with its baseline/feature times and the
    overhead of feature over baseline, both computed net of the harness floor.
    Cases missing either variant are skipped; the ``_baseline`` case is never a
    row of its own. ``thresholds`` (case -> percent, from :func:`load_thresholds`)
    drives the per-row ``flagged`` flag; absent cases use the default.
    """
    thresholds = thresholds or {}
    floor = data.get(FLOOR_KEY, 0.0)
    cases = {}
    for key, usec in data.items():
        case, variant = _split_key(key)
        if case == "_baseline":
            continue
        cases.setdefault(case, {})[variant] = usec

    rows = []
    for case, variants in sorted(cases.items()):
        baseline = variants.get("baseline")
        feature = variants.get("feature")
        if baseline is None or feature is None:
            continue
        # Clamp to keep the net baseline strictly positive (avoids divide-by-zero
        # when a case is as cheap as the floor) and to avoid negative net times
        # when sub-floor noise makes a variant dip below the floor.
        net_baseline = max(baseline - floor, 1e-9)
        net_feature = max(feature - floor, 0.0)
        overhead_abs = net_feature - net_baseline
        overhead_percent = round(100.0 * overhead_abs / net_baseline, 1)
        threshold = thresholds.get(case)
        rows.append(
            {
                "case": case,
                "baseline_us": round(baseline, 1),
                "feature_us": round(feature, 1),
                "overhead_abs": round(overhead_abs, 1),
                "overhead_percent": overhead_percent,
                "threshold_percent": threshold,
                "flagged": _is_flagged(overhead_percent, threshold),
            }
        )
    return rows


def compare(old, new, tolerance_percent=10.0):
    """Diff two runner JSON maps key by key.

    Returns a list of per-key deltas. A key is flagged ``regressed`` when its
    time grew by more than ``tolerance_percent`` relative to the old run. Keys
    that the old run did not measure with a positive time are skipped (no
    meaningful percentage baseline). Keys present in the old run but missing
    from the new run are reported as ``missing`` and flagged ``regressed``: a
    benchmark that vanished (a removed case, or partial output after a workload
    failure) is lost coverage, and the gate must not pass silently.
    """
    out = []
    for key, new_usec in new.items():
        old_usec = old.get(key)
        if old_usec is None or old_usec <= 0:
            continue
        # Decide regression on the exact delta; only the reported value is
        # rounded. Rounding first could pull a real over-tolerance increase
        # (e.g. 10.04%) down to the tolerance and hide the regression.
        exact_delta_percent = 100.0 * (new_usec - old_usec) / old_usec
        out.append(
            {
                "key": key,
                "old_us": round(old_usec, 1),
                "new_us": round(new_usec, 1),
                "delta_percent": round(exact_delta_percent, 1),
                "regressed": exact_delta_percent > tolerance_percent,
                "missing": False,
            }
        )
    for key, old_usec in old.items():
        if key in new or old_usec is None or old_usec <= 0:
            continue
        out.append(
            {
                "key": key,
                "old_us": round(old_usec, 1),
                "new_us": None,
                "delta_percent": None,
                "regressed": True,
                "missing": True,
            }
        )
    return out


def _load_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _print_table(rows):
    if not rows:
        print("No A/B cases found.")
        return
    header = (
        f"{'case':<20}{'baseline_us':>14}{'feature_us':>14}"
        f"{'overhead_abs':>14}{'overhead_%':>12}{'threshold_%':>13}  flag"
    )
    print(header)
    print("-" * len(header))
    for row in rows:
        threshold = row["threshold_percent"]
        threshold_text = (
            "none" if threshold is not None and threshold < 0 else "default" if threshold is None else threshold
        )
        flag = "<-- over threshold" if row["flagged"] else ""
        print(
            f"{row['case']:<20}{row['baseline_us']:>14}{row['feature_us']:>14}"
            f"{row['overhead_abs']:>14}{row['overhead_percent']:>12}{str(threshold_text):>13}  {flag}"
        )


def _print_comparison(results):
    # Missing keys have no delta; sort them first (treated as the worst kind of
    # regression), then by descending delta for the rest.
    def sort_key(item):
        if item["missing"]:
            return (0, 0.0)
        return (1, -item["delta_percent"])

    for row in sorted(results, key=sort_key):
        if row["missing"]:
            print(f"{row['key']:<44}{row['old_us']:>12}{'(missing)':>12}{'--':>10} REGRESSION (missing)")
            continue
        mark = " REGRESSION" if row["regressed"] else ""
        print(f"{row['key']:<44}{row['old_us']:>12}{row['new_us']:>12}{row['delta_percent']:>9}%{mark}")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Render or compare FoundryScript benchmark JSON.")
    parser.add_argument("bench_json", nargs="?", help="bench.json to render")
    parser.add_argument(
        "--compare",
        nargs=2,
        metavar=("OLD", "NEW"),
        help="compare two bench.json files for regressions",
    )
    parser.add_argument(
        "--tolerance",
        type=float,
        default=10.0,
        help="regression tolerance percent for --compare (default: 10.0)",
    )
    parser.add_argument(
        "--corpus-dir",
        default=str(Path(__file__).resolve().parent),
        help="benchmark corpus directory whose case.cfg files supply per-case "
        "overhead thresholds (default: report.py's own directory)",
    )
    args = parser.parse_args(argv)

    if args.compare:
        old = _load_json(args.compare[0])
        new = _load_json(args.compare[1])
        results = compare(old, new, args.tolerance)
        _print_comparison(results)
        regressed = any(row["regressed"] for row in results)
        return 1 if regressed else 0

    if not args.bench_json:
        parser.error("provide bench.json or --compare OLD NEW")

    data = _load_json(args.bench_json)
    thresholds = load_thresholds(args.corpus_dir)
    _print_table(build_rows(data, thresholds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
