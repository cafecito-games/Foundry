#!/usr/bin/env python3
"""Classify Godot 4.6.3-stable..4.7-stable PR-merges into port buckets by changed paths."""

import json
import re
import subprocess
import sys
from collections import Counter
from typing import Counter as CounterType

RANGE = "4.6.3-stable..4.7-stable"


def git(*args):
    return subprocess.run(
        ["git", *args], capture_output=True, text=True, cwd=sys.argv[1] if len(sys.argv) > 1 else "."
    ).stdout


# Gather merges: sha, pr, title(body first line)
raw = git("log", "--merges", "--format=%H%x1f%s%x1f%b%x1e", RANGE)
records = []
for chunk in raw.split("\x1e"):
    chunk = chunk.strip("\n")
    if not chunk:
        continue
    parts = chunk.split("\x1f")
    sha, subject = parts[0], parts[1]
    body = parts[2] if len(parts) > 2 else ""
    m = re.search(r"#(\d+)", subject)
    pr = m.group(1) if m else "?"
    title = (body.strip().splitlines() or [subject])[0].strip()
    records.append({"sha": sha, "pr": pr, "title": title})

print(f"Parsed {len(records)} merges", file=sys.stderr)


# Diverged upstream paths (fork rewrote these -> Foundry Script / script editor)
def is_diverged(p):
    return (
        p.startswith("modules/gdscript/")
        or p.startswith("editor/script/")
        or p.startswith("editor/plugins/script_")
        or p == "editor/plugins/script_text_editor.cpp"
    )


def is_asset(p):
    return "asset_library" in p


def bucket_for(paths):
    if not paths:
        return "empty", "-"
    nonempty = [p for p in paths if p]
    if all(p.startswith("thirdparty/") for p in nonempty):
        return "dep-bump", "thirdparty"
    if all(p.startswith("doc/classes/") for p in nonempty):
        return "docs", "doc"
    if any(is_asset(p) for p in nonempty):
        return "skip-assetstore", "asset_library"
    if any(is_diverged(p) for p in nonempty):
        return "manual-flag", "diverged"
    # candidate: pick dominant subsystem for fan-out
    tops = Counter()
    for p in nonempty:
        seg = p.split("/")
        key = "/".join(seg[:2]) if len(seg) > 1 else seg[0]
        tops[key] += 1
    return "candidate", tops.most_common(1)[0][0]


# Get changed files per merge (net PR delta). Batch for speed.
bucket_counts: CounterType[str] = Counter()
subsys_counts: CounterType[str] = Counter()
for r in records:
    out = git("diff", "--name-only", f"{r['sha']}^1", r["sha"])
    paths = out.splitlines()
    b, sub = bucket_for(paths)
    r["bucket"] = b
    r["subsystem"] = sub
    r["nfiles"] = len(paths)
    bucket_counts[b] += 1
    if b == "candidate":
        subsys_counts[sub] += 1

json.dump(
    records,
    open(
        "/private/tmp/claude-501/-Users-christian-CafecitoGames-Foundry/038bab56-fd92-4583-97ba-d26ee567dc53/scratchpad/catalog.json",
        "w",
    ),
    indent=1,
)

print("\n=== BUCKET COUNTS ===", file=sys.stderr)
for b, c in bucket_counts.most_common():
    print(f"{c:5d}  {b}", file=sys.stderr)
print("\n=== CANDIDATE SUBSYSTEMS (top 30) ===", file=sys.stderr)
for s, c in subsys_counts.most_common(30):
    print(f"{c:5d}  {s}", file=sys.stderr)
