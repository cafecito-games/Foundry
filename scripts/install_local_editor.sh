#!/bin/sh

set -eu

if [ "$(uname -s)" != "Darwin" ]; then
    echo "Local editor installation is only supported on macOS." >&2
    exit 2
fi

script_dir="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(dirname "$script_dir")"
cd "$repo_root"

jobs="${JOBS:-$(sysctl -n hw.ncpu)}"
case "$jobs" in
    ''|0|*[!0-9]*)
        echo "JOBS must be a positive integer (received '$jobs')." >&2
        exit 2
        ;;
esac

sign_identity="${SIGN_IDENTITY:--}"
python3 scripts/agent_build.py \
    --backend ninja \
    --jobs "$jobs"

case "$(uname -m)" in
    aarch64) arch="arm64" ;;
    amd64) arch="x86_64" ;;
    *) arch="$(uname -m)" ;;
esac
binary="bin/foundry.macos.editor.dev.$arch"
if [ ! -x "$binary" ]; then
    echo "Build succeeded without producing the expected executable $binary." >&2
    exit 1
fi

bundle_root=""
staging_root=""
backup=""
destination=""
restore_previous=0

cleanup() {
    if [ "$restore_previous" -eq 1 ] && { [ -e "$backup" ] || [ -L "$backup" ]; }; then
        if ! { [ -e "$destination" ] || [ -L "$destination" ]; }; then
            mv "$backup" "$destination"
        fi
    fi
    if [ -n "$staging_root" ]; then
        rm -rf "$staging_root"
    fi
    if [ -n "$bundle_root" ]; then
        rm -rf "$bundle_root"
    fi
}
trap cleanup EXIT

version_fields="$(python3 -c 'import version; print(version.major, version.minor, version.patch, version.status)')"
# The version module fields contain no whitespace; split them into shell parameters.
# shellcheck disable=SC2086
set -- $version_fields
if [ "$#" -ne 4 ]; then
    echo "Could not read the editor version from version.py." >&2
    exit 1
fi
short_version="$1.$2"
if [ "$3" -gt 0 ]; then
    short_version="$short_version.$3"
fi
version_status="${FOUNDRY_VERSION_STATUS:-$4}"
version="$short_version.$version_status.${BUILD_NAME:-custom_build}"
bundle_root="$(mktemp -d "bin/.foundry-bundle.XXXXXX")"
app="$bundle_root/Foundry.app"
mkdir -p "$app/Contents/MacOS"
/usr/bin/ditto "misc/dist/macos_tools.app/Contents" "$app/Contents"
rm -f "$app/Contents/Info.plist"
/usr/bin/ditto "$binary" "$app/Contents/MacOS/Foundry"
/usr/bin/ditto "misc/dist/macos/editor_info_plist.template" "$app/Contents/Info.plist"
/usr/bin/plutil -replace CFBundleShortVersionString -string "$short_version" "$app/Contents/Info.plist"
/usr/bin/plutil -replace CFBundleVersion -string "$version" "$app/Contents/Info.plist"

if [ -n "$sign_identity" ]; then
    /usr/bin/codesign \
        -s "$sign_identity" \
        --deep \
        --force \
        --options=runtime \
        --entitlements "misc/dist/macos/editor_debug.entitlements" \
        "$app"
fi

install_dir="${INSTALL_DIR:-/Applications}"
mkdir -p "$install_dir"
destination="$install_dir/Foundry.app"
staging_root="$(mktemp -d "$install_dir/.foundry-install.XXXXXX")"
staged="$staging_root/Foundry.app"
backup="$staging_root/Previous-Foundry.app"

/usr/bin/ditto "$app" "$staged"
if [ -e "$destination" ] || [ -L "$destination" ]; then
    mv "$destination" "$backup"
    restore_previous=1
fi
mv "$staged" "$destination"
restore_previous=0
rm -rf "$backup"
rmdir "$staging_root"
staging_root=""
rm -rf "$bundle_root"
bundle_root=""
trap - EXIT

xattr -dr com.apple.quarantine "$destination" 2>/dev/null || true
echo "Installed $destination"
