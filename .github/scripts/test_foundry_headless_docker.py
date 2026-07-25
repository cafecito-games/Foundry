#!/usr/bin/env python3

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DOCKERFILE = REPO_ROOT / "docker/foundry-headless.Dockerfile"
DOCKERIGNORE = REPO_ROOT / "docker/.dockerignore"
GITIGNORE = REPO_ROOT / ".gitignore"
PROJECT = REPO_ROOT / "tests/fixtures/headless_container/project.foundry"
SCRIPT = REPO_ROOT / "tests/fixtures/headless_container/scripts/valid.fs"


def require(text: str, snippet: str, context: str) -> None:
    if snippet not in text:
        raise AssertionError(f"{context} is missing {snippet!r}")


def main() -> None:
    dockerfile = DOCKERFILE.read_text(encoding="utf-8")
    require(dockerfile, "FROM ubuntu:24.04", "runtime base")
    require(
        dockerfile,
        'org.opencontainers.image.source="https://github.com/cafecito-games/Foundry"',
        "source label",
    )
    for package in (
        "ca-certificates",
        "libfontconfig1",
        "libfreetype6",
        "libgl1",
        "libpulse0",
        "libxcursor1",
        "libxi6",
        "libxinerama1",
        "libxrandr2",
    ):
        require(dockerfile, package, "runtime packages")
    require(dockerfile, "groupadd --gid 10001 foundry", "non-root group")
    require(dockerfile, "useradd --uid 10001", "non-root user")
    require(
        dockerfile,
        "COPY --chown=10001:10001 --chmod=0755 foundry.linuxbsd.editor.x86_64",
        "executable release binary copy",
    )
    if "RUN chmod 0755 /usr/local/bin/foundry" in dockerfile:
        raise AssertionError("release binary must not create a separate chmod layer")
    require(dockerfile, "WORKDIR /workspace", "workspace")
    require(dockerfile, "USER 10001:10001", "runtime user")
    require(dockerfile, 'ENTRYPOINT ["/usr/local/bin/foundry", "--headless"]', "entrypoint")
    require(dockerfile, 'CMD ["--help"]', "default command")

    dockerignore = DOCKERIGNORE.read_text(encoding="utf-8")
    require(dockerignore, "*", "Docker context deny-by-default rule")
    require(dockerignore, "!foundry.linuxbsd.editor.x86_64", "Docker context binary allowlist")
    require(
        GITIGNORE.read_text(encoding="utf-8"),
        "/docker/foundry.linuxbsd.editor.x86_64",
        "release binary gitignore",
    )
    require(PROJECT.read_text(encoding="utf-8"), 'config/name="Headless Container Smoke"', "smoke project")
    require(SCRIPT.read_text(encoding="utf-8"), "func answer() -> int:", "smoke script")
    print("Foundry headless Docker contract tests passed")


if __name__ == "__main__":
    main()
