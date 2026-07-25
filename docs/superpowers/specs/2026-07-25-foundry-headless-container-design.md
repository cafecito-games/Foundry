# Foundry Headless Container Design

## Summary

Publish a minimal, public Linux container for Foundry's release-facing CI and
Foundry Script commands. Each image packages the same production Linux editor
binary already built by the Foundry release workflow. It does not rebuild the
engine in Docker.

The image is published to:

```text
ghcr.io/cafecito-games/foundry
```

The first release supports `linux/amd64` only. Foundry's source tree can target
Linux arm64, but the release workflow does not currently build or test a Linux
arm64 editor. Container arm64 support is deferred until that release artifact
exists.

## Goals

- Run release-facing Foundry CLI tools in Linux CI without installing Foundry
  on the host.
- Make headless execution automatic.
- Package the exact Linux editor binary shipped in the corresponding GitHub
  release.
- Publish exact release tags and channel-aware moving tags.
- Keep the runtime image small, non-root, public, and free of build tools.
- Verify the image before assigning public release tags.
- Publish container provenance and an SBOM.

## Non-goals

- GUI editor or editor-automation use.
- Foundry's internal C++/doctest suite, which requires a `tests=yes` build.
- Export templates or project exporting.
- Dedicated game-server hosting.
- Linux arm64 in the initial release.
- Git, compilers, language runtimes, or other general-purpose CI utilities.
- Containers for `develop`, pull requests, or draft releases.

## Release Architecture

Add a container-publishing job to `.github/workflows/release.yml`. The job
depends on:

- `resolve`, for the release tag, channel, version, draft state, and commit;
- `build-linux`, for the existing `release-linux-editor` artifact; and
- `publish`, so no container is published unless the GitHub release itself was
  published successfully.

The job runs only when `resolve.outputs.draft == 'false'`. Manual draft releases
continue to build and upload their existing artifacts without authenticating to
GHCR or publishing a container.

The container job performs this flow:

1. Check out the release commit.
2. Download only the `release-linux-editor` artifact from the current workflow
   run.
3. Restore the binary's executable bit, which GitHub artifact transport does
   not preserve.
4. Stage the binary under the fixed filename expected by the Docker build.
5. Build a local `linux/amd64` image.
6. Run all smoke tests against that local image.
7. Log in to GHCR using `GITHUB_TOKEN`.
8. Publish the validated build with its release and channel tags, OCI metadata,
   BuildKit provenance, and SBOM.

The job gets only the permissions it needs: `contents: read` and
`packages: write`. It does not receive repository write access or deployment
credentials.

Building from the existing artifact guarantees that the executable inside the
container has the same bytes and embedded version metadata as the Linux editor
in the release ZIP. The Docker build contains no SCons toolchain and does not
compile Foundry a second time.

## Runtime Image

Add a dedicated runtime Dockerfile under `docker/`. It uses Ubuntu 24.04, which
matches the proven runtime base in the existing Cafecito Games custom Godot
image.

The image contains only:

- CA certificates;
- the runtime libraries required by the production Linux editor, beginning
  with the proven custom Godot set of fontconfig, FreeType, X cursor/Xinerama/
  RandR/input, OpenGL, and PulseAudio libraries; and
- the release editor binary installed as `/usr/local/bin/foundry`.

The dependency list is validated by running the actual Foundry image. Libraries
are added only when the binary or a supported smoke test requires them. Package
manager indexes and temporary files are removed in the same layer.

The image creates a fixed, unprivileged `foundry` user and group with UID/GID
`10001`. Its writable locations are:

- `/home/foundry`;
- `/home/foundry/.config`;
- `/home/foundry/.cache`;
- `/home/foundry/.local/share`; and
- `/workspace` when it is not replaced by a bind mount.

The image sets `HOME`, the corresponding XDG environment variables, and
`WORKDIR /workspace`, then switches permanently to the `foundry` user. It does
not request Linux capabilities or use a privilege-dropping entrypoint script.

The command contract is:

```dockerfile
ENTRYPOINT ["/usr/local/bin/foundry", "--headless"]
CMD ["--help"]
```

Arguments passed to `docker run` are therefore Foundry arguments. Running the
image without arguments prints help and exits instead of starting a long-lived
process.

The image includes standard OCI labels for:

- source repository;
- source revision;
- release version;
- image title and description;
- license; and
- documentation URL.

## Image Names and Tags

Every published release gets an exact tag equal to the Git tag, including its
leading `v`:

```text
ghcr.io/cafecito-games/foundry:v0.1.0-alpha.9
```

The workflow also assigns exactly one moving channel tag:

| Release channel | Moving image tag |
| --- | --- |
| `alpha` | `latest-alpha` |
| `beta` | `latest-beta` |
| `rc` | `latest-rc` |
| `stable` | `latest` |

Prereleases never update `latest`. Stable releases do not update a
`latest-stable` alias. No unprefixed version alias is published.

Exact release tags are release-addressed: only the workflow resolving that
exact Git tag may publish them. A retry for the same release may reproduce the
tag from the same source commit; branch and pull-request workflows cannot
publish it.

## Supported User Workflows

The production editor keeps release-facing Foundry CLI tooling. Primary
examples are:

```sh
# Inspect the packaged release.
docker run --rm \
  ghcr.io/cafecito-games/foundry:v0.1.0-alpha.9 \
  --version

# Lint a mounted project.
docker run --rm \
  -v "$PWD:/workspace" \
  ghcr.io/cafecito-games/foundry:v0.1.0-alpha.9 \
  script lint .

# Run a project-owned Foundry Script test runner.
docker run --rm \
  -v "$PWD:/workspace" \
  ghcr.io/cafecito-games/foundry:v0.1.0-alpha.9 \
  project test --project . --runner res://tests/runner.fs

# Evaluate an inline Foundry Script snippet.
docker run --rm \
  ghcr.io/cafecito-games/foundry:v0.1.0-alpha.9 \
  script eval 'print("ok")'
```

Other release-facing commands such as `script format` and
`project run --script` use the same interface.

Read-only bind mounts work whenever the mounted files are world-readable.
Commands that modify project files require compatible host permissions. The
documentation includes an override for CI hosts whose checkout UID differs
from `10001`:

```sh
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp/foundry-home \
  -v "$PWD:/workspace" \
  ghcr.io/cafecito-games/foundry:v0.1.0-alpha.9 \
  script format --write .
```

Using `/tmp/foundry-home` gives an arbitrary host UID a location where Foundry
can create its configuration and cache directories without granting broader
permissions in the image.

## Verification

The release job builds a local image and runs these checks before public release
tags are assigned:

1. Inspect the image configuration and assert its architecture is `amd64`.
2. Assert the configured user is the numeric non-root UID/GID.
3. Run `--version` and assert it contains the resolved Foundry release version.
4. Run `script eval 'print("ok")'` and assert successful exit and expected
   output.
5. Mount a small read-only Foundry project fixture at `/workspace` and run
   `script lint .`.
6. Inspect the OCI source, version, and revision labels and compare them with
   the repository and resolved release metadata.

Any build, smoke-test, registry-login, attestation, or push failure fails the
container job and therefore the release workflow. Because the GitHub release
must succeed before this job starts, a failed GitHub release cannot leave a
newly tagged container behind. A container failure after GitHub publication is
visible as a failed release workflow and can be retried from the same release
commit.

## Public Package Visibility

The package must be anonymously pullable. GHCR package visibility is persistent
package configuration rather than an image label. If the Cafecito Games
organization does not make repository-linked packages public automatically,
the first successful push requires a one-time change of the
`cafecito-games/foundry` container package to public visibility in GitHub's
package settings. The README calls out this bootstrap step so the image is not
accidentally documented as public while still requiring authentication.

## Documentation

Add a concise container section to the root `README.md` covering:

- the GHCR image name;
- exact and moving tag semantics;
- automatic `--headless` behavior;
- the command examples above;
- bind-mount and non-root ownership behavior;
- the first-publication visibility check; and
- the initial `linux/amd64` limitation.

The release workflow remains the source of truth for supported channels and
published release metadata. The Dockerfile remains responsible only for the
runtime filesystem and command contract.
