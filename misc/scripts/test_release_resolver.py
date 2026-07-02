#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
RESOLVER = REPO_ROOT / ".github/scripts/resolve_release.py"
RELEASE_WORKFLOW = REPO_ROOT / ".github/workflows/release.yml"


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    sys.exit(1)


def load_resolver():
    spec = importlib.util.spec_from_file_location("resolve_release", RESOLVER)
    if spec is None or spec.loader is None:
        fail(f"could not load resolver module at {RESOLVER}")

    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def assert_equal(actual, expected, message: str) -> None:
    if actual != expected:
        fail(f"{message}: expected {expected!r}, got {actual!r}")


def assert_raises(expected_message: str, callback) -> None:
    try:
        callback()
    except ValueError as exc:
        if expected_message not in str(exc):
            fail(f"wrong error message: expected {expected_message!r} in {exc!r}")
        return
    fail(f"expected ValueError containing {expected_message!r}")


def test_manual_draft_keeps_existing_rehearsal_behavior(resolver) -> None:
    release = resolver.resolve_release(
        event_name="workflow_dispatch",
        ref_name="develop",
        engine_version=resolver.EngineVersion(0, 1, 0),
        existing_names=[
            "v0.1.0-alpha.1",
            "v0.1.0-alpha.2",
            "v0.1.0-beta.1",
        ],
        manual_mode="draft",
        manual_channel="alpha",
    )

    assert_equal(release.version, "0.1.0", "draft version")
    assert_equal(release.tag, "v0.1.0-alpha.3", "draft tag")
    assert_equal(release.status, "alpha3", "draft engine status")
    assert_equal(release.release_version, "0.1.0-alpha.3", "draft release version")
    assert_equal(release.template_version, "0.1.alpha3", "draft template version")
    assert_equal(release.draft, "true", "draft mode")
    assert_equal(release.prerelease, "true", "draft prerelease")
    assert_equal(release.create_tag, "false", "draft should not create a tag")


def test_manual_publish_creates_next_prerelease_tag(resolver) -> None:
    release = resolver.resolve_release(
        event_name="workflow_dispatch",
        ref_name="develop",
        engine_version=resolver.EngineVersion(0, 1, 0),
        existing_names=[
            "refs/tags/v0.1.0-alpha.1",
            "v0.1.0-alpha.2",
            "v0.1.0-beta.1",
        ],
        manual_mode="publish",
        manual_channel="alpha",
    )

    assert_equal(release.tag, "v0.1.0-alpha.3", "publish prerelease tag")
    assert_equal(release.status, "alpha3", "publish engine status")
    assert_equal(release.draft, "false", "publish should not be draft")
    assert_equal(release.prerelease, "true", "publish prerelease")
    assert_equal(release.create_tag, "true", "publish should create a tag")


def test_manual_publish_stable_uses_clean_semver_tag(resolver) -> None:
    release = resolver.resolve_release(
        event_name="workflow_dispatch",
        ref_name="develop",
        engine_version=resolver.EngineVersion(0, 1, 0),
        existing_names=["v0.1.0-alpha.4"],
        manual_mode="publish",
        manual_channel="stable",
    )

    assert_equal(release.tag, "v0.1.0", "stable tag")
    assert_equal(release.status, "stable", "stable engine status")
    assert_equal(release.prerelease, "false", "stable prerelease")
    assert_equal(release.template_version, "0.1.stable", "stable template version")
    assert_equal(release.create_tag, "true", "stable publish should create a tag")


def test_manual_publish_rejects_existing_stable_tag(resolver) -> None:
    assert_raises(
        "already exists",
        lambda: resolver.resolve_release(
            event_name="workflow_dispatch",
            ref_name="develop",
            engine_version=resolver.EngineVersion(0, 1, 0),
            existing_names=["v0.1.0"],
            manual_mode="publish",
            manual_channel="stable",
        ),
    )


def test_tag_push_still_parses_existing_release_tags(resolver) -> None:
    release = resolver.resolve_release(
        event_name="push",
        ref_name="v0.1.0-rc.2",
        engine_version=resolver.EngineVersion(0, 1, 0),
        existing_names=[],
        manual_mode="draft",
        manual_channel="alpha",
    )

    assert_equal(release.tag, "v0.1.0-rc.2", "push tag")
    assert_equal(release.status, "rc2", "push engine status")
    assert_equal(release.draft, "false", "tag push draft")
    assert_equal(release.create_tag, "false", "tag push should not create a tag")


def test_tag_push_rejects_version_mismatch(resolver) -> None:
    assert_raises(
        "does not match version.py",
        lambda: resolver.resolve_release(
            event_name="push",
            ref_name="v0.2.0-alpha.1",
            engine_version=resolver.EngineVersion(0, 1, 0),
            existing_names=[],
            manual_mode="draft",
            manual_channel="alpha",
        ),
    )


def test_release_workflow_wires_manual_publish_mode() -> None:
    workflow = RELEASE_WORKFLOW.read_text()
    required_snippets = [
        "description: Release mode. Draft builds assets only; "
        "publish creates the tag and publishes Maven/GitHub release.",
        "MODE_INPUT: ${{ inputs.mode }}",
        "CHANNEL_INPUT: ${{ inputs.channel }}",
        "python3 .github/scripts/resolve_release.py",
        "create_tag: ${{ steps.resolve.outputs.create_tag }}",
        "if: needs.resolve.outputs.create_tag == 'true'",
        'git push origin "refs/tags/$TAG_NAME"',
    ]
    missing = [snippet for snippet in required_snippets if snippet not in workflow]
    if missing:
        fail(f"release workflow is missing manual publish wiring: {missing}")

    forbidden_snippets = [
        "inputs.version",
        "inputs.status",
        "VERSION_INPUT",
        "STATUS_INPUT",
    ]
    present = [snippet for snippet in forbidden_snippets if snippet in workflow]
    if present:
        fail(f"release workflow still has free-form/legacy manual release inputs: {present}")


def main() -> None:
    resolver = load_resolver()
    test_manual_draft_keeps_existing_rehearsal_behavior(resolver)
    test_manual_publish_creates_next_prerelease_tag(resolver)
    test_manual_publish_stable_uses_clean_semver_tag(resolver)
    test_manual_publish_rejects_existing_stable_tag(resolver)
    test_tag_push_still_parses_existing_release_tags(resolver)
    test_tag_push_rejects_version_mismatch(resolver)
    test_release_workflow_wires_manual_publish_mode()
    print("release resolver tests passed")


if __name__ == "__main__":
    main()
