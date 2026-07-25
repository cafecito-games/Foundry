#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/release.yml"


def require(text: str, snippet: str, context: str) -> None:
    if snippet not in text:
        raise AssertionError(f"{context} is missing {snippet!r}")


def require_order(text: str, snippets: tuple[str, ...], context: str) -> None:
    positions = [text.find(snippet) for snippet in snippets]
    if -1 in positions or positions != sorted(positions):
        raise AssertionError(f"{context} is not ordered as expected: {snippets!r}")


def main() -> None:
    workflow = WORKFLOW.read_text(encoding="utf-8")
    start = workflow.find("  publish-container:\n")
    if start == -1:
        raise AssertionError("release workflow does not define publish-container")
    block = workflow[start:]

    for dependency in ("- resolve", "- build-linux", "- publish"):
        require(block, dependency, "container dependencies")
    require(block, "if: needs.resolve.outputs.draft == 'false'", "draft gate")
    require(block, "contents: read", "container permissions")
    require(block, "packages: write", "container permissions")
    require(block, "name: release-linux-editor", "Linux editor artifact")
    require(block, "path: docker", "Docker artifact staging")
    require(block, "chmod 0755 docker/foundry.linuxbsd.editor.x86_64", "executable-bit restoration")
    if "run-id:" in block:
        raise AssertionError("Linux editor artifact must come from the current workflow run")

    require(block, "ghcr.io/cafecito-games/foundry", "GHCR image name")
    require(block, "type=raw,value=${{ needs.resolve.outputs.tag }}", "exact release tag")
    require(block, 'if [ "$CHANNEL" = "stable" ]', "stable channel branch")
    require(block, 'echo "tag=latest"', "stable tag")
    require(block, 'echo "tag=latest-$CHANNEL"', "prerelease channel tag")
    require(block, "type=raw,value=${{ steps.channel-tag.outputs.tag }}", "moving channel tag")
    require(block, "platforms: linux/amd64", "image platform")
    require(block, "load: true", "local smoke image")
    require(block, '"10001:10001"', "non-root smoke assertion")
    require(block, "--version --json", "version smoke test")
    require(block, "script eval 'print(\"ok\")'", "script eval smoke test")
    require(
        block,
        'cp -R tests/fixtures/headless_container/. "$fixture_dir"',
        "lint fixture staging",
    )
    require(block, 'mkdir "$fixture_dir/.foundry"', "lint metadata mountpoint")
    require(block, '-v "$fixture_dir:/workspace:ro"', "mounted lint fixture")
    require(
        block,
        "--tmpfs /workspace/.foundry:rw,uid=10001,gid=10001,mode=0700",
        "writable lint metadata mount",
    )
    require(block, "script lint --project . scripts", "lint smoke test")
    require(block, "org.opencontainers.image.source", "source label smoke assertion")
    require(block, "org.opencontainers.image.version", "version label smoke assertion")
    require(block, "org.opencontainers.image.revision", "revision label smoke assertion")
    require(block, "push: true", "registry publication")
    require(block, "sbom: true", "SBOM publication")
    require(block, "provenance: mode=max", "provenance publication")
    if "scons" in block.lower():
        raise AssertionError("container publication must reuse the staged binary without compiling Foundry")

    require_order(
        block,
        (
            "- name: Build local smoke image",
            "- name: Verify headless image",
            "- name: Log in to GHCR",
            "- name: Publish release image",
        ),
        "local validation and publication steps",
    )
    print("Foundry release container workflow tests passed")


if __name__ == "__main__":
    main()
