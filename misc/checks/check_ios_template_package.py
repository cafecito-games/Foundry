#!/usr/bin/env python3
"""Check that `misc/scripts/package_ios_templates.py` produces an iOS-only bundle.

`find_archive_violations` is pure over an archive's entry names and is driven by
`misc/checks/tests/test_check_ios_template_package.py`; the rest of the check
runs the real packager against synthetic inputs and a fake `lipo`.
"""

from __future__ import annotations

import os
import stat
import subprocess
import sys
import tempfile
import textwrap
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PACKAGER = REPO_ROOT / "misc/scripts/package_ios_templates.py"

LIBRARY_NAMES = [
    "libfoundry.ios.template_release.arm64.a",
    "libfoundry.ios.template_release.arm64.simulator.a",
    "libfoundry.ios.template_release.x86_64.simulator.a",
    "libfoundry.ios.template_debug.arm64.a",
    "libfoundry.ios.template_debug.arm64.simulator.a",
    "libfoundry.ios.template_debug.x86_64.simulator.a",
]


EXPECTED_ARCHIVE_ENTRIES = {
    "ios_xcode/libfoundry.ios.release.xcframework/ios-arm64/libfoundry.a",
    "ios_xcode/libfoundry.ios.release.xcframework/ios-arm64_x86_64-simulator/libfoundry.a",
    "ios_xcode/libfoundry.ios.debug.xcframework/ios-arm64/libfoundry.a",
    "ios_xcode/libfoundry.ios.debug.xcframework/ios-arm64_x86_64-simulator/libfoundry.a",
}


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def find_archive_violations(names: set[str]) -> list[str]:
    """Return every packaging violation implied by an archive's entry names."""
    violations: list[str] = []

    missing = EXPECTED_ARCHIVE_ENTRIES - names
    if missing:
        violations.append(f"iOS archive is missing expected entries: {sorted(missing)}")

    non_ios = sorted(name for name in names if "visionos" in name)
    if non_ios:
        violations.append(f"iOS archive contains non-iOS framework entries: {non_ios}")

    return violations


def make_fake_lipo(path: Path) -> None:
    path.write_text(
        textwrap.dedent(
            """\
            #!/usr/bin/env python3
            import sys
            from pathlib import Path

            args = sys.argv[1:]
            output_index = args.index("-output")
            output = Path(args[output_index + 1])
            inputs = [Path(value) for value in args[1:output_index]]
            output.write_text("fat:" + ":".join(path.read_text() for path in inputs))
            """
        )
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def make_template(template_dir: Path) -> None:
    framework_paths = [
        "libfoundry.ios.release.xcframework/ios-arm64",
        "libfoundry.ios.release.xcframework/ios-arm64_x86_64-simulator",
        "libfoundry.ios.debug.xcframework/ios-arm64",
        "libfoundry.ios.debug.xcframework/ios-arm64_x86_64-simulator",
        "libfoundry.visionos.release.xcframework/xros-arm64",
        "libfoundry.visionos.debug.xcframework/xros-arm64",
    ]
    for relative_path in framework_paths:
        destination = template_dir / relative_path
        destination.mkdir(parents=True, exist_ok=True)
        (destination / "empty").write_text("placeholder")


def run_packager(bin_dir: Path, template_dir: Path, output: Path, path: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(PACKAGER),
            "--bin-dir",
            str(bin_dir),
            "--template-dir",
            str(template_dir),
            "--output",
            str(output),
        ],
        cwd=REPO_ROOT,
        env={**os.environ, "PATH": path},
        capture_output=True,
        text=True,
    )


def check_package_contains_all_ios_slices() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        bin_dir = root / "bin"
        template_dir = root / "template"
        fake_tools = root / "tools"
        output = root / "dist" / "ios_xcode.zip"
        bin_dir.mkdir()
        template_dir.mkdir()
        fake_tools.mkdir()
        make_template(template_dir)
        make_fake_lipo(fake_tools / "lipo")

        for index, name in enumerate(LIBRARY_NAMES):
            (bin_dir / name).write_text(f"library-{index}")

        result = run_packager(bin_dir, template_dir, output, f"{fake_tools}{os.pathsep}{os.environ['PATH']}")
        if result.returncode != 0:
            fail(f"packager failed unexpectedly:\n{result.stdout}\n{result.stderr}")
        if not output.is_file():
            fail(f"packager did not create {output}")

        with zipfile.ZipFile(output) as archive:
            violations = find_archive_violations(set(archive.namelist()))
            if violations:
                fail("\n".join(violations))


def check_package_reports_missing_input() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        bin_dir = root / "bin"
        template_dir = root / "template"
        fake_tools = root / "tools"
        output = root / "dist" / "ios_xcode.zip"
        bin_dir.mkdir()
        template_dir.mkdir()
        fake_tools.mkdir()
        make_template(template_dir)
        make_fake_lipo(fake_tools / "lipo")
        for name in LIBRARY_NAMES[:-1]:
            (bin_dir / name).write_text("library")

        result = run_packager(bin_dir, template_dir, output, f"{fake_tools}{os.pathsep}{os.environ['PATH']}")
        missing_name = LIBRARY_NAMES[-1]
        if result.returncode == 0:
            fail("packager succeeded despite a missing input library")
        if missing_name not in result.stderr:
            fail(f"missing input was not reported:\n{result.stderr}")


def main() -> None:
    check_package_contains_all_ios_slices()
    check_package_reports_missing_input()
    print("iOS template packaging check passed")


if __name__ == "__main__":
    main()
