#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.request
from typing import Any

ALLOWED_PERMISSIONS = {"write", "maintain", "admin"}
REQUEST_PATTERN = re.compile(r"\bbuild\s+me\s+(?:an?\s+)?(?P<platform>macos|mac|osx|linux|linuxbsd)\s+editor\b", re.I)
FOLLOWUP_PLATFORM_PATTERN = re.compile(
    r"\s*(?:,\s*and|,|and)\s+(?:an?\s+)?(?P<platform>macos|mac|osx|linux|linuxbsd)\s+editor\b",
    re.I,
)

PLATFORM_METADATA = {
    "linux": {
        "label": "Linux",
        "runner": "ubuntu-22.04",
        "artifact_path": "bin/foundry.linuxbsd.editor.dev.x86_64",
    },
    "macos": {
        "label": "macOS",
        "runner": "macos-latest",
        "artifact_path": "bin/foundry.macos.editor.universal",
    },
}


def requested_platforms(body: str) -> list[str]:
    platforms: list[str] = []
    seen: set[str] = set()

    def add_platform(platform: str) -> None:
        normalized = "macos" if platform.lower() in {"macos", "mac", "osx"} else "linux"
        if normalized not in seen:
            platforms.append(normalized)
            seen.add(normalized)

    for match in REQUEST_PATTERN.finditer(body):
        add_platform(match.group("platform"))

        cursor = match.end()
        while followup := FOLLOWUP_PLATFORM_PATTERN.match(body, cursor):
            add_platform(followup.group("platform"))
            cursor = followup.end()

    return platforms


def has_write_access(permission: str) -> bool:
    return permission.lower() in ALLOWED_PERMISSIONS


def short_sha(head_sha: str) -> str:
    return head_sha[:12].lower()


def artifact_name(pr_number: int, head_sha: str, platform: str) -> str:
    return f"pr-{pr_number}-{short_sha(head_sha)}-{platform}-editor"


def matrix_entry(pr_number: int, head_sha: str, platform: str) -> dict[str, str]:
    metadata = PLATFORM_METADATA[platform]
    return {
        "os": platform,
        "label": metadata["label"],
        "runner": metadata["runner"],
        "artifact_name": artifact_name(pr_number, head_sha, platform),
        "artifact_path": metadata["artifact_path"],
    }


def platform_label(platforms: list[str]) -> str:
    labels = [PLATFORM_METADATA[platform]["label"] for platform in platforms]
    if len(labels) == 0:
        return ""
    if len(labels) == 1:
        return labels[0]
    return " and ".join(labels)


def build_plan(
    *,
    body: str,
    permission: str,
    is_pr: bool,
    pr_number: int,
    head_sha: str,
    head_repo: str,
) -> dict[str, Any]:
    platforms = requested_platforms(body)
    request_found = bool(platforms)
    requested = request_found and is_pr
    authorized = requested and has_write_access(permission)
    include = [matrix_entry(pr_number, head_sha, platform) for platform in platforms] if authorized else []

    return {
        "request_found": request_found,
        "requested": requested,
        "authorized": authorized,
        "matrix": json.dumps({"include": include}, separators=(",", ":")),
        "pr_number": str(pr_number),
        "pr_head_sha": head_sha,
        "pr_short_sha": short_sha(head_sha),
        "pr_head_repo": head_repo,
        "platforms_label": platform_label(platforms),
    }


def artifact_web_url(repository: str, artifact: dict[str, Any]) -> str:
    artifact_id = artifact.get("id", "")
    workflow_run = artifact.get("workflow_run") or {}
    run_id = workflow_run.get("id")
    if artifact_id and run_id:
        return f"https://github.com/{repository}/actions/runs/{run_id}/artifacts/{artifact_id}"
    return artifact.get("archive_download_url", "")


def artifact_run_url(repository: str, artifact: dict[str, Any]) -> str:
    workflow_run = artifact.get("workflow_run") or {}
    if workflow_run.get("html_url"):
        return workflow_run["html_url"]
    if workflow_run.get("id"):
        return f"https://github.com/{repository}/actions/runs/{workflow_run['id']}"
    return ""


def find_existing_artifact(
    artifacts: list[dict[str, Any]],
    *,
    artifact_name: str,
    repository: str,
) -> dict[str, Any] | None:
    for artifact in artifacts:
        if artifact.get("name") != artifact_name:
            continue
        if artifact.get("expired"):
            continue
        return {
            "id": artifact.get("id", ""),
            "artifact_url": artifact_web_url(repository, artifact),
            "run_url": artifact_run_url(repository, artifact),
        }
    return None


def parse_link_header(header: str) -> dict[str, str]:
    links: dict[str, str] = {}
    for part in header.split(","):
        section = part.strip()
        if not section:
            continue
        match = re.match(r'<([^>]+)>;\s*rel="([^"]+)"', section)
        if match:
            links[match.group(2)] = match.group(1)
    return links


def list_repository_artifacts(*, repository: str, token: str, api_url: str) -> list[dict[str, Any]]:
    artifacts: list[dict[str, Any]] = []
    url = f"{api_url.rstrip('/')}/repos/{repository}/actions/artifacts?per_page=100"
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2022-11-28",
    }

    while url:
        request = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = json.loads(response.read().decode("utf-8"))
            artifacts.extend(payload.get("artifacts", []))
            url = parse_link_header(response.headers.get("Link", "")).get("next", "")

    return artifacts


def emit_output(name: str, value: Any) -> None:
    if isinstance(value, bool):
        text = "true" if value else "false"
    else:
        text = str(value)

    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as output_file:
            output_file.write(f"{name}={text}\n")
    else:
        print(f"{name}={text}")


def emit_outputs(values: dict[str, Any]) -> None:
    for name, value in values.items():
        emit_output(name, value)


def command_plan(args: argparse.Namespace) -> int:
    emit_outputs(
        build_plan(
            body=args.body,
            permission=args.permission,
            is_pr=args.is_pr,
            pr_number=args.pr_number,
            head_sha=args.head_sha,
            head_repo=args.head_repo,
        )
    )
    return 0


def command_find_artifact(args: argparse.Namespace) -> int:
    artifacts = list_repository_artifacts(repository=args.repository, token=args.token, api_url=args.api_url)
    existing = find_existing_artifact(artifacts, artifact_name=args.artifact_name, repository=args.repository)
    if existing:
        emit_outputs({"found": True, **existing})
    else:
        emit_outputs({"found": False, "id": "", "artifact_url": "", "run_url": ""})
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Plan and deduplicate PR editor artifact requests.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    plan = subparsers.add_parser("plan", help="Emit GitHub outputs for a PR editor artifact request.")
    plan.add_argument("--body", required=True)
    plan.add_argument("--permission", required=True)
    plan.add_argument("--is-pr", action=argparse.BooleanOptionalAction, default=False)
    plan.add_argument("--pr-number", required=True, type=int)
    plan.add_argument("--head-sha", required=True)
    plan.add_argument("--head-repo", required=True)
    plan.set_defaults(func=command_plan)

    find_artifact = subparsers.add_parser("find-artifact", help="Emit outputs for an existing matching artifact.")
    find_artifact.add_argument("--repository", required=True)
    find_artifact.add_argument("--artifact-name", required=True)
    find_artifact.add_argument("--token", default=os.environ.get("GITHUB_TOKEN", ""))
    find_artifact.add_argument("--api-url", default=os.environ.get("GITHUB_API_URL", "https://api.github.com"))
    find_artifact.set_defaults(func=command_find_artifact)

    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
