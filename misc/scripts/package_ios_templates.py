#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

LIBRARY_NAMES = {
    "release_device": "libfoundry.ios.template_release.arm64.a",
    "release_simulator_arm64": "libfoundry.ios.template_release.arm64.simulator.a",
    "release_simulator_x86_64": "libfoundry.ios.template_release.x86_64.simulator.a",
    "debug_device": "libfoundry.ios.template_debug.arm64.a",
    "debug_simulator_arm64": "libfoundry.ios.template_debug.arm64.simulator.a",
    "debug_simulator_x86_64": "libfoundry.ios.template_debug.x86_64.simulator.a",
}

IOS_FRAMEWORKS = {
    "libfoundry.ios.release.xcframework",
    "libfoundry.ios.debug.xcframework",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Assemble the iOS export template Xcode project.")
    parser.add_argument("--bin-dir", required=True, type=Path, help="Directory containing raw iOS static libraries.")
    parser.add_argument("--template-dir", required=True, type=Path, help="Apple embedded Xcode template directory.")
    parser.add_argument("--output", required=True, type=Path, help="Output zip path.")
    return parser.parse_args()


def validate_inputs(bin_dir: Path, template_dir: Path) -> dict[str, Path]:
    if not template_dir.is_dir():
        raise FileNotFoundError(f"Template directory does not exist: {template_dir}")

    libraries = {key: bin_dir / name for key, name in LIBRARY_NAMES.items()}
    missing = [path for path in libraries.values() if not path.is_file()]
    if missing:
        missing_paths = ", ".join(str(path) for path in missing)
        raise FileNotFoundError(f"Required iOS template libraries are missing: {missing_paths}")
    return libraries


def create_fat_simulator_library(bin_dir: Path, arm64: Path, x86_64: Path, name: str) -> Path:
    output = bin_dir / name
    subprocess.run(
        ["lipo", "-create", str(arm64), str(x86_64), "-output", str(output)],
        check=True,
    )
    return output


def copy_library(source: Path, staging_dir: Path, framework: str, identifier: str) -> None:
    destination_dir = staging_dir / framework / identifier
    if not destination_dir.is_dir():
        raise FileNotFoundError(f"Framework destination does not exist: {destination_dir}")
    shutil.copy2(source, destination_dir / "libfoundry.a")


def package_ios_templates(bin_dir: Path, template_dir: Path, output: Path) -> Path:
    bin_dir = bin_dir.resolve()
    template_dir = template_dir.resolve()
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    libraries = validate_inputs(bin_dir, template_dir)

    fat_libraries: list[Path] = []
    try:
        release_simulator = create_fat_simulator_library(
            bin_dir,
            libraries["release_simulator_arm64"],
            libraries["release_simulator_x86_64"],
            "libfoundry.ios.template_release.fat.simulator.a",
        )
        debug_simulator = create_fat_simulator_library(
            bin_dir,
            libraries["debug_simulator_arm64"],
            libraries["debug_simulator_x86_64"],
            "libfoundry.ios.template_debug.fat.simulator.a",
        )
        fat_libraries.extend((release_simulator, debug_simulator))

        with tempfile.TemporaryDirectory(dir=output.parent, prefix=".ios-template-") as temporary_directory:
            temporary_root = Path(temporary_directory)
            staging_dir = temporary_root / "ios_xcode"
            shutil.copytree(template_dir, staging_dir)

            for framework_dir in staging_dir.glob("*.xcframework"):
                if framework_dir.name not in IOS_FRAMEWORKS:
                    shutil.rmtree(framework_dir)

            copy_library(libraries["release_device"], staging_dir, "libfoundry.ios.release.xcframework", "ios-arm64")
            copy_library(
                release_simulator,
                staging_dir,
                "libfoundry.ios.release.xcframework",
                "ios-arm64_x86_64-simulator",
            )
            copy_library(libraries["debug_device"], staging_dir, "libfoundry.ios.debug.xcframework", "ios-arm64")
            copy_library(
                debug_simulator,
                staging_dir,
                "libfoundry.ios.debug.xcframework",
                "ios-arm64_x86_64-simulator",
            )

            if output.exists():
                output.unlink()
            archive_base = output.with_suffix("")
            archive_path = Path(
                shutil.make_archive(str(archive_base), "zip", root_dir=temporary_root, base_dir="ios_xcode")
            )
    finally:
        for fat_library in fat_libraries:
            fat_library.unlink(missing_ok=True)

    return archive_path


def main() -> None:
    args = parse_args()
    try:
        output = package_ios_templates(args.bin_dir, args.template_dir, args.output)
    except (FileNotFoundError, OSError, subprocess.CalledProcessError) as error:
        print(f"iOS template packaging failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
    print(output)


if __name__ == "__main__":
    main()
