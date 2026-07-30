#!/usr/bin/env bash
# Decide whether this release may move its channel container alias.
#
# Reads the GitHub release listing from the file named by RELEASES_JSON (written by the
# workflow's `gh api --paginate --slurp` call), plus RELEASE_TAG and RELEASE_CHANNEL, and
# writes `publish_channel_tag=<true|false>` to $GITHUB_OUTPUT. A missing or unusable
# listing degrades to publishing the exact tag only.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

publish_channel_tag=false
if [ -f "$RELEASES_JSON" ]; then
  if resolved="$(python3 "$script_dir/resolve_container_alias.py" \
    --releases-json "$RELEASES_JSON" \
    --release-tag "$RELEASE_TAG" \
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
