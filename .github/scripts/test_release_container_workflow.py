#!/usr/bin/env python3
from __future__ import annotations

import ast
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/release.yml"

EXPECTED_VERSION_ASSERTION = """\
import json
import os

metadata = json.loads(os.environ["VERSION_JSON"])
expected = {
    "product": "Foundry",
    "version": os.environ["ENGINE_VERSION"],
    "release_tag": os.environ["RELEASE_TAG"],
    "channel": os.environ["RELEASE_CHANNEL"],
    "git_commit": os.environ["SOURCE_REVISION"],
}
for key, value in expected.items():
    if metadata.get(key) != value:
        raise SystemExit(f"{key}: expected {value!r}, got {metadata.get(key)!r}")
"""

EXPECTED_VERIFY_SCRIPT = """\
set -euo pipefail

test "$(docker image inspect "$IMAGE_REF" --format '{{.Architecture}}')" = "amd64"
test "$(docker image inspect "$IMAGE_REF" --format '{{.Config.User}}')" = "10001:10001"

version_json="$(docker run --rm "$IMAGE_REF" --version --json)"
VERSION_JSON="$version_json" python3 - <<'PY'
import json
import os

metadata = json.loads(os.environ["VERSION_JSON"])
expected = {
    "product": "Foundry",
    "version": os.environ["ENGINE_VERSION"],
    "release_tag": os.environ["RELEASE_TAG"],
    "channel": os.environ["RELEASE_CHANNEL"],
    "git_commit": os.environ["SOURCE_REVISION"],
}
for key, value in expected.items():
    if metadata.get(key) != value:
        raise SystemExit(f"{key}: expected {value!r}, got {metadata.get(key)!r}")
PY

docker run --rm "$IMAGE_REF" script eval 'print("ok")' | grep -Fx "ok"
fixture_dir="$RUNNER_TEMP/headless-container-fixture"
mkdir "$fixture_dir"
cp -R tests/fixtures/headless_container/. "$fixture_dir"
mkdir "$fixture_dir/.foundry"
docker run --rm \\
  -v "$fixture_dir:/workspace:ro" \\
  --tmpfs /workspace/.foundry:rw,uid=10001,gid=10001,mode=0700 \\
  "$IMAGE_REF" script lint --project . scripts

test "$(docker image inspect "$IMAGE_REF" \\
  --format '{{ index .Config.Labels "org.opencontainers.image.version" }}')" = "$RELEASE_VERSION"
test "$(docker image inspect "$IMAGE_REF" \\
  --format '{{ index .Config.Labels "org.opencontainers.image.revision" }}')" = "$SOURCE_REVISION"
test "$(docker image inspect "$IMAGE_REF" \\
  --format '{{ index .Config.Labels "org.opencontainers.image.source" }}')" = "$SOURCE_REPOSITORY"
"""

EXPECTED_STEP_NAMES = (
    "Checkout",
    "Download Linux editor artifact",
    "Restore Linux executable mode",
    "Set up Docker Buildx",
    "Resolve moving channel tag",
    "Resolve image metadata",
    "Build local smoke image",
    "Verify headless image",
    "Log in to GHCR",
    "Publish release image",
)

EXPECTED_STEP_FIELDS = {
    "Checkout": ("uses",),
    "Download Linux editor artifact": ("uses", "with"),
    "Restore Linux executable mode": ("run",),
    "Set up Docker Buildx": ("uses",),
    "Resolve moving channel tag": ("id", "env", "run"),
    "Resolve image metadata": ("id", "uses", "with"),
    "Build local smoke image": ("uses", "with"),
    "Verify headless image": ("env", "run"),
    "Log in to GHCR": ("uses", "with"),
    "Publish release image": ("uses", "with"),
}

EXPECTED_BUILD_ARGS = """\
FOUNDRY_VERSION=${{ needs.resolve.outputs.release_version }}
FOUNDRY_REVISION=${{ github.sha }}
"""


class ContractError(AssertionError):
    pass


def require(condition: bool, context: str) -> None:
    if not condition:
        raise ContractError(context)


def field(text: str, key: str, indent: int, context: str) -> tuple[str, str]:
    prefix = " " * indent
    pattern = re.compile(rf"^{prefix}{re.escape(key)}:(?: (.*))?$", re.MULTILINE)
    matches = list(pattern.finditer(text))
    require(len(matches) == 1, f"{context} must define {key!r} exactly once")
    match = matches[0]
    value = match.group(1) or ""
    body_start = match.end()
    if body_start < len(text) and text[body_start] == "\n":
        body_start += 1

    sibling_pattern = re.compile(rf"^{prefix}[A-Za-z0-9_-]+:(?: .*)?$", re.MULTILINE)
    sibling = sibling_pattern.search(text, body_start)
    body_end = sibling.start() if sibling else len(text)
    return value, text[body_start:body_end]


def scalar(text: str, key: str, indent: int, context: str) -> str:
    value, body = field(text, key, indent, context)
    require(value not in ("", "|", ">"), f"{context}.{key} must be a scalar")
    require(not body.strip(), f"{context}.{key} must not have nested values")
    return value


def optional_scalar(text: str, key: str, indent: int) -> str:
    prefix = " " * indent
    if not re.search(rf"^{prefix}{re.escape(key)}:", text, re.MULTILINE):
        return ""
    return scalar(text, key, indent, key)


def mapping(text: str, indent: int, context: str) -> dict[str, str]:
    prefix = " " * indent
    entries: dict[str, str] = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(rf"{prefix}([A-Za-z0-9_-]+): (.+)", line)
        if match is None:
            raise ContractError(f"{context} contains unsupported entry {line!r}")
        key, value = match.groups()
        require(key not in entries, f"{context} repeats {key!r}")
        entries[key] = value
    return entries


def list_items(text: str, indent: int, context: str) -> tuple[str, ...]:
    prefix = " " * indent
    items: list[str] = []
    for line in text.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(rf"{prefix}- (.+)", line)
        if match is None:
            raise ContractError(f"{context} contains unsupported entry {line!r}")
        items.append(match.group(1))
    return tuple(items)


def literal(text: str, key: str, indent: int, context: str) -> str:
    value, body = field(text, key, indent, context)
    require(value == "|", f"{context}.{key} must use a literal block")
    content_prefix = " " * (indent + 2)
    lines: list[str] = []
    for line in body.splitlines():
        if not line:
            lines.append("")
            continue
        require(line.startswith(content_prefix), f"{context}.{key} has invalid indentation")
        lines.append(line[len(content_prefix) :])
    return "\n".join(lines).rstrip() + "\n"


def child_keys(text: str, indent: int) -> tuple[str, ...]:
    prefix = " " * indent
    return tuple(re.findall(rf"^{prefix}([A-Za-z0-9_-]+):(?: .*)?$", text, re.MULTILINE))


def named_block(text: str, name: str, indent: int, context: str) -> str:
    value, body = field(text, name, indent, context)
    require(not value, f"{context}.{name} must be a mapping")
    return body


def parse_steps(job: str) -> list[tuple[str, str]]:
    steps_body = named_block(job, "steps", 4, "publish-container")
    pattern = re.compile(r"^      - name: (.+)$", re.MULTILINE)
    matches = list(pattern.finditer(steps_body))
    all_step_headers = re.findall(r"^      - ", steps_body, re.MULTILINE)
    require(len(matches) == len(all_step_headers), "every publish-container step must have a name")

    steps: list[tuple[str, str]] = []
    names = set()
    for index, match in enumerate(matches):
        name = match.group(1)
        require(name not in names, f"publish-container repeats step name {name!r}")
        names.add(name)
        body_start = match.end()
        if body_start < len(steps_body) and steps_body[body_start] == "\n":
            body_start += 1
        body_end = matches[index + 1].start() if index + 1 < len(matches) else len(steps_body)
        steps.append((name, steps_body[body_start:body_end]))
    return steps


def step_by_name(steps: list[tuple[str, str]], name: str) -> tuple[int, str]:
    matches = [(index, body) for index, (step_name, body) in enumerate(steps) if step_name == name]
    require(len(matches) == 1, f"publish-container must define step {name!r} exactly once")
    return matches[0]


def step_uses(body: str) -> str:
    return optional_scalar(body, "uses", 8)


def step_run(body: str, name: str) -> str:
    value, nested = field(body, "run", 8, name)
    if value == "|":
        return literal(body, "run", 8, name)
    require(value not in ("", ">"), f"{name}.run must be a command or literal block")
    require(not nested.strip(), f"{name}.run scalar must not have nested values")
    return value + "\n"


def step_with(body: str, name: str) -> dict[str, str]:
    return mapping(named_block(body, "with", 8, name), 10, f"{name}.with")


def normalized_shell(script: str) -> str:
    script = re.sub(r"\\\n\s*", " ", script)
    return " ".join(script.split())


def require_shell(script: str, command: str, context: str) -> None:
    require(command in normalized_shell(script), f"{context} is missing exact command {command!r}")


def extract_version_assertion(script: str) -> str:
    marker = "VERSION_JSON=\"$version_json\" python3 - <<'PY'\n"
    require(script.count(marker) == 1, "version smoke must define exactly one Python assertion")
    start = script.index(marker) + len(marker)
    end_marker = "\nPY\n"
    end = script.find(end_marker, start)
    require(end != -1, "version smoke Python assertion is missing its terminator")
    require(script.find(end_marker, end + len(end_marker)) == -1, "version smoke has multiple Python assertions")
    return script[start:end] + "\n"


def require_exact_python(actual: str, expected: str, context: str) -> None:
    actual_tree = ast.parse(actual, filename=context)
    expected_tree = ast.parse(expected, filename=f"{context} expected")
    require(
        ast.dump(actual_tree, include_attributes=False) == ast.dump(expected_tree, include_attributes=False),
        f"{context} does not perform the exact embedded metadata comparisons",
    )


def validate_events(workflow: str) -> None:
    on_block = named_block(workflow, "on", 0, "workflow")
    require(
        child_keys(on_block, 2) == ("push", "workflow_dispatch"),
        "release workflow events must be exactly push and workflow_dispatch",
    )
    push_block = named_block(on_block, "push", 2, "release events")
    require(child_keys(push_block, 4) == ("tags",), "release push event must be tag-only")
    tags = list_items(named_block(push_block, "tags", 4, "release push"), 6, "release push tags")
    require(tuple(tag.strip("'\"") for tag in tags) == ("v*",), "release push tags must be exactly v*")


def validate_job_contract(workflow: str) -> tuple[str, list[tuple[str, str]]]:
    jobs = named_block(workflow, "jobs", 0, "workflow")
    job = named_block(jobs, "publish-container", 2, "jobs")

    needs = list_items(named_block(job, "needs", 4, "publish-container"), 6, "publish-container.needs")
    require(needs == ("resolve", "build-linux", "publish"), "publish-container needs must be exact")
    require(
        scalar(job, "if", 4, "publish-container") == "needs.resolve.outputs.draft == 'false'",
        "publish-container draft gate must be exact",
    )
    require(scalar(job, "runs-on", 4, "publish-container") == "ubuntu-24.04", "container runner must be ubuntu-24.04")

    permissions = mapping(
        named_block(job, "permissions", 4, "publish-container"),
        6,
        "publish-container.permissions",
    )
    require(
        permissions == {"contents": "read", "packages": "write"},
        "publish-container permissions must be exactly contents: read and packages: write",
    )
    environment = mapping(named_block(job, "env", 4, "publish-container"), 6, "publish-container.env")
    require(
        environment == {"IMAGE_NAME": "ghcr.io/cafecito-games/foundry"},
        "publish-container image name must be exact",
    )
    steps = parse_steps(job)
    require(
        tuple(name for name, _ in steps) == EXPECTED_STEP_NAMES,
        "publish-container step names and order must match the release contract exactly",
    )
    for name, body in steps:
        require(
            child_keys(body, 8) == EXPECTED_STEP_FIELDS[name],
            f"{name} fields must match the release contract exactly",
        )
    return job, steps


def validate_artifact(steps: list[tuple[str, str]]) -> None:
    _, checkout = step_by_name(steps, "Checkout")
    require(step_uses(checkout) == "actions/checkout@v6", "container checkout action must be exact")

    _, body = step_by_name(steps, "Download Linux editor artifact")
    require(
        step_uses(body) == "actions/download-artifact@v8",
        "Linux editor artifact must use actions/download-artifact@v8",
    )
    require(
        step_with(body, "Download Linux editor artifact") == {"name": "release-linux-editor", "path": "docker"},
        "Linux editor artifact configuration must contain only its current-run name and docker path",
    )

    _, restore = step_by_name(steps, "Restore Linux executable mode")
    require(
        step_run(restore, "Restore Linux executable mode") == "chmod 0755 docker/foundry.linuxbsd.editor.x86_64\n",
        "Linux executable restoration command must be exact",
    )

    _, setup_buildx = step_by_name(steps, "Set up Docker Buildx")
    require(
        step_uses(setup_buildx) == "docker/setup-buildx-action@v3",
        "Docker Buildx setup action must be exact",
    )


def validate_tags(steps: list[tuple[str, str]]) -> None:
    _, channel_body = step_by_name(steps, "Resolve moving channel tag")
    require(
        scalar(channel_body, "id", 8, "Resolve moving channel tag") == "channel-tag", "channel tag id must be exact"
    )
    channel_env = mapping(
        named_block(channel_body, "env", 8, "Resolve moving channel tag"),
        10,
        "Resolve moving channel tag.env",
    )
    require(
        channel_env == {"CHANNEL": "${{ needs.resolve.outputs.channel }}"},
        "moving channel input must use the resolved channel",
    )
    expected_channel_script = """\
if [ "$CHANNEL" = "stable" ]; then
  echo "tag=latest" >> "$GITHUB_OUTPUT"
else
  echo "tag=latest-$CHANNEL" >> "$GITHUB_OUTPUT"
fi
"""
    require(
        step_run(channel_body, "Resolve moving channel tag") == expected_channel_script,
        "moving tag logic must separate stable latest from prerelease channels",
    )

    _, metadata_body = step_by_name(steps, "Resolve image metadata")
    require(step_uses(metadata_body) == "docker/metadata-action@v5", "image metadata action must be exact")
    metadata_with = named_block(metadata_body, "with", 8, "Resolve image metadata")
    require(
        child_keys(metadata_with, 10) == ("images", "tags", "labels"),
        "image metadata inputs must be exact",
    )
    require(
        scalar(metadata_with, "images", 10, "Resolve image metadata.with") == "${{ env.IMAGE_NAME }}",
        "image metadata must use IMAGE_NAME",
    )
    tags = tuple(
        line for line in literal(metadata_with, "tags", 10, "Resolve image metadata.with").splitlines() if line
    )
    require(
        tags
        == (
            "type=raw,value=${{ needs.resolve.outputs.tag }}",
            "type=raw,value=${{ steps.channel-tag.outputs.tag }}",
        ),
        "image metadata must define exactly the raw release tag and one moving channel tag",
    )
    labels = tuple(
        line for line in literal(metadata_with, "labels", 10, "Resolve image metadata.with").splitlines() if line
    )
    require(
        labels
        == (
            "org.opencontainers.image.version=${{ needs.resolve.outputs.release_version }}",
            "org.opencontainers.image.revision=${{ github.sha }}",
        ),
        "image metadata labels must use the resolved release version and source revision",
    )


def validate_builds_and_order(job: str, steps: list[tuple[str, str]]) -> None:
    build_index, local_body = step_by_name(steps, "Build local smoke image")
    verify_index, _ = step_by_name(steps, "Verify headless image")
    login_index, login_body = step_by_name(steps, "Log in to GHCR")
    publish_index, publish_body = step_by_name(steps, "Publish release image")

    action_uses = [(index, name, step_uses(body)) for index, (name, body) in enumerate(steps) if step_uses(body)]
    build_actions = [(index, name) for index, name, uses in action_uses if uses == "docker/build-push-action@v6"]
    login_actions = [(index, name) for index, name, uses in action_uses if uses == "docker/login-action@v3"]
    require(
        build_actions == [(build_index, "Build local smoke image"), (publish_index, "Publish release image")],
        "only the local smoke and publication steps may run docker/build-push-action",
    )
    require(
        login_actions == [(login_index, "Log in to GHCR")],
        "GHCR authentication must occur exactly once in the named login step",
    )
    require(
        build_index < verify_index < login_index < publish_index,
        "actual local build, smoke, login, and publish actions must be ordered",
    )

    local_with = named_block(local_body, "with", 8, "Build local smoke image")
    require(
        child_keys(local_with, 10) == ("context", "file", "platforms", "load", "tags", "labels", "build-args"),
        "local smoke build inputs must be exact",
    )
    require(scalar(local_with, "context", 10, "local build") == "docker", "local build context must be docker")
    require(
        scalar(local_with, "file", 10, "local build") == "docker/foundry-headless.Dockerfile",
        "local build Dockerfile must be exact",
    )
    require(scalar(local_with, "platforms", 10, "local build") == "linux/amd64", "local build must be amd64")
    require(scalar(local_with, "load", 10, "local build") == "true", "local image must be loaded for smoke tests")
    require(
        scalar(local_with, "tags", 10, "local build") == "foundry-headless-smoke:${{ github.sha }}",
        "local smoke tag must be revision-specific",
    )
    require(
        scalar(local_with, "labels", 10, "local build") == "${{ steps.metadata.outputs.labels }}",
        "local smoke image must use resolved OCI labels",
    )
    require(
        literal(local_with, "build-args", 10, "local build") == EXPECTED_BUILD_ARGS,
        "local image build arguments must use resolved release metadata",
    )
    require(not optional_scalar(local_with, "push", 10), "local smoke build must not push")

    login_with = step_with(login_body, "Log in to GHCR")
    require(
        login_with
        == {
            "registry": "ghcr.io",
            "username": "${{ github.actor }}",
            "password": "${{ secrets.GITHUB_TOKEN }}",
        },
        "GHCR login must use github.actor and the exact GITHUB_TOKEN credential",
    )
    require("GHCR_PAT" not in job, "publish-container must not use GHCR_PAT")

    publish_with = named_block(publish_body, "with", 8, "Publish release image")
    require(
        child_keys(publish_with, 10)
        == ("context", "file", "platforms", "push", "tags", "labels", "build-args", "sbom", "provenance"),
        "publication build inputs must be exact",
    )
    require(scalar(publish_with, "context", 10, "publish") == "docker", "publish context must be docker")
    require(
        scalar(publish_with, "file", 10, "publish") == "docker/foundry-headless.Dockerfile",
        "publish Dockerfile must be exact",
    )
    require(scalar(publish_with, "platforms", 10, "publish") == "linux/amd64", "publish build must be amd64")
    require(scalar(publish_with, "push", 10, "publish") == "true", "publication must push")
    require(
        scalar(publish_with, "tags", 10, "publish") == "${{ steps.metadata.outputs.tags }}",
        "publication must use only resolved image tags",
    )
    require(
        scalar(publish_with, "labels", 10, "publish") == "${{ steps.metadata.outputs.labels }}",
        "publication must use resolved OCI labels",
    )
    require(
        literal(publish_with, "build-args", 10, "publish") == EXPECTED_BUILD_ARGS,
        "publication build arguments must use resolved release metadata",
    )
    require(scalar(publish_with, "sbom", 10, "publish") == "true", "publication must attach an SBOM")
    require(
        scalar(publish_with, "provenance", 10, "publish") == "mode=max",
        "publication must attach maximum provenance",
    )

    for index, (name, body) in enumerate(steps):
        run_script = step_run(body, name) if re.search(r"^        run:", body, re.MULTILINE) else ""
        require(
            not re.search(
                r"(?i)\b(?:docker|podman|skopeo|oras)\s+(?:login|push)\b|\bcrane\s+auth\b|--push\b|push=true",
                run_script,
            ),
            f"{name} must not authenticate or push directly",
        )
        if index < verify_index:
            require("${{ secrets." not in body, f"{name} must not access credentials before smoke tests")

        uses = step_uses(body)
        if "login" in uses.lower() or "auth" in uses.lower():
            require(
                index == login_index and uses == "docker/login-action@v3",
                f"{name} must not authenticate outside the post-smoke GHCR login",
            )
        if "push" in uses.lower():
            require(
                uses == "docker/build-push-action@v6" and index in (build_index, publish_index),
                f"{name} must not use an unexpected push action",
            )
        require(
            "foundry-build" not in uses.lower(),
            "container publication must reuse release-linux-editor without a second Foundry build",
        )
        require(
            "scripts/agent_build.py" not in run_script and ".github/actions/foundry-build" not in run_script,
            f"{name} must not invoke the Foundry build wrappers",
        )
        forbidden_build = re.search(
            r"(?im)^\s*(?:python3?\s+-m\s+scons|scons|cmake|ninja|make|gcc|g\+\+|clang\+\+)\b",
            run_script,
        )
        require(forbidden_build is None, f"{name} must not compile Foundry from source")


def validate_smoke(steps: list[tuple[str, str]]) -> None:
    _, verify_body = step_by_name(steps, "Verify headless image")
    verify_env = mapping(named_block(verify_body, "env", 8, "Verify headless image"), 10, "Verify headless image.env")
    require(
        verify_env
        == {
            "IMAGE_REF": "foundry-headless-smoke:${{ github.sha }}",
            "ENGINE_VERSION": "${{ needs.resolve.outputs.version }}",
            "RELEASE_VERSION": "${{ needs.resolve.outputs.release_version }}",
            "RELEASE_TAG": "${{ needs.resolve.outputs.tag }}",
            "RELEASE_CHANNEL": "${{ needs.resolve.outputs.channel }}",
            "SOURCE_REPOSITORY": "https://github.com/cafecito-games/Foundry",
            "SOURCE_REVISION": "${{ github.sha }}",
        },
        "headless image smoke environment must use exact resolved release metadata",
    )
    script = step_run(verify_body, "Verify headless image")
    require(
        script == EXPECTED_VERIFY_SCRIPT,
        "Verify headless image must contain only the exact pre-login smoke-test commands",
    )
    require_shell(
        script,
        """test "$(docker image inspect "$IMAGE_REF" --format '{{.Architecture}}')" = "amd64" """.strip(),
        "architecture smoke assertion",
    )
    require_shell(
        script,
        """test "$(docker image inspect "$IMAGE_REF" --format '{{.Config.User}}')" = "10001:10001" """.strip(),
        "runtime user smoke assertion",
    )
    require_shell(
        script,
        'version_json="$(docker run --rm "$IMAGE_REF" --version --json)"',
        "version JSON smoke",
    )
    require_exact_python(
        extract_version_assertion(script),
        EXPECTED_VERSION_ASSERTION,
        "version JSON smoke",
    )
    require_shell(
        script,
        """docker run --rm "$IMAGE_REF" script eval 'print("ok")' | grep -Fx "ok" """.strip(),
        "script eval smoke",
    )
    require_shell(
        script,
        'cp -R tests/fixtures/headless_container/. "$fixture_dir"',
        "read-only lint fixture staging",
    )
    require_shell(script, 'mkdir "$fixture_dir/.foundry"', "lint metadata mountpoint")
    require_shell(
        script,
        '-v "$fixture_dir:/workspace:ro" --tmpfs /workspace/.foundry:rw,uid=10001,gid=10001,mode=0700 '
        '"$IMAGE_REF" script lint --project . scripts',
        "read-only lint smoke",
    )
    require_shell(
        script,
        """test "$(docker image inspect "$IMAGE_REF" --format '{{ index .Config.Labels """
        """"org.opencontainers.image.version" }}')" = "$RELEASE_VERSION" """.strip(),
        "OCI version label smoke",
    )
    require_shell(
        script,
        """test "$(docker image inspect "$IMAGE_REF" --format '{{ index .Config.Labels """
        """"org.opencontainers.image.revision" }}')" = "$SOURCE_REVISION" """.strip(),
        "OCI revision label smoke",
    )
    require_shell(
        script,
        """test "$(docker image inspect "$IMAGE_REF" --format '{{ index .Config.Labels """
        """"org.opencontainers.image.source" }}')" = "$SOURCE_REPOSITORY" """.strip(),
        "OCI source label smoke",
    )


def validate(workflow: str) -> None:
    validate_events(workflow)
    job, steps = validate_job_contract(workflow)
    validate_artifact(steps)
    validate_tags(steps)
    validate_builds_and_order(job, steps)
    validate_smoke(steps)


def main() -> None:
    validate(WORKFLOW.read_text(encoding="utf-8"))
    print("Foundry release container workflow tests passed")


if __name__ == "__main__":
    main()
