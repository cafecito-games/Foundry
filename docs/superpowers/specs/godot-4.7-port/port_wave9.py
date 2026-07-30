#!/usr/bin/env python3
"""Wave-9 scoped cherry-pick of the port-now PRs from triage/wave9/out-*.json.

Same mechanics as port.py (chronological `-x -m 1` picks, abort+record on
conflict) but reads only the wave-9 triage outputs and writes
ported-log-wave9.json, so it does not disturb the earlier waves' logs.

Usage:
    python3 port_wave9.py [--risk low,med] [--dry-run] [--limit N]

Run from the worktree root.
"""

import argparse
import glob
import json
import subprocess
import sys

SPEC = "docs/superpowers/specs/godot-4.7-port"
RANGE = "4.6.3-stable..4.7-stable"


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True)


def chronological_rank():
    out = git("rev-list", "--merges", "--reverse", RANGE).stdout.split()
    return {sha: i for i, sha in enumerate(out)}


def load_port_now(risk_filter):
    seen = {}
    for path in sorted(glob.glob(f"{SPEC}/triage/wave9/out-*.json")):
        for rec in json.load(open(path)):
            if rec.get("decision") != "port-now":
                continue
            if risk_filter and rec.get("risk") not in risk_filter:
                continue
            seen[rec["sha"]] = rec
    return list(seen.values())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--risk", default="")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    risk_filter = set(x.strip() for x in args.risk.split(",") if x.strip())
    rank = chronological_rank()
    picks = load_port_now(risk_filter)
    picks.sort(key=lambda r: rank.get(r["sha"], 1 << 30))
    if args.limit:
        picks = picks[: args.limit]

    print(
        f"{len(picks)} port-now PRs selected"
        + (f" (risk in {sorted(risk_filter)})" if risk_filter else "")
        + (" [DRY RUN]" if args.dry_run else "")
    )

    if not args.dry_run and git("status", "--porcelain").stdout.strip():
        sys.exit("Working tree not clean; aborting.")

    log = []
    for i, rec in enumerate(picks, 1):
        sha, pr = rec["sha"], rec["pr"]
        label = f"[{i}/{len(picks)}] PR#{pr} {sha[:10]} risk={rec.get('risk')}"
        if args.dry_run:
            print(f"would pick {label}")
            log.append({**rec, "outcome": "dry-run"})
            continue
        r = git("cherry-pick", "-x", "-m", "1", sha)
        if r.returncode == 0:
            new_sha = git("rev-parse", "HEAD").stdout.strip()
            print(f"OK    {label}")
            log.append({**rec, "outcome": "ported", "local_sha": new_sha})
        else:
            git("cherry-pick", "--abort")
            reason = (r.stderr or r.stdout).strip().splitlines()
            reason = reason[-1] if reason else "conflict"
            print(f"CONFL {label} -> {reason}")
            log.append({**rec, "outcome": "conflicted", "reason": reason})

    json.dump(log, open(f"{SPEC}/ported-log-wave9.json", "w"), indent=1)
    ported = sum(1 for x in log if x["outcome"] == "ported")
    confl = sum(1 for x in log if x["outcome"] == "conflicted")
    print(f"\nDone: {ported} ported, {confl} conflicted. Log -> {SPEC}/ported-log-wave9.json")


if __name__ == "__main__":
    main()
