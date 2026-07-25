#!/usr/bin/env python3
from __future__ import annotations

import ast
import re
import runpy
from collections.abc import Callable
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/release.yml"
ALIAS_POLICY = REPO_ROOT / ".github/scripts/resolve_container_alias.py"

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

EXPECTED_SMOKE_COMMANDS = frozenset(
    (
        "set -euo pipefail",
        """test "$(docker image inspect "$IMAGE_REF" --format '{{.Architecture}}')" = "amd64" """.strip(),
        """test "$(docker image inspect "$IMAGE_REF" --format '{{.Config.User}}')" = "10001:10001" """.strip(),
        'version_json="$(docker run --rm "$IMAGE_REF" --version --json)"',
        """VERSION_JSON="$version_json" python3 - <<'PY'""",
        """docker run --rm "$IMAGE_REF" script eval 'print("ok")' | grep -Fx "ok" """.strip(),
        'fixture_dir="$RUNNER_TEMP/headless-container-fixture"',
        'mkdir "$fixture_dir"',
        'cp -R tests/fixtures/headless_container/. "$fixture_dir"',
        'mkdir "$fixture_dir/.foundry"',
        'docker run --rm -v "$fixture_dir:/workspace:ro" '
        "--tmpfs /workspace/.foundry:rw,uid=10001,gid=10001,mode=0700 "
        '"$IMAGE_REF" script lint --project . scripts',
        """test "$(docker image inspect "$IMAGE_REF" --format '{{ index .Config.Labels """
        """"org.opencontainers.image.version" }}')" = "$RELEASE_VERSION" """.strip(),
        """test "$(docker image inspect "$IMAGE_REF" --format '{{ index .Config.Labels """
        """"org.opencontainers.image.revision" }}')" = "$SOURCE_REVISION" """.strip(),
        """test "$(docker image inspect "$IMAGE_REF" --format '{{ index .Config.Labels """
        """"org.opencontainers.image.source" }}')" = "$SOURCE_REPOSITORY" """.strip(),
    )
)

EXPECTED_BUILD_ARGS = frozenset(
    (
        "FOUNDRY_VERSION=${{ needs.resolve.outputs.release_version }}",
        "FOUNDRY_REVISION=${{ github.sha }}",
    )
)

EXPECTED_FRESHNESS_SCRIPT = """\
set -euo pipefail

releases_json="$RUNNER_TEMP/published-releases.json"
publish_channel_tag=false
if gh api \\
  --paginate \\
  --slurp \\
  -H "Accept: application/vnd.github+json" \\
  -H "X-GitHub-Api-Version: 2022-11-28" \\
  "repos/$GITHUB_REPOSITORY/releases?per_page=100" \\
  > "$releases_json"; then
  if resolved="$(python3 .github/scripts/resolve_container_alias.py \\
    --releases-json "$releases_json" \\
    --release-tag "$RELEASE_TAG" \\
    --channel "$RELEASE_CHANNEL")"; then
    if [ "$resolved" = "true" ]; then
      publish_channel_tag=true
    fi
  else
    echo "::warning::Could not evaluate moving container alias freshness; publishing the exact tag only."
  fi
else
  echo "::warning::Could not list published GitHub releases; publishing the exact tag only."
fi
echo "publish_channel_tag=$publish_channel_tag" >> "$GITHUB_OUTPUT"
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
    nested_lines = [line for line in body.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    require(not nested_lines, f"{context}.{key} must not have nested values")
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
        if not line.strip() or line.lstrip().startswith("#"):
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
        if not line.strip() or line.lstrip().startswith("#"):
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


def require_step_fields(body: str, expected: set[str], context: str) -> None:
    actual = set(child_keys(body, 8))
    actual.discard("name")
    require(actual == expected, f"{context} fields must be exactly {sorted(expected)!r}, plus an optional display name")


def named_block(text: str, name: str, indent: int, context: str) -> str:
    value, body = field(text, name, indent, context)
    require(not value, f"{context}.{name} must be a mapping")
    return body


def parse_steps(job: str) -> list[str]:
    steps_body = named_block(job, "steps", 4, "publish-container")
    pattern = re.compile(r"^      - ([A-Za-z0-9_-]+):(.*)$", re.MULTILINE)
    matches = list(pattern.finditer(steps_body))
    all_step_headers = re.findall(r"^      - ", steps_body, re.MULTILINE)
    require(len(matches) == len(all_step_headers), "publish-container contains an unsupported step header")

    steps: list[str] = []
    for index, match in enumerate(matches):
        body_start = match.end()
        if body_start < len(steps_body) and steps_body[body_start] == "\n":
            body_start += 1
        body_end = matches[index + 1].start() if index + 1 < len(matches) else len(steps_body)
        first_field = f"        {match.group(1)}:{match.group(2)}\n"
        steps.append(first_field + steps_body[body_start:body_end])
    return steps


def find_step(steps: list[str], predicate: Callable[[str], bool], context: str) -> tuple[int, str]:
    matches = [(index, body) for index, body in enumerate(steps) if predicate(body)]
    require(len(matches) == 1, f"publish-container must define exactly one {context} step")
    return matches[0]


def step_label(body: str) -> str:
    return optional_scalar(body, "name", 8) or "unnamed step"


def step_uses(body: str) -> str:
    return optional_scalar(body, "uses", 8)


def step_id(body: str) -> str:
    return optional_scalar(body, "id", 8)


def step_run(body: str, context: str) -> str:
    value, nested = field(body, "run", 8, context)
    if value == "|":
        return literal(body, "run", 8, context)
    require(value not in ("", ">"), f"{context}.run must be a command or literal block")
    require(not nested.strip(), f"{context}.run scalar must not have nested values")
    return value + "\n"


def step_with(body: str, context: str) -> dict[str, str]:
    return mapping(named_block(body, "with", 8, context), 10, f"{context}.with")


def nested_scalar(body: str, parent: str, key: str) -> str:
    if not re.search(rf"^        {re.escape(parent)}:", body, re.MULTILINE):
        return ""
    parent_body = named_block(body, parent, 8, step_label(body))
    return optional_scalar(parent_body, key, 10)


def shell_commands(script: str) -> tuple[str, ...]:
    commands: list[str] = []
    continued = ""
    heredoc_terminator = ""
    for line in script.splitlines():
        stripped = line.strip()
        if heredoc_terminator:
            if stripped == heredoc_terminator:
                heredoc_terminator = ""
            continue
        if not stripped or stripped.startswith("#"):
            continue

        if continued:
            stripped = continued + " " + stripped
            continued = ""
        if stripped.endswith("\\"):
            continued = stripped[:-1].rstrip()
            continue

        command = " ".join(stripped.split())
        commands.append(command)
        heredoc_match = re.search(r"<<'([^']+)'$", command)
        if heredoc_match:
            heredoc_terminator = heredoc_match.group(1)

    require(not continued, "smoke script has a dangling line continuation")
    require(not heredoc_terminator, "smoke script has an unterminated heredoc")
    return tuple(commands)


def semantic_lines(text: str) -> tuple[str, ...]:
    return tuple(
        " ".join(line.split()) for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")
    )


def exact_unordered(values: tuple[str, ...], expected: frozenset[str]) -> bool:
    return frozenset(values) == expected and len(values) == len(expected)


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
        set(child_keys(on_block, 2)) == {"push", "workflow_dispatch"},
        "release workflow events must be exactly push and workflow_dispatch",
    )
    push_block = named_block(on_block, "push", 2, "release events")
    require(set(child_keys(push_block, 4)) == {"tags"}, "release push event must be tag-only")
    tags = list_items(named_block(push_block, "tags", 4, "release push"), 6, "release push tags")
    require({tag.strip("'\"") for tag in tags} == {"v*"} and len(tags) == 1, "release push tags must be exactly v*")


def validate_job_contract(workflow: str) -> tuple[str, list[str]]:
    jobs = named_block(workflow, "jobs", 0, "workflow")
    job = named_block(jobs, "publish-container", 2, "jobs")

    needs = list_items(named_block(job, "needs", 4, "publish-container"), 6, "publish-container.needs")
    require(
        set(needs) == {"resolve", "build-linux", "publish"} and len(needs) == 3,
        "publish-container needs must be exact",
    )
    require(
        scalar(job, "if", 4, "publish-container") == "needs.resolve.outputs.draft == 'false'",
        "publish-container draft gate must be exact",
    )
    require(scalar(job, "runs-on", 4, "publish-container") == "ubuntu-24.04", "container runner must be ubuntu-24.04")

    concurrency = mapping(
        named_block(job, "concurrency", 4, "publish-container"),
        6,
        "publish-container.concurrency",
    )
    require(
        concurrency
        == {
            "group": "release-container-${{ needs.resolve.outputs.channel }}",
            "cancel-in-progress": "false",
            "queue": "max",
        },
        "publish-container concurrency must serialize and retain every channel publication",
    )
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
    require(len(steps) == 11, "publish-container must contain only the eleven allowed release steps")
    return job, steps


def validate_artifact(steps: list[str]) -> tuple[int, int, int, int]:
    checkout_index, checkout = find_step(
        steps,
        lambda body: step_uses(body) == "actions/checkout@v6",
        "container checkout",
    )
    require_step_fields(checkout, {"uses"}, "container checkout")

    artifact_index, body = find_step(
        steps,
        lambda candidate: nested_scalar(candidate, "with", "name") == "release-linux-editor",
        "Linux editor artifact download",
    )
    require_step_fields(body, {"uses", "with"}, "Linux editor artifact download")
    require(
        step_uses(body) == "actions/download-artifact@v8",
        "Linux editor artifact must use actions/download-artifact@v8",
    )
    require(
        step_with(body, "Download Linux editor artifact") == {"name": "release-linux-editor", "path": "docker"},
        "Linux editor artifact configuration must contain only its current-run name and docker path",
    )

    restore_command = "chmod 0755 docker/foundry.linuxbsd.editor.x86_64\n"
    restore_index, restore = find_step(
        steps,
        lambda candidate: re.search(r"^        run:", candidate, re.MULTILINE) is not None
        and step_run(candidate, "executable restoration") == restore_command,
        "Linux executable restoration",
    )
    require_step_fields(restore, {"run"}, "Linux executable restoration")
    require(
        step_run(restore, "Linux executable restoration") == restore_command,
        "Linux executable restoration command must be exact",
    )

    setup_buildx_index, setup_buildx = find_step(
        steps,
        lambda candidate: step_uses(candidate) == "docker/setup-buildx-action@v3",
        "Docker Buildx setup",
    )
    require_step_fields(setup_buildx, {"uses"}, "Docker Buildx setup")
    return checkout_index, artifact_index, restore_index, setup_buildx_index


def validate_tags(steps: list[str]) -> tuple[int, int, int]:
    channel_index, channel_body = find_step(
        steps,
        lambda body: step_id(body) == "channel-tag",
        "channel tag resolution",
    )
    require_step_fields(channel_body, {"id", "env", "run"}, "channel tag resolution")
    channel_env = mapping(
        named_block(channel_body, "env", 8, "channel tag resolution"),
        10,
        "channel tag resolution.env",
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
        shell_commands(step_run(channel_body, "channel tag resolution")) == shell_commands(expected_channel_script),
        "moving tag logic must separate stable latest from prerelease channels",
    )

    freshness_index, freshness_body = find_step(
        steps,
        lambda body: step_id(body) == "release-freshness",
        "moving alias freshness",
    )
    require_step_fields(freshness_body, {"id", "env", "run"}, "moving alias freshness")
    freshness_env = mapping(
        named_block(freshness_body, "env", 8, "moving alias freshness"),
        10,
        "moving alias freshness.env",
    )
    require(
        freshness_env
        == {
            "GH_TOKEN": "${{ github.token }}",
            "RELEASE_CHANNEL": "${{ needs.resolve.outputs.channel }}",
            "RELEASE_TAG": "${{ needs.resolve.outputs.tag }}",
        },
        "moving alias freshness must use the current release and job token",
    )
    require(
        semantic_lines(step_run(freshness_body, "moving alias freshness")) == semantic_lines(EXPECTED_FRESHNESS_SCRIPT),
        "moving alias freshness must query published releases and fail closed to exact-tag-only publication",
    )

    metadata_index, metadata_body = find_step(
        steps,
        lambda body: step_id(body) == "metadata",
        "image metadata",
    )
    require_step_fields(metadata_body, {"id", "uses", "with"}, "image metadata")
    require(step_uses(metadata_body) == "docker/metadata-action@v5", "image metadata action must be exact")
    metadata_with = named_block(metadata_body, "with", 8, "image metadata")
    require(
        set(child_keys(metadata_with, 10)) == {"images", "tags", "labels"},
        "image metadata inputs must be exact",
    )
    require(
        scalar(metadata_with, "images", 10, "Resolve image metadata.with") == "${{ env.IMAGE_NAME }}",
        "image metadata must use IMAGE_NAME",
    )
    tags = semantic_lines(literal(metadata_with, "tags", 10, "Resolve image metadata.with"))
    require(
        set(tags)
        == {
            "type=raw,value=${{ needs.resolve.outputs.tag }}",
            "type=raw,value=${{ steps.channel-tag.outputs.tag }},"
            "enable=${{ steps.release-freshness.outputs.publish_channel_tag == 'true' }}",
        }
        and len(tags) == 2,
        "image metadata must always define the exact tag and conditionally enable the moving channel tag",
    )
    labels = semantic_lines(literal(metadata_with, "labels", 10, "Resolve image metadata.with"))
    require(
        set(labels)
        == {
            "org.opencontainers.image.version=${{ needs.resolve.outputs.release_version }}",
            "org.opencontainers.image.revision=${{ github.sha }}",
        }
        and len(labels) == 2,
        "image metadata labels must use the resolved release version and source revision",
    )
    return channel_index, freshness_index, metadata_index


def validate_alias_policy() -> None:
    require(ALIAS_POLICY.is_file(), "moving alias freshness policy script must exist")
    namespace = runpy.run_path(str(ALIAS_POLICY))
    should_publish = namespace.get("should_publish_moving_alias")
    if not callable(should_publish):
        raise ContractError("moving alias freshness policy must expose should_publish_moving_alias")

    def release(
        tag_name: str,
        *,
        draft: object = False,
        prerelease: object = True,
        published_at: object = "2026-07-25T12:00:00Z",
    ) -> dict[str, object]:
        return {
            "tag_name": tag_name,
            "draft": draft,
            "prerelease": prerelease,
            "published_at": published_at,
        }

    alpha_releases = [
        [
            release("v2.0.0-alpha.2"),
            release("v2.0.0-alpha.1"),
            release("v9.0.0-beta.1"),
            release("v9.0.0", prerelease=False),
        ]
    ]
    require(
        should_publish(alpha_releases, "v2.0.0-alpha.2", "alpha") is True,
        "newest published release in a channel must publish its moving alias",
    )
    require(
        should_publish(alpha_releases, "v2.0.0-alpha.1", "alpha") is False,
        "stale published releases must not publish a moving alias",
    )
    require(
        should_publish(alpha_releases, "v2.0.0-alpha.3", "alpha") is False,
        "a release absent from published GitHub metadata must not publish a moving alias",
    )

    stable_releases = [
        [
            release("v3.0.0", prerelease=False, draft=True),
            release("v2.0.0", prerelease=False),
            release("v4.0.0", prerelease=False, published_at=None),
        ]
    ]
    require(
        should_publish(stable_releases, "v2.0.0", "stable") is True,
        "draft and unpublished releases must not suppress the newest published alias",
    )

    current_alpha = release("v2.0.0-alpha.1")
    malformed_releases = {
        "string draft": release("v2.0.0-alpha.2", draft="false"),
        "missing draft": {key: value for key, value in release("v2.0.0-alpha.2").items() if key != "draft"},
        "string prerelease": release("v2.0.0-alpha.2", prerelease="true"),
        "missing prerelease": {key: value for key, value in release("v2.0.0-alpha.2").items() if key != "prerelease"},
        "numeric published_at": release("v2.0.0-alpha.1", published_at=1),
        "empty published_at": release("v2.0.0-alpha.1", published_at=""),
        "missing published_at": {
            key: value for key, value in release("v2.0.0-alpha.1").items() if key != "published_at"
        },
    }
    for label, malformed_release in malformed_releases.items():
        releases = [[malformed_release, current_alpha]]
        try:
            should_publish(releases, "v2.0.0-alpha.1", "alpha")
        except ValueError:
            pass
        else:
            raise ContractError(f"moving alias freshness must reject matching-channel metadata with {label}")

    unrelated_malformed_release = release("v9.0.0-beta.1", draft="false", prerelease="true", published_at=1)
    require(
        should_publish([[unrelated_malformed_release, current_alpha]], "v2.0.0-alpha.1", "alpha") is True,
        "malformed metadata from another channel must not affect the current channel",
    )

    semantic_mismatches = (
        ("alpha", "v2.0.0-alpha.1", "v2.0.0-alpha.2", False, True),
        ("beta", "v2.0.0-beta.1", "v2.0.0-beta.2", False, True),
        ("rc", "v2.0.0-rc.1", "v2.0.0-rc.2", False, True),
        ("stable", "v2.0.0", "v3.0.0", True, False),
    )
    for channel, current_tag, newer_tag, newer_prerelease, current_prerelease in semantic_mismatches:
        releases = [
            [
                release(newer_tag, prerelease=newer_prerelease),
                release(current_tag, prerelease=current_prerelease),
            ]
        ]
        try:
            should_publish(releases, current_tag, channel)
        except ValueError:
            pass
        else:
            raise ContractError(f"moving alias freshness must reject {channel} prerelease/tag disagreement")

    try:
        should_publish(alpha_releases, "2.0.0-alpha.2", "alpha")
    except ValueError:
        pass
    else:
        raise ContractError("moving alias freshness must require leading-v release tags")


def validate_builds_and_order(job: str, steps: list[str]) -> tuple[int, int, int, int]:
    build_index, local_body = find_step(
        steps,
        lambda body: nested_scalar(body, "with", "load") == "true",
        "loaded local image build",
    )
    verify_index, verify_body = find_step(
        steps,
        lambda body: nested_scalar(body, "env", "IMAGE_REF") == "foundry-headless-smoke:${{ github.sha }}",
        "headless image smoke",
    )
    login_index, login_body = find_step(
        steps,
        lambda body: step_uses(body) == "docker/login-action@v3",
        "GHCR login",
    )
    publish_index, publish_body = find_step(
        steps,
        lambda body: nested_scalar(body, "with", "push") == "true",
        "release image publication",
    )
    require_step_fields(local_body, {"uses", "with"}, "loaded local image build")
    require_step_fields(verify_body, {"env", "run"}, "headless image smoke")
    require_step_fields(login_body, {"uses", "with"}, "GHCR login")
    require_step_fields(publish_body, {"uses", "with"}, "release image publication")

    action_uses = [(index, step_uses(body)) for index, body in enumerate(steps) if step_uses(body)]
    build_actions = [index for index, uses in action_uses if uses == "docker/build-push-action@v6"]
    login_actions = [index for index, uses in action_uses if uses == "docker/login-action@v3"]
    require(
        build_actions == [build_index, publish_index],
        "only the local smoke and publication steps may run docker/build-push-action",
    )
    require(
        login_actions == [login_index],
        "GHCR authentication must occur exactly once after smoke tests",
    )
    require(
        build_index < verify_index < login_index < publish_index,
        "actual local build, smoke, login, and publish actions must be ordered",
    )

    local_with = named_block(local_body, "with", 8, "local image build")
    require(
        set(child_keys(local_with, 10)) == {"context", "file", "platforms", "load", "tags", "labels", "build-args"},
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
        exact_unordered(semantic_lines(literal(local_with, "build-args", 10, "local build")), EXPECTED_BUILD_ARGS),
        "local image build arguments must use resolved release metadata",
    )
    require(not optional_scalar(local_with, "push", 10), "local smoke build must not push")

    login_with = step_with(login_body, "GHCR login")
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

    publish_with = named_block(publish_body, "with", 8, "release image publication")
    require(
        set(child_keys(publish_with, 10))
        == {"context", "file", "platforms", "push", "tags", "labels", "build-args", "sbom", "provenance"},
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
        exact_unordered(semantic_lines(literal(publish_with, "build-args", 10, "publish")), EXPECTED_BUILD_ARGS),
        "publication build arguments must use resolved release metadata",
    )
    require(scalar(publish_with, "sbom", 10, "publish") == "true", "publication must attach an SBOM")
    require(
        scalar(publish_with, "provenance", 10, "publish") == "mode=max",
        "publication must attach maximum provenance",
    )

    for index, body in enumerate(steps):
        label = step_label(body)
        run_script = step_run(body, label) if re.search(r"^        run:", body, re.MULTILINE) else ""
        command_text = "\n".join(shell_commands(run_script)) if run_script else ""
        require(
            not re.search(
                r"(?i)\b(?:docker|podman|skopeo|oras)\s+(?:login|push)\b|\bcrane\s+auth\b|--push\b|push=true",
                command_text,
            ),
            f"{label} must not authenticate or push directly",
        )
        if index < verify_index:
            require("${{ secrets." not in body, f"{label} must not access credentials before smoke tests")

        uses = step_uses(body)
        if "login" in uses.lower() or "auth" in uses.lower():
            require(
                index == login_index and uses == "docker/login-action@v3",
                f"{label} must not authenticate outside the post-smoke GHCR login",
            )
        if "push" in uses.lower():
            require(
                uses == "docker/build-push-action@v6" and index in (build_index, publish_index),
                f"{label} must not use an unexpected push action",
            )
        require(
            "foundry-build" not in uses.lower(),
            "container publication must reuse release-linux-editor without a second Foundry build",
        )
        require(
            "scripts/agent_build.py" not in command_text and ".github/actions/foundry-build" not in command_text,
            f"{label} must not invoke the Foundry build wrappers",
        )
        forbidden_build = re.search(
            r"(?im)^\s*(?:python3?\s+-m\s+scons|scons|cmake|ninja|make|gcc|g\+\+|clang\+\+)\b",
            command_text,
        )
        require(forbidden_build is None, f"{label} must not compile Foundry from source")
    return build_index, verify_index, login_index, publish_index


def validate_smoke(steps: list[str]) -> None:
    _, verify_body = find_step(
        steps,
        lambda body: nested_scalar(body, "env", "IMAGE_REF") == "foundry-headless-smoke:${{ github.sha }}",
        "headless image smoke",
    )
    verify_env = mapping(named_block(verify_body, "env", 8, "headless image smoke"), 10, "headless image smoke.env")
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
    script = step_run(verify_body, "headless image smoke")
    commands = shell_commands(script)
    require(
        frozenset(commands) == EXPECTED_SMOKE_COMMANDS and len(commands) == len(EXPECTED_SMOKE_COMMANDS),
        "headless image smoke commands and comparisons must match the release contract",
    )
    require(commands[0] == "set -euo pipefail", "headless image smoke must enable strict mode before any command")
    require(
        commands.index('version_json="$(docker run --rm "$IMAGE_REF" --version --json)"')
        < commands.index("""VERSION_JSON="$version_json" python3 - <<'PY'"""),
        "version JSON capture must precede its Python assertion",
    )
    fixture_commands = (
        'fixture_dir="$RUNNER_TEMP/headless-container-fixture"',
        'mkdir "$fixture_dir"',
        'cp -R tests/fixtures/headless_container/. "$fixture_dir"',
        'mkdir "$fixture_dir/.foundry"',
        'docker run --rm -v "$fixture_dir:/workspace:ro" '
        "--tmpfs /workspace/.foundry:rw,uid=10001,gid=10001,mode=0700 "
        '"$IMAGE_REF" script lint --project . scripts',
    )
    require(
        tuple(commands.index(command) for command in fixture_commands)
        == tuple(sorted(commands.index(command) for command in fixture_commands)),
        "read-only lint fixture staging must precede lint execution",
    )
    require_exact_python(
        extract_version_assertion(script),
        EXPECTED_VERSION_ASSERTION,
        "version JSON smoke",
    )


def validate(workflow: str) -> None:
    validate_events(workflow)
    job, steps = validate_job_contract(workflow)
    checkout_index, artifact_index, restore_index, setup_buildx_index = validate_artifact(steps)
    channel_index, freshness_index, metadata_index = validate_tags(steps)
    build_index, _, _, _ = validate_builds_and_order(job, steps)
    require(
        checkout_index < artifact_index < restore_index < build_index,
        "checkout, artifact download, executable restoration, and local build must be ordered",
    )
    require(setup_buildx_index < build_index, "Docker Buildx setup must precede the local image build")
    require(
        channel_index < freshness_index < metadata_index < build_index,
        "channel tag resolution, freshness, image metadata, and local image build must be ordered",
    )
    validate_smoke(steps)


def main() -> None:
    validate_alias_policy()
    validate(WORKFLOW.read_text(encoding="utf-8"))
    print("Foundry release container workflow tests passed")


if __name__ == "__main__":
    main()
