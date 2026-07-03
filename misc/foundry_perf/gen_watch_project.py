#!/usr/bin/env python3
"""Generate a synthetic Foundry project with many files/directories.

Used to exercise the editor filesystem directory watcher (EditorFileSystem::_fs_watch_*):
a large tree makes the O(number of files) focus-in scan measurable, so the watcher's
skip-when-idle behavior is easy to observe.

Usage: gen_watch_project.py <out_dir> [num_dirs=300] [files_per_dir=25]
Example: python3 gen_watch_project.py /tmp/proj_large 300 25   # ~7,500 .fs files
"""

import os
import sys


def main():
    out = sys.argv[1]
    num_dirs = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    per = int(sys.argv[3]) if len(sys.argv) > 3 else 25

    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "project.foundry"), "w") as f:
        f.write('config_version=5\n\n[application]\n\nconfig/name="watch_bench"\n')

    for d in range(num_dirs):
        dd = os.path.join(out, "scripts", f"d{d:04d}")
        os.makedirs(dd, exist_ok=True)
        for i in range(per):
            with open(os.path.join(dd, f"s{i:03d}.fs"), "w") as f:
                f.write("extends Node\nvar x: int = 0\n")

    print(f"Generated {num_dirs * per} files across {num_dirs} directories under {out}")


if __name__ == "__main__":
    main()
