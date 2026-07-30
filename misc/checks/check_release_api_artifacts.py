#!/usr/bin/env python3
"""Check the shape of the release API artifact bundle and its workflow wiring.

`find_bundle_violations` and `find_workflow_wiring_violations` are pure and are
driven by `misc/checks/tests/test_check_release_api_artifacts.py`; the rest of
the check runs `misc/scripts/package_foundry_api_artifacts.py` against a fake
`foundry` binary.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import textwrap
import zipfile
from collections.abc import Iterable
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
PACKAGER = REPO_ROOT / "misc/scripts/package_foundry_api_artifacts.py"
RELEASE_WORKFLOW = REPO_ROOT / ".github/workflows/release.yml"


EXPECTED_BUNDLE_ENTRIES = {
    "extension_api.json",
    "extension_api_with_docs.json",
    "foundry_extension_interface.h",
    "foundry_extension_interface.json",
    "metadata.json",
}

REQUIRED_WORKFLOW_SNIPPETS = [
    "package_foundry_api_artifacts.py",
    "name: release-api",
    "api-artifacts/*",
    "artifacts/release-api/*.zip",
]


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    sys.exit(1)


def find_bundle_violations(
    names: Iterable[str],
    metadata: dict[str, Any],
    version: str,
    tag: str,
    commit: str,
    header_full_name: str,
) -> list[str]:
    """Return every violation of the API bundle contract, not just the first."""
    violations: list[str] = []

    entries = set(names)
    if entries != EXPECTED_BUNDLE_ENTRIES:
        violations.append(f"unexpected zip entries: {sorted(entries)}")

    for field, expected in (("version", version), ("tag", tag), ("commit", commit)):
        if metadata.get(field) != expected:
            violations.append(f"metadata {field} mismatch: expected {expected!r}, got {metadata.get(field)!r}")

    header = metadata.get("extension_api_header") or {}
    if header.get("version_full_name") != header_full_name:
        violations.append("metadata did not preserve extension API header")

    files = metadata.get("files") or []
    inventory = sorted(entry["name"] for entry in files)
    if inventory != sorted(EXPECTED_BUNDLE_ENTRIES - {"metadata.json"}):
        violations.append(f"metadata file inventory mismatch: {inventory}")

    bad_digests = sorted(entry["name"] for entry in files if len(entry.get("sha256", "")) != 64)
    if bad_digests:
        violations.append(f"metadata sha256 values are not hex digests: {bad_digests}")

    return violations


def find_workflow_wiring_violations(workflow: str) -> list[str]:
    """Return every missing API-artifact wiring snippet in the release workflow."""
    missing = [snippet for snippet in REQUIRED_WORKFLOW_SNIPPETS if snippet not in workflow]
    if missing:
        return [f"release workflow is missing API artifact wiring: {missing}"]
    return []


def make_fake_foundry_binary(path: Path) -> None:
    path.write_text(
        textwrap.dedent(
            """\
            #!/usr/bin/env python3

            import json
            import sys
            from pathlib import Path

            args = sys.argv[1:]

            def has_pair(flag: str, value: str | None = None) -> bool:
                if flag not in args:
                    return False
                if value is None:
                    return True
                index = args.index(flag)
                return index + 1 < len(args) and args[index + 1] == value

            wants_api_with_docs = (
                "--dump-extension-api-with-docs" in args
                or (has_pair("docs", "generate-api") and "--include-docs" in args)
            )
            wants_api = (
                "--dump-extension-api" in args
                or (has_pair("docs", "generate-api") and not wants_api_with_docs)
            )
            wants_interface_json = (
                "--dump-foundryextension-interface-json" in args
                or (has_pair("extension", "dump-interface") and has_pair("--format", "json"))
            )
            wants_interface_header = (
                "--dump-foundryextension-interface" in args
                or (has_pair("extension", "dump-interface") and not wants_interface_json)
            )

            if wants_api_with_docs:
                print("Dumping Extension API including documentation")
                Path("extension_api.json").write_text(json.dumps({
                    "header": {
                        "version_major": 0,
                        "version_minor": 1,
                        "version_patch": 0,
                        "version_status": "alpha1",
                        "version_build": "test_build",
                        "version_full_name": "Foundry v0.1.alpha1.test_build",
                        "precision": "single"
                    },
                    "classes": [{"name": "FoundryScript"}],
                    "docs": True
                }))
            elif wants_api:
                print("Dumping Extension API")
                Path("extension_api.json").write_text(json.dumps({
                    "header": {
                        "version_major": 0,
                        "version_minor": 1,
                        "version_patch": 0,
                        "version_status": "alpha1",
                        "version_build": "test_build",
                        "version_full_name": "Foundry v0.1.alpha1.test_build",
                        "precision": "single"
                    },
                    "classes": [{"name": "FoundryScript"}],
                    "docs": False
                }))
            elif wants_interface_json:
                print("Dumping FoundryExtension interface json file")
                Path("foundry_extension_interface.json").write_text(json.dumps({
                    "format_version": 1,
                    "types": [{"name": "FoundryExtensionBool"}]
                }))
            elif wants_interface_header:
                print("Dumping FoundryExtension interface header file")
                Path("foundry_extension_interface.h").write_text("typedef unsigned char FoundryExtensionBool;\\n")
            else:
                print("unexpected args", sys.argv[1:], file=sys.stderr)
                sys.exit(2)
            """
        )
    )
    path.chmod(0o755)


def check_packager() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        fake_binary = tmp_path / "foundry"
        out_dir = tmp_path / "dist"
        make_fake_foundry_binary(fake_binary)

        result = subprocess.run(
            [
                sys.executable,
                str(PACKAGER),
                "--binary",
                str(fake_binary),
                "--output-dir",
                str(out_dir),
                "--version",
                "0.1.0-alpha.1",
                "--tag",
                "v0.1.0-alpha.1",
                "--commit",
                "abc123",
                "--generated-at",
                "2026-07-01T00:00:00Z",
            ],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        zip_path = out_dir / "Foundry_v0.1.0-alpha.1_api.zip"
        stdout_lines = result.stdout.strip().splitlines()
        if stdout_lines != [str(zip_path)]:
            fail(f"packager stdout should contain only the zip path, got: {stdout_lines}")
        if "Dumping Extension API" not in result.stderr:
            fail("packager should forward Foundry dump output to stderr")

        if not zip_path.is_file():
            fail(f"missing API zip: {zip_path}")

        with zipfile.ZipFile(zip_path) as archive:
            violations = find_bundle_violations(
                archive.namelist(),
                json.loads(archive.read("metadata.json")),
                version="0.1.0-alpha.1",
                tag="v0.1.0-alpha.1",
                commit="abc123",
                header_full_name="Foundry v0.1.alpha1.test_build",
            )
            if violations:
                fail("\n".join(violations))


def check_release_workflow_wires_api_bundle() -> None:
    violations = find_workflow_wiring_violations(RELEASE_WORKFLOW.read_text())
    if violations:
        fail("\n".join(violations))


def main() -> None:
    check_packager()
    check_release_workflow_wires_api_bundle()
    print("release API artifact check passed")


if __name__ == "__main__":
    main()
