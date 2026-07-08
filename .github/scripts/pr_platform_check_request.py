#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
from typing import Any


ALLOWED_PERMISSIONS = {"write", "maintain", "admin"}
REQUEST_PATTERN = re.compile(r"\b(?:check|build)\s+every\s+platform\b", re.I)


def is_every_platform_request(body: str) -> bool:
    return bool(REQUEST_PATTERN.search(body))


def has_write_access(permission: str) -> bool:
    return permission.lower() in ALLOWED_PERMISSIONS


def short_sha(head_sha: str) -> str:
    return head_sha[:12].lower()


def build_plan(
    *,
    body: str,
    permission: str,
    is_pr: bool,
    pr_number: int,
    head_sha: str,
    head_repo: str,
) -> dict[str, Any]:
    request_found = is_every_platform_request(body)
    requested = request_found and is_pr
    authorized = requested and has_write_access(permission)

    return {
        "request_found": request_found,
        "requested": requested,
        "authorized": authorized,
        "pr_number": str(pr_number),
        "pr_head_sha": head_sha,
        "pr_short_sha": short_sha(head_sha),
        "pr_head_repo": head_repo,
    }


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


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Plan on-demand PR platform checks.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    plan = subparsers.add_parser("plan", help="Emit GitHub Actions outputs for a PR comment.")
    plan.add_argument("--body", required=True)
    plan.add_argument("--permission", required=True)
    pr_group = plan.add_mutually_exclusive_group(required=True)
    pr_group.add_argument("--is-pr", dest="is_pr", action="store_true")
    pr_group.add_argument("--no-is-pr", dest="is_pr", action="store_false")
    plan.add_argument("--pr-number", type=int, required=True)
    plan.add_argument("--head-sha", required=True)
    plan.add_argument("--head-repo", required=True)
    plan.set_defaults(func=command_plan)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
