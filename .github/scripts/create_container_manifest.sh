#!/usr/bin/env bash
# Join per-architecture image digests into one tagged multi-architecture index.
#
# Takes the digest directory as its only argument: one file per architecture, each
# holding the `sha256:...` digest that architecture's push produced. Reads IMAGE_NAME,
# the newline-separated TAGS to publish, and the newline-separated ANNOTATIONS to attach
# to the index. DOCKER_BIN overrides the docker executable.
set -euo pipefail

digest_dir="${1:?usage: create_container_manifest.sh <digest-dir>}"

arguments=()
while IFS= read -r annotation; do
  [ -n "$annotation" ] && arguments+=(--annotation "$annotation")
done <<< "${ANNOTATIONS:-}"

tag_count=0
while IFS= read -r tag; do
  [ -n "$tag" ] || continue
  arguments+=(--tag "$tag")
  tag_count=$((tag_count + 1))
done <<< "${TAGS:-}"

if [ "$tag_count" -eq 0 ]; then
  echo "::error::No image tags to publish." >&2
  exit 1
fi

source_count=0
for digest_file in "$digest_dir"/*; do
  [ -f "$digest_file" ] || continue
  digest="$(tr -d '[:space:]' < "$digest_file")"
  if [ -z "$digest" ]; then
    echo "::error::Empty image digest in $digest_file." >&2
    exit 1
  fi
  arguments+=("${IMAGE_NAME}@${digest}")
  source_count=$((source_count + 1))
done

if [ "$source_count" -eq 0 ]; then
  echo "::error::No image digests found in $digest_dir." >&2
  exit 1
fi

"${DOCKER_BIN:-docker}" buildx imagetools create "${arguments[@]}"
