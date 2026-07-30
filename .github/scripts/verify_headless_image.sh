#!/usr/bin/env bash
# Smoke-verify the locally built headless container image before it is pushed.
#
# Expects IMAGE_REF, ENGINE_VERSION, RELEASE_VERSION, RELEASE_TAG, RELEASE_CHANNEL,
# SOURCE_REPOSITORY, SOURCE_REVISION, and RUNNER_TEMP in the environment, and must be
# run from the repository root so the fixture project resolves.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

test "$(docker image inspect "$IMAGE_REF" --format '{{.Architecture}}')" = "amd64"
test "$(docker image inspect "$IMAGE_REF" --format '{{.Config.User}}')" = "10001:10001"

version_json="$(docker run --rm "$IMAGE_REF" --version --json)"
VERSION_JSON="$version_json" python3 "$script_dir/verify_container_version_metadata.py"

docker run --rm "$IMAGE_REF" script eval 'print("ok")' | grep -Fx "ok"
fixture_dir="$RUNNER_TEMP/headless-container-fixture"
mkdir "$fixture_dir"
cp -R tests/fixtures/headless_container/. "$fixture_dir"
mkdir "$fixture_dir/.foundry"
docker run --rm \
  -v "$fixture_dir:/workspace:ro" \
  --tmpfs /workspace/.foundry:rw,uid=10001,gid=10001,mode=0700 \
  "$IMAGE_REF" script lint --project . scripts

test "$(docker image inspect "$IMAGE_REF" \
  --format '{{ index .Config.Labels "org.opencontainers.image.version" }}')" = "$RELEASE_VERSION"
test "$(docker image inspect "$IMAGE_REF" \
  --format '{{ index .Config.Labels "org.opencontainers.image.revision" }}')" = "$SOURCE_REVISION"
test "$(docker image inspect "$IMAGE_REF" \
  --format '{{ index .Config.Labels "org.opencontainers.image.source" }}')" = "$SOURCE_REPOSITORY"
