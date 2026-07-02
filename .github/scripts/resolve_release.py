#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

VALID_CHANNELS = {"alpha", "beta", "rc", "stable"}
VALID_MODES = {"draft", "publish"}
STABLE_TAG_RE = re.compile(r"^v(?P<version>[0-9]+\.[0-9]+\.[0-9]+)$")
PRERELEASE_TAG_RE = re.compile(
    r"^v(?P<version>[0-9]+\.[0-9]+\.[0-9]+)-(?P<channel>alpha|beta|rc)\.(?P<number>[1-9][0-9]*)$"
)


@dataclass(frozen=True)
class EngineVersion:
    major: int
    minor: int
    patch: int

    @property
    def base(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


@dataclass(frozen=True)
class Release:
    version: str
    tag: str
    status: str
    prerelease_number: str
    release_version: str
    release_name: str
    template_version: str
    draft: str
    prerelease: str
    create_tag: str

    def as_outputs(self) -> dict[str, str]:
        return {
            "version": self.version,
            "tag": self.tag,
            "status": self.status,
            "prerelease_number": self.prerelease_number,
            "release_version": self.release_version,
            "release_name": self.release_name,
            "template_version": self.template_version,
            "draft": self.draft,
            "prerelease": self.prerelease,
            "create_tag": self.create_tag,
        }


def normalize_existing_name(name: str) -> str:
    name = name.strip()
    if name.startswith("refs/tags/"):
        name = name[len("refs/tags/") :]
    if name.endswith("^{}"):
        name = name[:-3]
    return name


def next_prerelease_number(base_version: str, channel: str, existing_names: list[str]) -> int:
    max_number = 0
    for raw_name in existing_names:
        name = normalize_existing_name(raw_name)
        match = PRERELEASE_TAG_RE.fullmatch(name)
        if match is None:
            continue
        if match.group("version") == base_version and match.group("channel") == channel:
            max_number = max(max_number, int(match.group("number")))
    return max_number + 1


def tag_exists(tag: str, existing_names: list[str]) -> bool:
    return any(normalize_existing_name(name) == tag for name in existing_names)


def template_version_for(engine_version: EngineVersion, status: str) -> str:
    patch = f".{engine_version.patch}" if engine_version.patch else ""
    return f"{engine_version.major}.{engine_version.minor}{patch}.{status}"


def build_release(
    *,
    engine_version: EngineVersion,
    tag: str,
    status: str,
    prerelease_number: str,
    release_version: str,
    draft: bool,
    create_tag: bool,
) -> Release:
    return Release(
        version=engine_version.base,
        tag=tag,
        status=status,
        prerelease_number=prerelease_number,
        release_version=release_version,
        release_name=f"Foundry {release_version}",
        template_version=template_version_for(engine_version, status),
        draft=str(draft).lower(),
        prerelease=str(status != "stable").lower(),
        create_tag=str(create_tag).lower(),
    )


def resolve_push_release(ref_name: str, engine_version: EngineVersion) -> Release:
    stable_match = STABLE_TAG_RE.fullmatch(ref_name)
    prerelease_match = PRERELEASE_TAG_RE.fullmatch(ref_name)

    if stable_match is not None:
        release_version = stable_match.group("version")
        status = "stable"
        prerelease_number = ""
    elif prerelease_match is not None:
        release_version = ref_name[1:]
        status = f"{prerelease_match.group('channel')}{prerelease_match.group('number')}"
        prerelease_number = prerelease_match.group("number")
    else:
        raise ValueError(
            f"Invalid release tag '{ref_name}'; expected vX.Y.Z or vX.Y.Z-alpha.N, vX.Y.Z-beta.N, vX.Y.Z-rc.N."
        )

    version = release_version.split("-", 1)[0]
    if version != engine_version.base:
        raise ValueError(f"Requested release version '{version}' does not match version.py ('{engine_version.base}').")

    return build_release(
        engine_version=engine_version,
        tag=ref_name,
        status=status,
        prerelease_number=prerelease_number,
        release_version=release_version,
        draft=False,
        create_tag=False,
    )


def resolve_manual_release(
    *,
    engine_version: EngineVersion,
    existing_names: list[str],
    manual_mode: str,
    manual_channel: str,
) -> Release:
    if manual_mode not in VALID_MODES:
        raise ValueError(f"Invalid release mode '{manual_mode}'; expected draft or publish.")
    if manual_channel not in VALID_CHANNELS:
        raise ValueError(f"Invalid release channel '{manual_channel}'; expected alpha, beta, rc, or stable.")

    if manual_channel == "stable":
        release_version = engine_version.base
        tag = f"v{release_version}"
        status = "stable"
        prerelease_number = ""
    else:
        number = next_prerelease_number(engine_version.base, manual_channel, existing_names)
        prerelease_number = str(number)
        release_version = f"{engine_version.base}-{manual_channel}.{number}"
        tag = f"v{release_version}"
        status = f"{manual_channel}{number}"

    if manual_mode == "publish" and tag_exists(tag, existing_names):
        raise ValueError(f"Release tag '{tag}' already exists.")

    return build_release(
        engine_version=engine_version,
        tag=tag,
        status=status,
        prerelease_number=prerelease_number,
        release_version=release_version,
        draft=manual_mode == "draft",
        create_tag=manual_mode == "publish",
    )


def resolve_release(
    *,
    event_name: str,
    ref_name: str,
    engine_version: EngineVersion,
    existing_names: list[str],
    manual_mode: str,
    manual_channel: str,
) -> Release:
    if event_name == "push":
        return resolve_push_release(ref_name, engine_version)
    if event_name == "workflow_dispatch":
        return resolve_manual_release(
            engine_version=engine_version,
            existing_names=existing_names,
            manual_mode=manual_mode,
            manual_channel=manual_channel,
        )
    raise ValueError(f"Unsupported release event '{event_name}'.")


def run_command(args: list[str], *, cwd: Path) -> str:
    result = subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise ValueError(f"Command failed ({' '.join(args)}): {result.stderr.strip()}")
    return result.stdout


def collect_existing_names(repo_root: Path, repository: str) -> list[str]:
    tag_output = run_command(["git", "ls-remote", "--tags", "origin", "refs/tags/v*"], cwd=repo_root)
    existing = [line.split()[1] for line in tag_output.splitlines() if line.split()]

    release_output = run_command(
        ["gh", "release", "list", "-R", repository, "--limit", "1000", "--json", "tagName", "--jq", ".[].tagName"],
        cwd=repo_root,
    )
    existing.extend(line for line in release_output.splitlines() if line.strip())
    return sorted(set(existing))


def read_engine_version(repo_root: Path) -> EngineVersion:
    version_path = repo_root / "version.py"
    spec = importlib.util.spec_from_file_location("foundry_version", version_path)
    if spec is None or spec.loader is None:
        raise ValueError(f"Could not load {version_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return EngineVersion(int(module.major), int(module.minor), int(module.patch))


def write_github_outputs(path: str, outputs: dict[str, str]) -> None:
    with open(path, "a", encoding="utf-8") as output_file:
        for key, value in outputs.items():
            output_file.write(f"{key}={value}\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Resolve Foundry release metadata for GitHub Actions.")
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--event-name", default=os.environ.get("EVENT_NAME", ""))
    parser.add_argument("--ref-name", default=os.environ.get("REF_NAME", ""))
    parser.add_argument("--repository", default=os.environ.get("REPOSITORY", ""))
    parser.add_argument("--manual-mode", default=os.environ.get("MODE_INPUT", "draft"))
    parser.add_argument("--manual-channel", default=os.environ.get("CHANNEL_INPUT", "alpha"))
    parser.add_argument("--existing-name", action="append", default=None)
    parser.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        engine_version = read_engine_version(args.repo_root)
        existing_names = (
            args.existing_name
            if args.existing_name is not None
            else collect_existing_names(args.repo_root, args.repository)
        )
        release = resolve_release(
            event_name=args.event_name,
            ref_name=args.ref_name,
            engine_version=engine_version,
            existing_names=existing_names,
            manual_mode=args.manual_mode,
            manual_channel=args.manual_channel,
        )
        outputs = release.as_outputs()
        if args.github_output:
            write_github_outputs(args.github_output, outputs)

        print(
            f"Releasing {release.release_version} (tag {release.tag}, engine status {release.status}, "
            f"template dir {release.template_version}, draft={release.draft}, "
            f"prerelease={release.prerelease}, create_tag={release.create_tag})"
        )
        return 0
    except ValueError as exc:
        print(f"::error::{exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
