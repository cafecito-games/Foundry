#!/usr/bin/env python3
"""Generate a synthetic Foundry project to benchmark scene/resource/script loading.

Usage: gen_project.py <out_dir> <num_scripts> <num_leaf_scenes> <nodes_per_scene> <nest_children>

Produces:
  project.foundry
  scripts/script_XXXX.fs        (num_scripts standalone classes)
  scenes/flat_XXXX.tscn         (num_leaf_scenes scenes, nodes_per_scene plain nodes,
                                  every 2nd node gets a script attached)
  scenes/leaf.tscn              (a small leaf scene with a script)
  scenes/nested.tscn            (instances leaf.tscn nest_children times)
  bench_manifest.txt            (list of scene paths to benchmark, one per line)
"""
import os
import sys

def main():
    out = sys.argv[1]
    num_scripts = int(sys.argv[2])
    num_leaf_scenes = int(sys.argv[3])
    nodes_per_scene = int(sys.argv[4])
    nest_children = int(sys.argv[5])

    os.makedirs(os.path.join(out, "scripts"), exist_ok=True)
    os.makedirs(os.path.join(out, "scenes"), exist_ok=True)

    with open(os.path.join(out, "project.foundry"), "w") as f:
        f.write('config_version=5\n\n[application]\n\nconfig/name="perf_bench"\n')

    # Standalone scripts. Each has a handful of members + methods so parse/analyze
    # has real work. Every 4th script extends the previous one to create a dependency chain.
    for i in range(num_scripts):
        p = os.path.join(out, "scripts", f"script_{i:05d}.fs")
        with open(p, "w") as f:
            if i > 0 and i % 4 == 0:
                f.write(f'extends "res://scripts/script_{i-1:05d}.fs"\n\n')
            else:
                f.write("extends Node\n\n")
            f.write(f"var value_{i}: int = {i}\n")
            f.write(f"var name_{i}: String = \"n{i}\"\n")
            f.write(f"var arr_{i}: Array = [1, 2, 3]\n\n")
            f.write(f"func compute_{i}(x: int) -> int:\n")
            f.write(f"\tvar total: int = x\n")
            f.write(f"\tfor j in range(10):\n")
            f.write(f"\t\ttotal += j * value_{i}\n")
            f.write(f"\treturn total\n\n")
            f.write(f"func describe_{i}() -> String:\n")
            f.write(f"\treturn name_{i} + str(value_{i})\n")

    # A shared leaf script for scene attachment.
    with open(os.path.join(out, "scripts", "leaf_script.fs"), "w") as f:
        f.write("extends Node\n\nvar hp: int = 100\nvar label: String = \"leaf\"\n\n")
        f.write("func ping() -> int:\n\treturn hp\n")

    manifest = []

    # Flat scenes: nodes_per_scene plain Nodes, half of them carry a script.
    for s in range(num_leaf_scenes):
        # Reference a rotating subset of scripts so scenes pull in script compiles.
        used = []
        p = os.path.join(out, "scenes", f"flat_{s:05d}.tscn")
        lines = []
        ext = []
        load_steps = 1
        for n in range(nodes_per_scene):
            if n % 2 == 0 and num_scripts > 0:
                sid = (s * nodes_per_scene + n) % num_scripts
                if sid not in used:
                    used.append(sid)
        # Build ext_resource header
        idmap = {}
        for k, sid in enumerate(used):
            rid = f"{k+1}_s{sid}"
            idmap[sid] = rid
            ext.append(f'[ext_resource type="Script" path="res://scripts/script_{sid:05d}.fs" id="{rid}"]')
        load_steps = 1 + len(ext)
        lines.append(f"[gd_scene load_steps={load_steps} format=3]")
        lines.append("")
        lines.extend(ext)
        if ext:
            lines.append("")
        lines.append('[node name="Root" type="Node"]')
        for n in range(nodes_per_scene):
            lines.append(f'[node name="N{n}" type="Node" parent="."]')
            if n % 2 == 0 and num_scripts > 0:
                sid = (s * nodes_per_scene + n) % num_scripts
                if sid in idmap:
                    lines.append(f'script = ExtResource("{idmap[sid]}")')
        with open(p, "w") as f:
            f.write("\n".join(lines) + "\n")
        manifest.append(f"res://scenes/flat_{s:05d}.tscn")

    # Leaf scene with a script, used for nesting.
    with open(os.path.join(out, "scenes", "leaf.tscn"), "w") as f:
        f.write('[gd_scene load_steps=2 format=3]\n\n')
        f.write('[ext_resource type="Script" path="res://scripts/leaf_script.fs" id="1_leaf"]\n\n')
        f.write('[node name="Leaf" type="Node"]\nscript = ExtResource("1_leaf")\n')
        f.write('[node name="A" type="Node" parent="."]\n')
        f.write('[node name="B" type="Node" parent="."]\n')
    manifest.append("res://scenes/leaf.tscn")

    # Nested scene: instances leaf.tscn nest_children times.
    with open(os.path.join(out, "scenes", "nested.tscn"), "w") as f:
        f.write(f'[gd_scene load_steps=2 format=3]\n\n')
        f.write('[ext_resource type="PackedScene" path="res://scenes/leaf.tscn" id="1_leaf"]\n\n')
        f.write('[node name="Root" type="Node"]\n')
        for c in range(nest_children):
            f.write(f'[node name="Child{c}" parent="." instance=ExtResource("1_leaf")]\n')
    manifest.append("res://scenes/nested.tscn")

    with open(os.path.join(out, "bench_manifest.txt"), "w") as f:
        f.write("\n".join(manifest) + "\n")

    print(f"Generated project at {out}: scripts={num_scripts} flat_scenes={num_leaf_scenes} "
          f"nodes/scene={nodes_per_scene} nest_children={nest_children}")

if __name__ == "__main__":
    main()
