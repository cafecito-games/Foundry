#!/usr/bin/env python3

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/release.yml"


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def require(text: str, snippet: str, context: str) -> None:
    if snippet not in text:
        fail(f"{context} is missing: {snippet!r}")


def main() -> None:
    workflow = WORKFLOW.read_text(encoding="utf-8")
    build_start = workflow.find("  build-ios:\n")
    assemble_start = workflow.find("  assemble-ios:\n")
    package_start = workflow.find("  package:\n")
    if build_start == -1:
        fail("release workflow does not define build-ios")
    if package_start == -1:
        fail("release workflow does not define package")

    build_end = assemble_start if assemble_start != -1 else len(workflow)
    build_block = workflow[build_start:build_end]
    require(build_block, "strategy:\n      fail-fast: false", "iOS build matrix")
    require(build_block, "name: iOS ${{ matrix.name }}", "iOS build matrix")
    for cache_name in ("release-ios-device", "release-ios-simulator", "debug-ios-device", "debug-ios-simulator"):
        require(build_block, f"name: {cache_name}", "iOS build matrix")
        require(build_block, f"cache-name: {cache_name}", "iOS build matrix")
    require(build_block, "arch=x86_64 ios_simulator=yes", "iOS simulator build")
    require(build_block, "name: ${{ matrix.cache-name }}", "iOS artifact upload")
    if "Compilation (debug simulator x86_64) + bundle" in build_block:
        fail("serial iOS bundle compilation marker is still present")
    if "generate_bundle=yes" in build_block:
        fail("matrix compilation should not generate the combined bundle")

    if assemble_start == -1:
        fail("release workflow does not define assemble-ios")
    assemble_end = package_start if package_start > assemble_start else len(workflow)
    assemble_block = workflow[assemble_start:assemble_end]
    require(assemble_block, "needs: build-ios", "iOS assembly job")
    require(assemble_block, "actions/download-artifact@v8", "iOS assembly job")
    require(assemble_block, "pattern: '*-ios-*'", "iOS artifact download")
    require(assemble_block, "python3 misc/scripts/package_ios_templates.py", "iOS assembly job")
    require(assemble_block, "name: release-ios", "iOS assembled artifact")

    package_block = workflow[package_start:]
    require(package_block, "- assemble-ios", "release package dependencies")
    package_needs_start = package_block.find("    needs:\n")
    package_runs_start = package_block.find("    runs-on:")
    if package_needs_start == -1 or package_runs_start == -1:
        fail("could not isolate package dependency list")
    package_needs = package_block[package_needs_start:package_runs_start]
    if "build-ios" in package_needs:
        fail("package still depends directly on build-ios")
    require(package_block, "cp artifacts/release-ios/*.zip templates/ios.zip", "iOS release package")
    print("iOS release workflow tests passed")


if __name__ == "__main__":
    main()
