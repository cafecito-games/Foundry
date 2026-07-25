# Foundry Headless Container Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish a minimal public `linux/amd64` Foundry image to GHCR for release-facing headless CI and Foundry
Script commands.

**Architecture:** Reuse the production Linux editor artifact already produced by `.github/workflows/release.yml`,
package it in a non-root Ubuntu 24.04 runtime image, smoke-test the local image, and publish exact plus channel-aware
tags only for non-draft releases. Keep the runtime contract, release orchestration, and user documentation
independently testable with small Python contract tests wired into pre-commit.

**Tech Stack:** Docker/BuildKit, Ubuntu 24.04, GitHub Actions, GHCR, Python 3.8-compatible contract tests, Foundry
command-first CLI

---

## File Map

- Create `docker/foundry-headless.Dockerfile`: runtime filesystem, non-root user, OCI defaults, and automatic
  headless entrypoint.
- Create `docker/.dockerignore`: restrict the Docker build context to the staged release binary.
- Modify `.gitignore`: prevent the downloaded release binary from entering Git history.
- Create `tests/fixtures/headless_container/project.foundry`: minimal mounted-project fixture used by container lint
  smoke tests.
- Create `tests/fixtures/headless_container/scripts/valid.fs`: valid Foundry Script input for the mounted lint smoke
  test.
- Create `.github/scripts/test_foundry_headless_docker.py`: static contract test for the Dockerfile, ignored
  artifact, and fixture.
- Create `.github/scripts/test_release_container_workflow.py`: static contract test for release gating, tags,
  permissions, smoke tests, provenance, and SBOM.
- Modify `.github/workflows/release.yml`: consume the Linux editor artifact, test the image, and publish it to GHCR.
- Modify `.pre-commit-config.yaml`: run the two container contract tests when their inputs change.
- Modify `README.md`: document image tags, commands, architecture, and non-root bind-mount behavior.

### Task 1: Add the minimal non-root runtime image

**Files:**
- Create: `.github/scripts/test_foundry_headless_docker.py`
- Create: `docker/foundry-headless.Dockerfile`
- Create: `docker/.dockerignore`
- Modify: `.gitignore`
- Create: `tests/fixtures/headless_container/project.foundry`
- Create: `tests/fixtures/headless_container/scripts/valid.fs`

- [ ] **Step 1: Write the failing Docker contract test**

Create `.github/scripts/test_foundry_headless_docker.py`:

```python
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
    require(dockerfile, "COPY --chown=10001:10001 foundry.linuxbsd.editor.x86_64", "release binary copy")
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
```

- [ ] **Step 2: Run the contract test and verify it fails**

Run:

```sh
python3 .github/scripts/test_foundry_headless_docker.py
```

Expected: non-zero exit with `FileNotFoundError` for `docker/foundry-headless.Dockerfile`.

- [ ] **Step 3: Create the Docker build-context controls**

Create `docker/.dockerignore`:

```dockerignore
*
!foundry.linuxbsd.editor.x86_64
```

Add this exact entry to `.gitignore`:

```gitignore
/docker/foundry.linuxbsd.editor.x86_64
```

- [ ] **Step 4: Create the runtime Dockerfile**

Create `docker/foundry-headless.Dockerfile`:

```dockerfile
FROM ubuntu:24.04

ARG FOUNDRY_VERSION
ARG FOUNDRY_REVISION

LABEL org.opencontainers.image.title="Foundry" \
      org.opencontainers.image.description="Foundry editor CLI for headless CI and Foundry Script tooling" \
      org.opencontainers.image.source="https://github.com/cafecito-games/Foundry" \
      org.opencontainers.image.documentation="https://github.com/cafecito-games/Foundry#headless-container" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="${FOUNDRY_VERSION}" \
      org.opencontainers.image.revision="${FOUNDRY_REVISION}"

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      libfontconfig1 \
      libfreetype6 \
      libgl1 \
      libpulse0 \
      libxcursor1 \
      libxi6 \
      libxinerama1 \
      libxrandr2 && \
    rm -rf /var/lib/apt/lists/* && \
    groupadd --gid 10001 foundry && \
    useradd --uid 10001 --gid 10001 --create-home --home-dir /home/foundry foundry && \
    mkdir -p /home/foundry/.config /home/foundry/.cache /home/foundry/.local/share /workspace && \
    chown -R 10001:10001 /home/foundry /workspace

COPY --chown=10001:10001 foundry.linuxbsd.editor.x86_64 /usr/local/bin/foundry
RUN chmod 0755 /usr/local/bin/foundry

ENV HOME=/home/foundry \
    XDG_CONFIG_HOME=/home/foundry/.config \
    XDG_CACHE_HOME=/home/foundry/.cache \
    XDG_DATA_HOME=/home/foundry/.local/share

WORKDIR /workspace
USER 10001:10001

ENTRYPOINT ["/usr/local/bin/foundry", "--headless"]
CMD ["--help"]
```

- [ ] **Step 5: Add the read-only lint fixture**

Create `tests/fixtures/headless_container/project.foundry`:

```ini
config_version=5

[application]

config/name="Headless Container Smoke"
config/features=PackedStringArray("0.1")

[rendering]

renderer/rendering_method="gl_compatibility"
```

Create `tests/fixtures/headless_container/scripts/valid.fs`:

```gdscript
extends RefCounted

func answer() -> int:
	return 42
```

- [ ] **Step 6: Run the Docker contract test**

Run:

```sh
python3 .github/scripts/test_foundry_headless_docker.py
```

Expected: `Foundry headless Docker contract tests passed`.

- [ ] **Step 7: Commit the runtime image**

```sh
git add .gitignore docker tests/fixtures/headless_container .github/scripts/test_foundry_headless_docker.py
git commit -m "Add Foundry headless runtime image"
```

### Task 2: Publish validated release images to GHCR

**Files:**
- Create: `.github/scripts/test_release_container_workflow.py`
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Write the failing release-workflow contract test**

Create `.github/scripts/test_release_container_workflow.py`:

```python
#!/usr/bin/env python3

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/release.yml"


def require(text: str, snippet: str, context: str) -> None:
    if snippet not in text:
        raise AssertionError(f"{context} is missing {snippet!r}")


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
    require(block, "tests/fixtures/headless_container:/workspace:ro", "mounted lint fixture")
    require(block, "script lint --project . scripts", "lint smoke test")
    require(block, "push: true", "registry publication")
    require(block, "sbom: true", "SBOM publication")
    require(block, "provenance: mode=max", "provenance publication")
    print("Foundry release container workflow tests passed")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the workflow test and verify it fails**

Run:

```sh
python3 .github/scripts/test_release_container_workflow.py
```

Expected: non-zero exit with `release workflow does not define publish-container`.

- [ ] **Step 3: Add release metadata and tag generation**

Append a `publish-container` job after `publish` in `.github/workflows/release.yml` with this job header and metadata
setup:

```yaml
  publish-container:
    needs:
      - resolve
      - build-linux
      - publish
    if: needs.resolve.outputs.draft == 'false'
    runs-on: ubuntu-24.04
    permissions:
      contents: read
      packages: write
    env:
      IMAGE_NAME: ghcr.io/cafecito-games/foundry
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Download Linux editor artifact
        uses: actions/download-artifact@v8
        with:
          name: release-linux-editor
          path: docker

      - name: Restore Linux executable mode
        run: chmod 0755 docker/foundry.linuxbsd.editor.x86_64

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Resolve moving channel tag
        id: channel-tag
        env:
          CHANNEL: ${{ needs.resolve.outputs.channel }}
        run: |
          if [ "$CHANNEL" = "stable" ]; then
            echo "tag=latest" >> "$GITHUB_OUTPUT"
          else
            echo "tag=latest-$CHANNEL" >> "$GITHUB_OUTPUT"
          fi

      - name: Resolve image metadata
        id: metadata
        uses: docker/metadata-action@v5
        with:
          images: ${{ env.IMAGE_NAME }}
          tags: |
            type=raw,value=${{ needs.resolve.outputs.tag }}
            type=raw,value=${{ steps.channel-tag.outputs.tag }}
          labels: |
            org.opencontainers.image.version=${{ needs.resolve.outputs.release_version }}
            org.opencontainers.image.revision=${{ github.sha }}
```

- [ ] **Step 4: Build and smoke-test the local image**

Continue the job with:

```yaml
      - name: Build local smoke image
        uses: docker/build-push-action@v6
        with:
          context: docker
          file: docker/foundry-headless.Dockerfile
          platforms: linux/amd64
          load: true
          tags: foundry-headless-smoke:${{ github.sha }}
          labels: ${{ steps.metadata.outputs.labels }}
          build-args: |
            FOUNDRY_VERSION=${{ needs.resolve.outputs.release_version }}
            FOUNDRY_REVISION=${{ github.sha }}

      - name: Verify headless image
        env:
          IMAGE_REF: foundry-headless-smoke:${{ github.sha }}
          ENGINE_VERSION: ${{ needs.resolve.outputs.version }}
          RELEASE_VERSION: ${{ needs.resolve.outputs.release_version }}
          RELEASE_TAG: ${{ needs.resolve.outputs.tag }}
          RELEASE_CHANNEL: ${{ needs.resolve.outputs.channel }}
          SOURCE_REPOSITORY: https://github.com/cafecito-games/Foundry
          SOURCE_REVISION: ${{ github.sha }}
        run: |
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
          docker run --rm \
            -v "$PWD/tests/fixtures/headless_container:/workspace:ro" \
            "$IMAGE_REF" script lint --project . scripts

          test "$(docker image inspect "$IMAGE_REF" \
            --format '{{ index .Config.Labels "org.opencontainers.image.version" }}')" = "$RELEASE_VERSION"
          test "$(docker image inspect "$IMAGE_REF" \
            --format '{{ index .Config.Labels "org.opencontainers.image.revision" }}')" = "$SOURCE_REVISION"
          test "$(docker image inspect "$IMAGE_REF" \
            --format '{{ index .Config.Labels "org.opencontainers.image.source" }}')" = "$SOURCE_REPOSITORY"
```

- [ ] **Step 5: Authenticate and publish the validated release tags**

Finish the job with:

```yaml
      - name: Log in to GHCR
        uses: docker/login-action@v3
        with:
          registry: ghcr.io
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Publish release image
        uses: docker/build-push-action@v6
        with:
          context: docker
          file: docker/foundry-headless.Dockerfile
          platforms: linux/amd64
          push: true
          tags: ${{ steps.metadata.outputs.tags }}
          labels: ${{ steps.metadata.outputs.labels }}
          build-args: |
            FOUNDRY_VERSION=${{ needs.resolve.outputs.release_version }}
            FOUNDRY_REVISION=${{ github.sha }}
          sbom: true
          provenance: mode=max
```

- [ ] **Step 6: Run the release-workflow contract test**

Run:

```sh
python3 .github/scripts/test_release_container_workflow.py
```

Expected: `Foundry release container workflow tests passed`.

- [ ] **Step 7: Validate the workflow syntax**

Run:

```sh
pre-commit run check-yaml --files .github/workflows/release.yml
```

Expected: the YAML hook passes.

- [ ] **Step 8: Commit release publication**

```sh
git add .github/workflows/release.yml .github/scripts/test_release_container_workflow.py
git commit -m "Publish Foundry headless release images"
```

### Task 3: Document and continuously enforce the container contract

**Files:**
- Modify: `.pre-commit-config.yaml`
- Modify: `README.md`

- [ ] **Step 1: Wire the Docker contract test into pre-commit**

Under the downstream local-hook section in `.pre-commit-config.yaml`, add:

```yaml
      - id: foundry-headless-docker-contract
        name: Foundry headless Docker contract
        language: system
        entry: python3 .github/scripts/test_foundry_headless_docker.py
        pass_filenames: false
        files: ^(\.gitignore|docker/|tests/fixtures/headless_container|\.github/scripts/test_foundry_headless_docker)

      - id: foundry-release-container-workflow
        name: Foundry release container workflow
        language: system
        entry: python3 .github/scripts/test_release_container_workflow.py
        pass_filenames: false
        files: ^(\.github/workflows/release\.yml|\.github/.*release_container|docker/|tests/fixtures/headless_container)
```

- [ ] **Step 2: Replace the stale binary-download note**

Replace the `Binary downloads` paragraph in `README.md` with:

```markdown
### Binary downloads

Published editor binaries and export templates are available from the
[Foundry releases](https://github.com/cafecito-games/Foundry/releases).
```

- [ ] **Step 3: Add the headless-container documentation**

Add this section immediately after `Binary downloads`:

````markdown
### Headless container

Published releases are also available as a minimal `linux/amd64` image for
Foundry Script tooling and project-owned test runners:

```sh
docker pull ghcr.io/cafecito-games/foundry:v0.1.0-alpha.9
```

Every image has an exact tag matching its Git release tag. Moving tags are
channel-specific: `latest-alpha`, `latest-beta`, and `latest-rc` track
prereleases, while `latest` tracks stable releases only.

The image automatically passes `--headless` to Foundry and uses `/workspace` as
its working directory:

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  ghcr.io/cafecito-games/foundry:latest-alpha \
  script lint .

docker run --rm \
  -v "$PWD:/workspace" \
  ghcr.io/cafecito-games/foundry:latest-alpha \
  project test --project . --runner res://tests/runner.fs

docker run --rm \
  ghcr.io/cafecito-games/foundry:latest-alpha \
  script eval 'print("ok")'
```

The container runs as the non-root UID/GID `10001:10001`. Read-only commands
work with readable bind mounts. For commands that rewrite files on a host whose
checkout has a different owner, run with the host UID/GID and a temporary
writable home:

```sh
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp/foundry-home \
  -v "$PWD:/workspace" \
  ghcr.io/cafecito-games/foundry:latest-alpha \
  script format --write .
```

The initial image is `linux/amd64` only and contains Foundry plus its runtime
libraries, not export templates, compilers, Git, or the internal engine test
suite. The GHCR package is intended to be public; after its first publication,
an organization administrator must confirm that the package visibility is
Public in GitHub Packages.
````

- [ ] **Step 4: Run focused contract and formatting checks**

Run:

```sh
python3 .github/scripts/test_foundry_headless_docker.py
python3 .github/scripts/test_release_container_workflow.py
pre-commit run foundry-headless-docker-contract --all-files
pre-commit run foundry-release-container-workflow --all-files
pre-commit run file-format --files \
  .github/scripts/test_foundry_headless_docker.py \
  .github/scripts/test_release_container_workflow.py \
  .github/workflows/release.yml \
  .pre-commit-config.yaml \
  README.md \
  docker/foundry-headless.Dockerfile \
  docker/.dockerignore \
  tests/fixtures/headless_container/project.foundry \
  tests/fixtures/headless_container/scripts/valid.fs
git diff --check
```

Expected: both Python tests print their success messages; all pre-commit hooks
pass; `git diff --check` produces no output.

- [ ] **Step 5: Review the final release dependency and tag matrix**

Run:

```sh
rg -n \
  'publish-container|release-linux-editor|draft ==|latest-alpha|latest-beta|latest-rc|linux/amd64|sbom|provenance' \
  .github/workflows/release.yml README.md
```

Expected: the release workflow contains one `publish-container` job gated on
non-draft publication; README documents all three prerelease moving tags,
stable `latest`, and the amd64 limitation.

- [ ] **Step 6: Commit documentation and CI gates**

```sh
git add .pre-commit-config.yaml README.md
git commit -m "Document Foundry headless containers"
```

## Final Validation

- [ ] Run both contract tests directly.
- [ ] Run the two new pre-commit hooks over all files.
- [ ] Run YAML validation on `.github/workflows/release.yml`.
- [ ] Run `git diff --check`.
- [ ] Review the diff against
  `docs/superpowers/specs/2026-07-25-foundry-headless-container-design.md`.
- [ ] On the first real non-draft release after merge, confirm:
  - the exact `v...` image tag exists;
  - only the appropriate moving channel tag changed;
  - anonymous `docker pull` succeeds;
  - OCI revision/version labels match the release;
  - provenance and SBOM attestations are present; and
  - `script eval`, mounted lint, and a project test runner work from the
    published digest.
