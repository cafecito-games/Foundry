#!/usr/bin/env python3
"""Static contracts for authoritative Foundry Android CI and release builds."""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ANDROID_WORKFLOW = REPO_ROOT / ".github/workflows/android_builds.yml"
ANDROID_JAVA_WORKFLOW = REPO_ROOT / ".github/workflows/android_java_check.yml"
RELEASE_WORKFLOW = REPO_ROOT / ".github/workflows/release.yml"
PRE_COMMIT = REPO_ROOT / ".pre-commit-config.yaml"
RUNTIME_PIN = REPO_ROOT / "platform/android/foundry_android_runtime.json"
FOUNDRY_BUILD_ACTION = REPO_ROOT / ".github/actions/foundry-build/action.yml"

ABIS = {
    "arm64-v8a": "arm64",
    "armeabi-v7a": "arm32",
    "x86": "x86_32",
    "x86_64": "x86_64",
}
BUILD_TYPES = {
    "debug": {
        "target": "template_debug",
        "production": "no",
        "dev_mode": "no",
        "dev_build": "no",
        "debug_symbols": "no",
    },
    "dev": {
        "target": "template_debug",
        "production": "no",
        "dev_mode": "yes",
        "dev_build": "yes",
        "debug_symbols": "yes",
    },
    "release": {
        "target": "template_release",
        "production": "yes",
        "dev_mode": "no",
        "dev_build": "no",
        "debug_symbols": "no",
    },
}
EXPECTED_MATRIX = {(build_type, abi) for build_type in BUILD_TYPES for abi in ABIS}
MATRIX_FIELDS = {
    "build_type",
    "abi",
    "arch",
    "target",
    "production",
    "dev_mode",
    "dev_build",
    "debug_symbols",
    "artifact_name",
}


def _job(workflow: str, name: str) -> str:
    match = re.search(
        rf"^  {re.escape(name)}:\n(?P<body>.*?)(?=^  [a-zA-Z0-9_-]+:\n|\Z)",
        workflow,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"workflow is missing job {name!r}")
    return match.group("body")


def _native_matrix(job: str) -> dict[tuple[str, str], dict[str, str]]:
    entries: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    for line in job.splitlines():
        stripped = line.strip()
        if stripped.startswith("- build_type:"):
            if current is not None:
                entries.append(current)
            current = {"build_type": stripped.partition(":")[2].strip().strip("\"'")}
            continue
        if current is None:
            continue
        if stripped == "steps:":
            entries.append(current)
            break
        match = re.fullmatch(r"([a-z_]+):\s*(.+)", stripped)
        if match is not None and match.group(1) in MATRIX_FIELDS:
            current[match.group(1)] = match.group(2).strip().strip("\"'")
    else:
        if current is not None:
            entries.append(current)

    parsed: dict[tuple[str, str], dict[str, str]] = {}
    for entry in entries:
        key = (entry.get("build_type", ""), entry.get("abi", ""))
        if key in parsed:
            raise AssertionError(f"duplicate Android native matrix cell: {key}")
        parsed[key] = entry
    return parsed


class AndroidRuntimeWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.android = ANDROID_WORKFLOW.read_text()
        cls.android_java = ANDROID_JAVA_WORKFLOW.read_text()
        cls.release = RELEASE_WORKFLOW.read_text()
        cls.pre_commit = PRE_COMMIT.read_text()
        cls.foundry_build_action = FOUNDRY_BUILD_ACTION.read_text()
        cls.pin = json.loads(RUNTIME_PIN.read_text())
        cls.standalone_revision = cls.pin["source"]["revision"]

    def assert_matrix(self, workflow: str, job_name: str, artifact_prefix: str) -> None:
        job = _job(workflow, job_name)
        matrix = _native_matrix(job)
        self.assertEqual(EXPECTED_MATRIX, set(matrix))
        for (build_type, abi), entry in matrix.items():
            expected = {
                "build_type": build_type,
                "abi": abi,
                "arch": ABIS[abi],
                **BUILD_TYPES[build_type],
                "artifact_name": f"{artifact_prefix}{build_type}-{abi}",
            }
            self.assertEqual(expected, entry)

        for field in (
            "arch",
            "target",
            "production",
            "dev_mode",
            "dev_build",
            "debug_symbols",
        ):
            self.assertIn(f"${{{{ matrix.{field} }}}}", job)
        self.assertIn("${{ matrix.artifact_name }}", job)
        self.assertIn("tests=no", job)
        self.assertIn("swappy=yes", job)
        self.assertIn("preserve-source-tree: true", job)
        self.assertIn("bin/android-native/${ENGINE_REVISION}/${{ matrix.build_type }}/${{ matrix.abi }}", job)
        self.assertIn("libc++_shared.so", job)
        self.assertIn("libfoundry_android.so", job)
        self.assertIn("provenance.json", job)
        self.assertIn("path: ${{ runner.temp }}/android-native-artifact", job)

    def assert_assembly_contract(
        self,
        workflow: str,
        *,
        job_name: str,
        native_job: str,
        artifact_pattern: str,
    ) -> str:
        job = _job(workflow, job_name)
        self.assertIn(native_job, job)
        self.assertIn("git rev-parse HEAD", job)
        self.assertIn("repository: cafecito-games/Foundry-Android", job)
        self.assertIn(f"ref: {self.standalone_revision}", job)
        self.assertIn(f"pattern: '{artifact_pattern}'", job)
        self.assertIn("merge-multiple: true", job)
        self.assertIn("-PfoundryAndroidSource=", job)
        self.assertIn("-PfoundryNativeRoot=", job)
        self.assertIn("-PfoundryRuntimeScratch=", job)
        self.assertIn("./gradlew --no-daemon generateFoundryTemplates", job)
        for output in (
            "android_debug.apk",
            "android_dev.apk",
            "android_release.apk",
            "android_source.zip",
            "foundry-debug.aar",
            "foundry-dev.aar",
            "foundry-release.aar",
            "foundry-native.zip",
        ):
            self.assertIn(output, job)
        return job

    def test_authoritative_workflow_builds_and_assembles_complete_matrix(self) -> None:
        self.assert_matrix(self.android, "build-android-native", "android-native-")
        validation = _job(self.android, "validate-runtime-contracts")
        for command in (
            "test_android_runtime_contract",
            "test_android_runtime_build",
            "test_android_gradle_runtime_contract",
            "test_android_runtime_surface.py",
            "test_android_runtime_workflows.py",
        ):
            self.assertIn(command, validation)
        assembly = self.assert_assembly_contract(
            self.android,
            job_name="assemble-android",
            native_job="build-android-native",
            artifact_pattern="android-native-*",
        )
        self.assertIn("name: android-runtime-assembled", assembly)

    def test_android_java_check_calls_authoritative_reusable_workflow(self) -> None:
        job = _job(self.android_java, "android")
        self.assertIn("uses: ./.github/workflows/android_builds.yml", job)
        self.assertIn("checkout-ref: ${{ github.event.pull_request.head.sha || github.sha }}", job)
        for stale in (
            "foundry-build",
            "generateFoundryTemplates",
            "foundry-lib.template_debug.aar",
            "steps:",
        ):
            self.assertNotIn(stale, job)

    def test_android_native_builds_preserve_clean_source_identity(self) -> None:
        self.assertIn("preserve-source-tree:", self.foundry_build_action)
        self.assertIn('inputs.preserve-source-tree }}" != "true"', self.foundry_build_action)

    def test_release_uses_complete_matrix_and_assembly_gate(self) -> None:
        self.assert_matrix(self.release, "build-android-native", "release-android-native-")
        assembly = self.assert_assembly_contract(
            self.release,
            job_name="assemble-android",
            native_job="build-android-native",
            artifact_pattern="release-android-native-*",
        )
        self.assertIn("name: release-android-template-debug", assembly)
        self.assertIn("name: release-android-template-release", assembly)
        self.assertRegex(
            assembly,
            r"(?s)name: release-android-template-release.*?path:\s*\|.*?"
            r"android_release\.apk.*?android_source\.zip",
        )
        package = _job(self.release, "package")
        self.assertIn("- assemble-android", package)
        self.assertNotIn("- build-android", package)

    def test_release_has_no_foundry_android_maven_publication(self) -> None:
        forbidden = (
            ":lib:publish",
            "Publish Android library to Maven",
            "closeAndReleaseSonatypeStagingRepository",
            "OSSRH_USERNAME",
            "OSSRH_PASSWORD",
            "SONATYPE_STAGING_PROFILE_ID",
            "publish-publication",
        )
        present = [snippet for snippet in forbidden if snippet in self.release]
        self.assertEqual([], present)

    def test_pre_commit_runs_workflow_contract_for_relevant_files(self) -> None:
        self.assertIn("- id: foundry-android-runtime-workflows", self.pre_commit)
        self.assertIn("entry: python .github/scripts/test_android_runtime_workflows.py", self.pre_commit)
        for path in (
            "foundry-build",
            "android_builds",
            "android_java_check",
            "release",
            "foundry_android_runtime",
        ):
            self.assertIn(path, self.pre_commit)


if __name__ == "__main__":
    unittest.main()
