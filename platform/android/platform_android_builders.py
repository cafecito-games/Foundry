"""Functions used to generate source files during build time"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path
from typing import cast

import android_native_contract as contract


def _node_path(node) -> Path:
    return Path(getattr(node, "abspath", str(node)))


def get_foundry_source_identity(repository) -> tuple[str, str, bool]:
    """Resolve the exact Foundry source identity used by an Android SCons build."""

    return cast(tuple[str, str, bool], contract.foundry_identity(Path(repository)))


def write_android_native_provenance(target, source, env):
    """Write one canonical provenance record after both native libraries exist."""

    targets = [_node_path(node) for node in target]
    sources = [_node_path(node) for node in source]
    if len(targets) != 1 or targets[0].name != "provenance.json":
        raise contract.ContractError("Android native provenance action requires one provenance.json target")
    directory = targets[0].parent
    if {path.name for path in sources} != set(contract.LIBRARY_NAMES):
        raise contract.ContractError("Android native provenance action requires both canonical native libraries")
    if any(path.parent != directory for path in sources):
        raise contract.ContractError("Android native provenance libraries and target must share one cell directory")

    build_type = env["android_native_build_type"]
    abi = env["android_native_abi"]
    specification = contract.build_spec(build_type, abi)
    actual_build = {
        "abi": abi,
        "arch": env["arch"],
        "build_type": build_type,
        "debug_symbols": env["debug_symbols"],
        "dev_build": env["dev_build"],
        "dev_mode": env["dev_mode"],
        "production": env["production"],
        "swappy": env["swappy"],
        "target": env["target"],
        "tests": env["tests"],
    }
    if actual_build != specification.build_json():
        raise contract.ContractError(
            f"Android native cell build configuration mismatch: expected {specification.build_json()!r}, "
            f"got {actual_build!r}"
        )

    contract.write_native_provenance(
        targets[0],
        revision=env["foundry_android_source_revision"],
        tree=env["foundry_android_source_tree"],
        dirty=env["foundry_android_source_dirty"],
        build_type=build_type,
        abi=abi,
        library_directory=directory,
    )
    return 0


def _native_gradle_properties(env) -> list[str]:
    native_root = str(env.get("foundry_native_root", "") or "")
    if native_root:
        return [f"-PfoundryNativeRoot={native_root}"]
    return []


def generate_android_binaries(target, source, env):
    gradle_process = []

    if sys.platform.startswith("win"):
        gradle_process = [
            "cmd",
            "/c",
            "gradlew.bat",
        ]
    else:
        gradle_process = ["./gradlew"]

    if env["module_mono_enabled"]:
        gradle_process += ["generateFoundryMonoTemplates"]
    else:
        gradle_process += ["generateFoundryTemplates"]
    gradle_process += ["--quiet"]
    gradle_process += _native_gradle_properties(env)

    if env["debug_symbols"] and not env["separate_debug_symbols"]:
        gradle_process += ["-PdoNotStrip=true"]

    subprocess.run(
        gradle_process,
        cwd="platform/android/java",
        check=True,
    )
