from __future__ import annotations

import hashlib
import http.server
import importlib.util
import io
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import threading
import unittest
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path
from types import ModuleType

from tests.python_build.android_native_test_support import (
    EXTERNAL_JNI_SYMBOLS,
    FOUNDRY_SYMBOLS,
    populate_native_matrix,
)
from tests.python_build.test_android_gradle_behavioral import (
    copy_gradle_fixture,
    find_android_sdk,
    git_object,
)

REPO_ROOT = Path(__file__).resolve().parents[2]

EXPORTER = REPO_ROOT / "platform/android/export/export_plugin.cpp"
APP_BUILD = REPO_ROOT / "platform/android/java/app/build.gradle"
APP_CONFIG = REPO_ROOT / "platform/android/java/app/config.gradle"
SOURCE_TEMPLATE_TOOL = REPO_ROOT / "platform/android/android_source_template.py"
DEVICE_ACCEPTANCE_TOOL = REPO_ROOT / "platform/android/android_device_acceptance.py"
NATIVE_CONTRACT_TOOL = REPO_ROOT / "platform/android/android_native_contract.py"
GRADLE_WRAPPER = REPO_ROOT / "platform/android/java/gradlew"
JAVA_ROOT = REPO_ROOT / "platform/android/java"
APP_ROOT = REPO_ROOT / "platform/android/java/app"
INTEGRATION_FIXTURE = REPO_ROOT / "tests/fixtures/android_foundry_java"
ANDROID_RUNTIME_GUIDE = REPO_ROOT / "platform/android/ANDROID_RUNTIME.md"
ANDROID_EXPORT_CLASS_REFERENCE = REPO_ROOT / "platform/android/doc_classes/EditorExportPlatformAndroid.xml"
ANDROID_EXPORT_PLAN = REPO_ROOT / "docs/superpowers/plans/2026-07-26-foundry-java-android-export.md"
PRE_COMMIT_CONFIG = REPO_ROOT / ".pre-commit-config.yaml"
ANDROID_BUILDS_WORKFLOW = REPO_ROOT / ".github/workflows/android_builds.yml"
EXACT_FOUNDRY_JAVA_COMMIT = "0db6970116de257fffffffe2a55e89543d4a12b5"
FOUNDRY_JAVA_GROUP = "games.cafecito.foundry"
FOUNDRY_JAVA_VERSION = "0.1.0-SNAPSHOT"

EXPORT_OPTIONS = (
    "gradle_build/foundry_java/enabled",
    "gradle_build/foundry_java/gradle_plugin_maven",
    "gradle_build/foundry_java/gradle_plugin_local",
    "gradle_build/foundry_java/maven_repositories",
    "gradle_build/foundry_java/maven_artifacts",
    "gradle_build/foundry_java/local_artifacts",
)
ORDINARY_DEPENDENCY_FRAGMENTS = (
    'implementation project(":lib")',
    "debugImplementation fileTree(dir: 'libs/debug', include: ['*.aar'])",
    "devImplementation fileTree(dir: 'libs/dev', include: ['*.aar'])",
    "releaseImplementation fileTree(dir: 'libs/release', include: ['*.aar'])",
    "implementation fileTree(dir: \"$addonsDirectory\", include: ['**/*.jar', '**/*.aar'])",
)


def read(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def load_source_template_module() -> ModuleType:
    spec = importlib.util.spec_from_file_location("android_source_template", SOURCE_TEMPLATE_TOOL)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to load {SOURCE_TEMPLATE_TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_device_acceptance_module() -> ModuleType:
    spec = importlib.util.spec_from_file_location(
        "android_device_acceptance_foundry_java",
        DEVICE_ACCEPTANCE_TOOL,
    )
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to load {DEVICE_ACCEPTANCE_TOOL}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def load_native_contract_module() -> ModuleType:
    spec = importlib.util.spec_from_file_location(
        "android_native_contract_foundry_java",
        NATIVE_CONTRACT_TOOL,
    )
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to load {NATIVE_CONTRACT_TOOL}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def run_bounded_subprocess(
    command: list[str],
    *,
    cwd: Path,
    environment: dict[str, str] | None = None,
    timeout: int = 60,
) -> subprocess.CompletedProcess[str]:
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
        raise AssertionError(
            f"Subprocess did not terminate within {timeout} seconds and its process group was stopped:\n"
            f"{stdout}\n{stderr}"
        ) from error
    return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)


def test_scratch_directory() -> Path:
    scratch = Path(os.environ.get("FOUNDRY_TEST_SCRATCH", REPO_ROOT / ".test_scratch"))
    scratch.mkdir(parents=True, exist_ok=True)
    return scratch


class FoundryJavaExportSurfaceTests(unittest.TestCase):
    def test_exporter_exposes_only_the_versioned_explicit_handoff(self) -> None:
        exporter = EXPORTER.read_text(encoding="utf-8")
        for option in EXPORT_OPTIONS:
            with self.subTest(option=option):
                self.assertIn(option, exporter)
        self.assertIn("foundry_java_registry_marker=registry-index-v2", exporter)

    def test_ordinary_gradle_dependencies_remain_owned_by_foundry(self) -> None:
        build = APP_BUILD.read_text(encoding="utf-8")
        for fragment in ORDINARY_DEPENDENCY_FRAGMENTS:
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, build)

    def test_foundry_java_gradle_wiring_is_conditional(self) -> None:
        build = APP_BUILD.read_text(encoding="utf-8")
        config = APP_CONFIG.read_text(encoding="utf-8")
        self.assertIn("getFoundryJavaRegistryMarker", config)
        self.assertIn("if (getFoundryJavaEnabled())", build)
        self.assertNotIn('implementation "games.cafecito.foundry:', build)

    def test_foundry_java_application_repositories_precede_public_repositories(self) -> None:
        build = APP_BUILD.read_text(encoding="utf-8")
        application_repositories = build.split("allprojects {", maxsplit=1)[1].split(
            "configurations {",
            maxsplit=1,
        )[0]
        configured_repository = application_repositories.index("getFoundryJavaMavenRepositories()")
        for public_repository in (
            "google()",
            "mavenCentral()",
            "gradlePluginPortal()",
            'maven { url "https://plugins.gradle.org/m2/" }',
            'maven { url "https://central.sonatype.com/repository/maven-snapshots/"}',
        ):
            with self.subTest(public_repository=public_repository):
                self.assertLess(
                    configured_repository,
                    application_repositories.index(public_repository),
                )

    def test_source_template_embeds_only_foundry_host_aars(self) -> None:
        inspector = load_source_template_module()
        self.assertEqual(
            {
                "libs/debug/foundry-debug.aar",
                "libs/dev/foundry-dev.aar",
                "libs/release/foundry-release.aar",
            },
            set(inspector.EXPECTED_AARS),
        )
        self.assertEqual(
            (
                "foundry-java",
                "foundryjava.foundryextension",
                "foundry_java/registry-index-v2.txt",
                "libfoundry_java.so",
            ),
            inspector.FORBIDDEN_BINDING_FRAGMENTS,
        )

    def test_large_command_first_evidence_artifacts_use_streamed_hashing(self) -> None:
        source = Path(__file__).read_text(encoding="utf-8")
        self.assertIn("def sha256_file(", source)
        for eager_hash in (
            f"hashlib.sha256({'apk'}.read_bytes())",
            f"hashlib.sha256({'editor'}.read_bytes())",
            f"hashlib.sha256({'source_template'}.read_bytes())",
        ):
            with self.subTest(eager_hash=eager_hash):
                self.assertNotIn(eager_hash, source)

    def test_central_directory_validation_bounds_the_local_compressed_payload(self) -> None:
        exporter = EXPORTER.read_text(encoding="utf-8")
        validator_start = exporter.index("static bool _foundry_java_validate_central_directory_entry(")
        validator_end = exporter.index(
            "static bool _foundry_java_validate_current_central_directory_entry(",
            validator_start,
        )
        validator = exporter[validator_start:validator_end]
        self.assertIn(
            "_foundry_java_checked_add(local_extra_end, central_compressed_size, local_payload_end)",
            validator,
        )
        self.assertIn("local_payload_end > p_central_directory_start", validator)


class FoundryJavaSourceTemplateResourceTests(unittest.TestCase):
    @staticmethod
    def _archive(entries: tuple[tuple[str, bytes], ...]) -> bytes:
        contents = io.BytesIO()
        with zipfile.ZipFile(contents, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, payload in entries:
                archive.writestr(name, payload)
        return contents.getvalue()

    def test_nested_archive_rejects_an_entry_over_the_decompressed_size_limit(self) -> None:
        inspector = load_source_template_module()
        limit = getattr(inspector, "MAX_NESTED_ENTRY_UNCOMPRESSED_BYTES", None)
        self.assertIsNotNone(limit)
        inspector.MAX_NESTED_ENTRY_UNCOMPRESSED_BYTES = 4
        payload = self._archive((("classes.bin", b"12345"),))
        with self.assertRaisesRegex(inspector.SourceTemplateError, "entry decompressed size limit"):
            inspector._inspect_host_archive(payload, "host.aar")

    def test_nested_archive_rejects_cumulative_decompressed_size_over_the_limit(self) -> None:
        inspector = load_source_template_module()
        limit = getattr(inspector, "MAX_NESTED_TOTAL_UNCOMPRESSED_BYTES", None)
        self.assertIsNotNone(limit)
        inspector.MAX_NESTED_TOTAL_UNCOMPRESSED_BYTES = 5
        payload = self._archive(
            (
                ("first.bin", b"123"),
                ("second.bin", b"456"),
            )
        )
        with self.assertRaisesRegex(inspector.SourceTemplateError, "cumulative decompressed size limit"):
            inspector._inspect_host_archive(payload, "host.aar")

    def test_nested_archive_rejects_more_than_the_archive_count_limit(self) -> None:
        inspector = load_source_template_module()
        limit = getattr(inspector, "MAX_NESTED_ARCHIVES", None)
        self.assertIsNotNone(limit)
        inspector.MAX_NESTED_ARCHIVES = 2
        child = self._archive((("leaf.txt", b"leaf"),))
        nested = self._archive(
            (
                ("nested-a.jar", child),
                ("nested-b.jar", child),
            )
        )
        with self.assertRaisesRegex(inspector.SourceTemplateError, "nested archive count limit"):
            inspector._inspect_host_archive(nested, "host.aar")

    def test_nested_archive_rejects_more_than_the_entry_count_limit(self) -> None:
        inspector = load_source_template_module()
        limit = getattr(inspector, "MAX_NESTED_ARCHIVE_ENTRIES", None)
        self.assertIsNotNone(limit)
        inspector.MAX_NESTED_ARCHIVE_ENTRIES = 1
        payload = self._archive(
            (
                ("first.bin", b"first"),
                ("second.bin", b"second"),
            )
        )
        with self.assertRaisesRegex(inspector.SourceTemplateError, "nested archive has too many entries"):
            inspector._inspect_host_archive(payload, "host.aar")


class FoundryJavaDocumentationTests(unittest.TestCase):
    def test_repository_urls_document_ascii_uri_syntax_and_percent_encoding(self) -> None:
        documents = {
            "runtime guide": ANDROID_RUNTIME_GUIDE.read_text(encoding="utf-8"),
            "class reference": ANDROID_EXPORT_CLASS_REFERENCE.read_text(encoding="utf-8"),
            "design": read("docs/superpowers/specs/2026-07-26-foundry-java-android-export-design.md"),
            "plan": ANDROID_EXPORT_PLAN.read_text(encoding="utf-8"),
        }
        for name, document in documents.items():
            with self.subTest(document=name):
                normalized_document = " ".join(document.split())
                self.assertIn("ASCII URI syntax", normalized_document)
                self.assertIn("Non-ASCII characters must be percent-encoded", normalized_document)
                self.assertIn("rejected before Gradle", normalized_document)
                self.assertIn("redacted", normalized_document)

    def test_runtime_guide_documents_the_complete_opt_in_contract(self) -> None:
        guide = ANDROID_RUNTIME_GUIDE.read_text(encoding="utf-8")
        required_fragments = (
            "## Optional Foundry-Java extensions",
            "gradle_build/foundry_java/enabled",
            "gradle_build/foundry_java/gradle_plugin_maven",
            "gradle_build/foundry_java/gradle_plugin_local",
            "gradle_build/foundry_java/maven_repositories",
            "HTTPS or an absolute local `file:///` URL",
            "Plain HTTP",
            "embedded credentials",
            "redacted from verbose",
            "gradle_build/foundry_java/maven_artifacts",
            "gradle_build/foundry_java/local_artifacts",
            "registry-index-v2",
            "games.cafecito.foundry.java",
            "games.cafecito.foundry:foundry-java-android:",
            "FoundryJava.foundryextension",
            "assets/foundry_java/registry-index-v2.txt",
            "arm32",
            "arm64",
            "x86_32",
            "x86_64",
            "zero descriptor",
            "Foundry-Android",
            "read-only source donor",
        )
        for fragment in required_fragments:
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, guide)
        self.assertIn("ordinary exports remain", guide.lower())
        self.assertIn("unchanged:", guide.lower())
        self.assertRegex(guide.lower(), r"not\s+a dependency")

    def test_class_reference_documents_all_six_export_options(self) -> None:
        class_reference = ANDROID_EXPORT_CLASS_REFERENCE.read_text(encoding="utf-8")
        for option in EXPORT_OPTIONS:
            with self.subTest(option=option):
                self.assertIn(f'<member name="{option}"', class_reference)
        self.assertIn("registry-index-v2", class_reference)
        self.assertIn("games.cafecito.foundry.java", class_reference)
        self.assertIn("FoundryJava.foundryextension", class_reference)
        self.assertIn("absolute local [code]file:///[/code]", class_reference)

    def test_class_reference_documents_the_local_plugin_path_contract(self) -> None:
        class_reference = ANDROID_EXPORT_CLASS_REFERENCE.read_text(encoding="utf-8")
        local_plugin = class_reference.split(
            '<member name="gradle_build/foundry_java/gradle_plugin_local"',
            maxsplit=1,
        )[1].split("</member>", maxsplit=1)[0]
        self.assertIn("regular JAR file", local_plugin)
        self.assertIn("Every existing path component", local_plugin)
        self.assertIn("must not be a symbolic link", local_plugin)

    def test_plan_documents_the_bounded_pre_commit_contract_classes(self) -> None:
        plan = ANDROID_EXPORT_PLAN.read_text(encoding="utf-8")
        self.assertIn(EXACT_FOUNDRY_JAVA_COMMIT, plan)
        for stale_reference in (
            "FoundryJavaArchiveContractTests",
            "FoundryJavaAndroidIntegrationTests.test_exact_local_inputs",
            "FoundryJavaAbiAndReleaseTests",
        ):
            with self.subTest(stale_reference=stale_reference):
                self.assertNotIn(stale_reference, plan)
        task = plan.split("### Task 7: Document and register the contract gates", maxsplit=1)[1].split(
            "### Task 8:",
            maxsplit=1,
        )[0]
        for contract_class in (
            "FoundryJavaExportSurfaceTests",
            "FoundryJavaSourceTemplateResourceTests",
            "FoundryJavaDocumentationTests",
            "FoundryJavaFinalArtifactInspectorTests",
        ):
            with self.subTest(contract_class=contract_class):
                self.assertIn(
                    f"tests.python_build.test_android_foundry_java_export.{contract_class}",
                    task,
                )
        self.assertIn(
            "Gradle-, integration-, and editor-binary-dependent contract classes remain in CI",
            task,
        )
        self.assertNotIn(
            "python3 -m unittest tests.python_build.test_android_foundry_java_export -v",
            task,
        )

    def test_pre_commit_runs_the_contract_on_every_owned_surface(self) -> None:
        config = PRE_COMMIT_CONFIG.read_text(encoding="utf-8")
        hook_marker = "\n      - id: foundry-java-android-export\n"
        self.assertEqual(1, config.count(hook_marker))
        hook = config.split(hook_marker, maxsplit=1)[1].split("\n      - id:", maxsplit=1)[0]
        for contract_class in (
            "FoundryJavaExportSurfaceTests",
            "FoundryJavaSourceTemplateResourceTests",
            "FoundryJavaDocumentationTests",
            "FoundryJavaFinalArtifactInspectorTests",
        ):
            with self.subTest(contract_class=contract_class):
                self.assertIn(
                    f"tests.python_build.test_android_foundry_java_export.{contract_class}",
                    hook,
                )
        for non_local_contract in (
            "FoundryJavaGradlePropertyTests",
            "FoundryJavaAndroidIntegrationTests",
            "FoundryJavaExporterContractTests",
        ):
            with self.subTest(non_local_contract=non_local_contract):
                self.assertNotIn(non_local_contract, hook)
        self.assertNotIn(
            "\n          - tests.python_build.test_android_foundry_java_export\n",
            hook,
        )
        required_scopes = (
            r"\.pre-commit-config\.yaml",
            r"\.github/workflows/android_builds\.yml",
            r"platform/android/ANDROID_RUNTIME\.md",
            r"platform/android/android_device_acceptance\.py",
            r"platform/android/android_source_template\.py",
            r"platform/android/doc_classes/EditorExportPlatformAndroid\.xml",
            r"platform/android/export/export_plugin\.(?:cpp|h)",
            r"platform/android/java/app/(?:build|config)\.gradle",
            r"platform/android/java/app/settings\.gradle",
            r"tests/fixtures/android_foundry_java/.*",
            r"tests/python_build/test_android_device_acceptance\.py",
            r"tests/python_build/test_android_foundry_java_export\.py",
            r"tests/python_build/test_android_gradle_(?:behavioral|runtime_contract)\.py",
        )
        for scope in required_scopes:
            with self.subTest(scope=scope):
                self.assertIn(scope, hook)

    def test_android_ci_runs_the_exact_foundry_java_matrix_without_skipping(self) -> None:
        workflow = ANDROID_BUILDS_WORKFLOW.read_text(encoding="utf-8")
        job_marker = "\n  validate-foundry-java-export:\n"
        self.assertEqual(1, workflow.count(job_marker))
        job = workflow.split(job_marker, maxsplit=1)[1].split("\n  build-android-native:", maxsplit=1)[0]
        self.assertNotIn("\n    needs:", job)
        required_fragments = (
            "repository: cafecito-games/Foundry-Java",
            f"ref: {EXACT_FOUNDRY_JAVA_COMMIT}",
            "path: foundry-java-dependency",
            "FOUNDRY_JAVA_REPO: ${{ github.workspace }}/foundry-java-dependency",
            "actions/setup-java@v5",
            "java-version: '17'",
            "tests.python_build.test_android_foundry_java_export.FoundryJavaExportSurfaceTests",
            "tests.python_build.test_android_foundry_java_export.FoundryJavaSourceTemplateResourceTests",
            "tests.python_build.test_android_foundry_java_export.FoundryJavaDocumentationTests",
            "tests.python_build.test_android_foundry_java_export.FoundryJavaFinalArtifactInspectorTests",
            "tests.python_build.test_android_foundry_java_export.FoundryJavaGradlePropertyTests",
            "tests.python_build.test_android_foundry_java_export.FoundryJavaAndroidIntegrationTests",
        )
        for fragment in required_fragments:
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, job)

    def test_integration_matrix_has_bounded_contract_cells(self) -> None:
        required_cells = (
            "test_rejects_opaque_descriptor_mutations",
            "test_rejects_opaque_graph_identity_and_provenance_mutations",
            "test_rejects_opaque_binding_payload_mutations",
            "test_rejects_empty_and_unsupported_requested_abis",
            "test_ordinary_marker_absent_debug_and_release_remain_inert",
            "test_local_debug_single_abi_matrix",
            "test_staged_maven_x86_64_debug_matches_local",
            "test_local_x86_64_minified_release_is_reproducible",
        )
        for cell in required_cells:
            with self.subTest(cell=cell):
                self.assertTrue(hasattr(FoundryJavaAndroidIntegrationTests, cell), cell)
        self.assertFalse(
            hasattr(
                FoundryJavaAndroidIntegrationTests,
                "test_local_and_staged_maven_matrix_matches_final_apks",
            )
        )

    def test_command_first_acceptance_harness_is_explicit_and_targeted(self) -> None:
        self.assertTrue(
            hasattr(
                FoundryJavaAndroidIntegrationTests,
                "test_command_first_source_template_acceptance",
            )
        )
        source = Path(__file__).read_text(encoding="utf-8")
        for fragment in (
            "FOUNDRY_JAVA_COMMAND_FIRST_ACCEPTANCE",
            "FOUNDRY_EDITOR_BINARY",
            "FOUNDRY_ANDROID_SOURCE_TEMPLATE",
            "FOUNDRY_JAVA_COMMAND_FIRST_OUTPUT",
            "--install-android-build-template",
            "foundry-java-default-debug.apk",
            "foundry-java-custom-release.apk",
            "foundry-java-command-first-evidence.json",
            'run/main_scene="res://main.tscn"',
            'project.joinpath("main.tscn")',
            'project.joinpath("main.fs")',
            "FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY",
            '"foundry_revision"',
            '"editor_sha256"',
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, source)

    def test_android_ci_runs_command_first_apks_on_api_36(self) -> None:
        workflow = ANDROID_BUILDS_WORKFLOW.read_text(encoding="utf-8")
        command_job_marker = "\n  foundry-java-command-first:\n"
        compile_job_marker = "\n  compile-instrumented-assets:\n"
        assemble_job_marker = "\n  assemble-android:\n"
        device_job_marker = "\n  device-acceptance:\n"
        self.assertEqual(1, workflow.count(command_job_marker))
        self.assertEqual(1, workflow.count(device_job_marker))
        compile_job = workflow.split(compile_job_marker, maxsplit=1)[1].split(assemble_job_marker, maxsplit=1)[0]
        command_job = workflow.split(command_job_marker, maxsplit=1)[1].split(device_job_marker, maxsplit=1)[0]
        device_job = workflow.split(device_job_marker, maxsplit=1)[1]
        self.assertIn("name: linuxbsd-editor-foundry-java-command-first", compile_job)
        self.assertIn("path: bin/foundry.linuxbsd.editor.dev.x86_64", compile_job)
        for fragment in (
            "validate-foundry-java-export",
            "assemble-android",
            "compile-instrumented-assets",
            "linuxbsd-editor-foundry-java-command-first",
            "repository: cafecito-games/Foundry-Java",
            f"ref: {EXACT_FOUNDRY_JAVA_COMMIT}",
            "FOUNDRY_JAVA_COMMAND_FIRST_ACCEPTANCE: '1'",
            "FOUNDRY_EDITOR_BINARY:",
            "FOUNDRY_ANDROID_SOURCE_TEMPLATE:",
            "FOUNDRY_JAVA_COMMAND_FIRST_OUTPUT:",
            "platforms;android-36",
            "build-tools;36.1.0",
            "mkdir -p bin",
            'install -m 0755 "${FOUNDRY_EDITOR_BINARY}" bin/foundry.linuxbsd.editor.dev.x86_64',
            "tests.python_build.test_android_foundry_java_export.FoundryJavaExporterContractTests",
            ("FoundryJavaAndroidIntegrationTests.test_command_first_source_template_acceptance"),
            "foundry-java-command-first-exports",
        ):
            with self.subTest(job="command-first", fragment=fragment):
                self.assertIn(fragment, command_job)
        for fragment in (
            "foundry-java-command-first",
            "foundry-java-command-first-exports",
            "system-images;android-36;default;x86_64",
            "android_device_acceptance.py verify-apks",
            (
                "--apk games.cafecito.foundry.game="
                ".test_scratch/foundry-java-command-first/foundry-java-default-debug.apk"
            ),
            ("--apk dev.example.foundryjava=.test_scratch/foundry-java-command-first/foundry-java-custom-release.apk"),
            "foundry-java-command-first-device-evidence",
            "--required-runtime-marker FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY",
            "if: always()",
        ):
            with self.subTest(job="device", fragment=fragment):
                self.assertIn(fragment, device_job)


class FoundryJavaFinalArtifactInspectorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tool = load_device_acceptance_module()
        self.workspace_manager = tempfile.TemporaryDirectory(
            prefix="foundry-java-apk-inspector.",
            dir=test_scratch_directory(),
        )
        self.addCleanup(self.workspace_manager.cleanup)
        self.workspace = Path(self.workspace_manager.name)

    def write_apk(self, name: str, entries: tuple[str, ...]) -> Path:
        apk = self.workspace / name
        with zipfile.ZipFile(apk, "w") as archive:
            for entry in entries:
                archive.writestr(entry, f"{entry}\n")
        return apk

    def test_enabled_inspection_requires_exact_assets_and_requested_bridges(self) -> None:
        apk = self.write_apk(
            "valid.apk",
            (
                "assets/FoundryJava.foundryextension",
                "assets/foundry_java/registry-index-v2.txt",
                "lib/arm64-v8a/libfoundry_java.so",
                "lib/arm64-v8a/libfoundry_android.so",
                "lib/arm64-v8a/libunrelated.so",
            ),
        )

        evidence = self.tool.inspect_foundry_java_apk(
            apk,
            requested_abis=("arm64-v8a",),
            enabled=True,
        )

        self.assertEqual(("arm64-v8a",), evidence["requested_abis"])
        self.assertEqual(
            ("lib/arm64-v8a/libfoundry_java.so",),
            evidence["bridge_entries"],
        )
        self.assertEqual(
            ("lib/arm64-v8a/libfoundry_android.so",),
            evidence["host_entries"],
        )
        self.assertEqual(64, len(evidence["configuration_sha256"]))
        self.assertEqual(64, len(evidence["registry_index_sha256"]))

    def test_enabled_aab_inspection_requires_exact_base_assets_and_requested_bridges(self) -> None:
        aab = self.write_apk(
            "valid.aab",
            (
                "base/assets/FoundryJava.foundryextension",
                "base/assets/foundry_java/registry-index-v2.txt",
                "base/lib/arm64-v8a/libfoundry_java.so",
                "base/lib/arm64-v8a/libfoundry_android.so",
                "base/lib/arm64-v8a/libunrelated.so",
            ),
        )

        try:
            evidence = self.tool.inspect_foundry_java_apk(
                aab,
                requested_abis=("arm64-v8a",),
                enabled=True,
            )
        except self.tool.AcceptanceError as error:
            self.fail(f"valid AAB layout was rejected: {error}")

        self.assertEqual(("arm64-v8a",), evidence["requested_abis"])
        self.assertEqual(
            ("base/lib/arm64-v8a/libfoundry_java.so",),
            evidence["bridge_entries"],
        )
        self.assertEqual(
            ("base/lib/arm64-v8a/libfoundry_android.so",),
            evidence["host_entries"],
        )

    def test_disabled_inspection_is_inert_for_ordinary_exports(self) -> None:
        missing = self.workspace / "ordinary-export-is-not-inspected.apk"
        self.assertIsNone(
            self.tool.inspect_foundry_java_apk(
                missing,
                requested_abis=(),
                enabled=False,
            )
        )

    def test_enabled_inspection_rejects_every_final_output_mismatch(self) -> None:
        valid = (
            "assets/FoundryJava.foundryextension",
            "assets/foundry_java/registry-index-v2.txt",
            "lib/arm64-v8a/libfoundry_java.so",
            "lib/arm64-v8a/libfoundry_android.so",
        )
        cases = (
            (
                "missing-config.apk",
                valid[1:],
                ("arm64-v8a",),
                "exactly one assets/FoundryJava.foundryextension",
            ),
            (
                "missing-index.apk",
                (valid[0], *valid[2:]),
                ("arm64-v8a",),
                "exactly one assets/foundry_java/registry-index-v2.txt",
            ),
            (
                "missing-bridge.apk",
                (valid[0], valid[1], valid[3]),
                ("arm64-v8a",),
                "bridge entries differ",
            ),
            (
                "unrequested-bridge.apk",
                (*valid, "lib/x86_64/libfoundry_java.so"),
                ("arm64-v8a",),
                "bridge entries differ",
            ),
            (
                "empty-abis.apk",
                valid,
                (),
                "at least one requested ABI",
            ),
            (
                "unsupported-abi.apk",
                valid,
                ("mips",),
                "unsupported requested ABI",
            ),
        )
        for name, entries, requested_abis, message in cases:
            with self.subTest(name=name):
                apk = self.write_apk(name, entries)
                with self.assertRaisesRegex(self.tool.AcceptanceError, message):
                    self.tool.inspect_foundry_java_apk(
                        apk,
                        requested_abis=requested_abis,
                        enabled=True,
                    )

    def test_host_entries_are_recorded_but_never_rejected(self) -> None:
        required = (
            "assets/FoundryJava.foundryextension",
            "assets/foundry_java/registry-index-v2.txt",
            "lib/arm64-v8a/libfoundry_java.so",
        )
        for name, host_entries in (
            ("missing-host.apk", ()),
            ("extra-host.apk", ("lib/arm64-v8a/libfoundry_android.so", "lib/x86_64/libfoundry_android.so")),
        ):
            with self.subTest(name=name):
                apk = self.write_apk(name, (*required, *host_entries))
                try:
                    evidence = self.tool.inspect_foundry_java_apk(
                        apk,
                        requested_abis=("arm64-v8a",),
                        enabled=True,
                    )
                except self.tool.AcceptanceError as error:
                    self.fail(f"host evidence rejected the final APK: {error}")
                self.assertEqual(tuple(sorted(host_entries)), evidence["host_entries"])

    def test_enabled_aab_inspection_rejects_every_final_output_mismatch(self) -> None:
        valid = (
            "base/assets/FoundryJava.foundryextension",
            "base/assets/foundry_java/registry-index-v2.txt",
            "base/lib/arm64-v8a/libfoundry_java.so",
        )
        cases = (
            ("missing-config.aab", valid[1:], "exactly one base/assets/FoundryJava.foundryextension"),
            (
                "missing-index.aab",
                (valid[0], valid[2]),
                "exactly one base/assets/foundry_java/registry-index-v2.txt",
            ),
            ("missing-bridge.aab", valid[:2], "bridge entries differ"),
            (
                "unrequested-bridge.aab",
                (*valid, "base/lib/x86_64/libfoundry_java.so"),
                "bridge entries differ",
            ),
        )
        for name, entries, message in cases:
            with self.subTest(name=name):
                aab = self.write_apk(name, entries)
                with self.assertRaisesRegex(self.tool.AcceptanceError, message):
                    self.tool.inspect_foundry_java_apk(
                        aab,
                        requested_abis=("arm64-v8a",),
                        enabled=True,
                    )

    def test_enabled_inspection_rejects_duplicate_fixed_entries(self) -> None:
        for extension, root in (("apk", ""), ("aab", "base/")):
            configuration = f"{root}assets/FoundryJava.foundryextension"
            registry_index = f"{root}assets/foundry_java/registry-index-v2.txt"
            bridge = f"{root}lib/arm64-v8a/libfoundry_java.so"
            for duplicate, message in (
                (configuration, f"exactly one {configuration}"),
                (registry_index, f"exactly one {registry_index}"),
                (bridge, "bridge entries differ"),
            ):
                with self.subTest(extension=extension, duplicate=duplicate):
                    artifact = self.workspace / f"duplicate-{duplicate.replace('/', '-')}.{extension}"
                    with self.assertWarns(UserWarning):
                        with zipfile.ZipFile(artifact, "w") as archive:
                            for entry in (configuration, registry_index, bridge, duplicate):
                                archive.writestr(entry, entry)
                    with self.assertRaisesRegex(self.tool.AcceptanceError, message):
                        self.tool.inspect_foundry_java_apk(
                            artifact,
                            requested_abis=("arm64-v8a",),
                            enabled=True,
                        )


class FoundryJavaGradlePropertyTests(unittest.TestCase):
    workspace_manager: tempfile.TemporaryDirectory[str]
    workspace: Path
    plugin: Path
    artifact: Path
    plain_artifact: Path
    maven_repository_a: Path
    maven_repository_z: Path
    network_probe: http.server.ThreadingHTTPServer
    network_probe_thread: threading.Thread
    network_requests: list[str]

    @classmethod
    def setUpClass(cls) -> None:
        cls.workspace_manager = tempfile.TemporaryDirectory(prefix="foundry-java-gradle-properties.")
        cls.addClassCleanup(cls.workspace_manager.cleanup)
        cls.workspace = Path(cls.workspace_manager.name)
        cls.plugin = cls.workspace / "plugin.jar"
        cls.artifact = cls.workspace / "module.jar"
        cls.plain_artifact = cls.workspace / "module.txt"
        cls.plain_artifact.write_text("not an archive\n", encoding="utf-8")
        cls.maven_repository_a = cls.workspace / "maven-a"
        cls.maven_repository_z = cls.workspace / "maven-z"
        cls.network_requests = []

        class NetworkProbeHandler(http.server.BaseHTTPRequestHandler):
            def do_GET(self) -> None:
                cls.network_requests.append(self.path)
                self.send_error(404)

            def do_HEAD(self) -> None:
                cls.network_requests.append(self.path)
                self.send_error(404)

            def log_message(self, format: str, *args: object) -> None:
                pass

        cls.network_probe = http.server.ThreadingHTTPServer(("127.0.0.1", 0), NetworkProbeHandler)
        cls.network_probe_thread = threading.Thread(
            target=cls.network_probe.serve_forever,
            daemon=True,
        )
        cls.network_probe_thread.start()
        cls.addClassCleanup(cls._stop_network_probe)
        cls._build_fixture_plugin()
        for archive_path in (cls.artifact,):
            with zipfile.ZipFile(archive_path, "w"):
                pass
        cls._stage_maven_artifact(
            cls.maven_repository_z,
            "test.fixture",
            "foundry-java-fixture-plugin",
            "1.0.0",
            cls.plugin,
        )
        for artifact_name in ("module-a", "module-z"):
            cls._stage_maven_artifact(
                cls.maven_repository_a,
                "test.fixture",
                artifact_name,
                "1.0.0",
                cls.artifact,
            )

    @classmethod
    def _stop_network_probe(cls) -> None:
        cls.network_probe.shutdown()
        cls.network_probe.server_close()
        cls.network_probe_thread.join(timeout=5)

    @classmethod
    def _build_fixture_plugin(cls) -> None:
        source_root = cls.workspace / "plugin-source"
        classes_root = cls.workspace / "plugin-classes"
        source_file = source_root / "test/fixture/FoundryJavaFixturePlugin.java"
        source_file.parent.mkdir(parents=True)
        classes_root.mkdir()
        source_file.write_text(
            textwrap.dedent(
                """
                package test.fixture;

                import java.io.File;
                import java.util.ArrayList;
                import java.util.Collections;
                import java.util.List;
                import java.util.Set;
                import java.util.stream.Collectors;
                import org.gradle.api.Plugin;
                import org.gradle.api.Project;
                import org.gradle.api.artifacts.Dependency;
                import org.gradle.api.artifacts.FileCollectionDependency;
                import org.gradle.api.artifacts.repositories.MavenArtifactRepository;
                import org.gradle.api.provider.SetProperty;

                public final class FoundryJavaFixturePlugin implements Plugin<Project> {
                    @Override
                    public void apply(Project project) {
                        FixtureExtension extension =
                            project.getExtensions().create("foundryJava", FixtureExtension.class);
                        project.getTasks().register("verifyFoundryJavaFixture", task -> task.doLast(ignored -> {
                            List<String> abis = new ArrayList<>(extension.getRequestedAbis().get());
                            Collections.sort(abis);

                            List<String> dependencies = new ArrayList<>();
                            for (Dependency dependency :
                                    project.getConfigurations().getByName("implementation").getDependencies()) {
                                if (dependency instanceof FileCollectionDependency) {
                                    Set<File> files = ((FileCollectionDependency) dependency).getFiles().getFiles();
                                    dependencies.addAll(files.stream()
                                        .map(File::getName)
                                        .sorted()
                                        .collect(Collectors.toList()));
                                } else if (dependency.getGroup() != null && dependency.getVersion() != null) {
                                    dependencies.add(
                                        dependency.getGroup() + ":" + dependency.getName() + ":" + dependency.getVersion()
                                    );
                                }
                            }

                            List<String> repositories = project.getRepositories()
                                .withType(MavenArtifactRepository.class)
                                .stream()
                                .map(repository -> repository.getUrl().toString())
                                .filter(url -> url.contains("foundry-java-gradle-properties"))
                                .collect(Collectors.toList());

                            System.out.println("FIXTURE_PLUGIN_APPLIED=true");
                            System.out.println("FIXTURE_REQUESTED_ABIS=" + String.join(",", abis));
                            System.out.println("FIXTURE_DEPENDENCIES=" + String.join(",", dependencies));
                            System.out.println("FIXTURE_REPOSITORIES=" + String.join(",", repositories));
                        }));
                    }

                    public abstract static class FixtureExtension {
                        public abstract SetProperty<String> getRequestedAbis();
                    }
                }
                """
            ).strip()
            + "\n",
            encoding="utf-8",
        )

        wrapper_result = run_bounded_subprocess(
            [str(GRADLE_WRAPPER), "--version"],
            cwd=APP_ROOT,
            timeout=30,
        )
        if wrapper_result.returncode != 0:
            raise AssertionError(wrapper_result.stdout + wrapper_result.stderr)
        gradle_home = next(
            (
                path
                for path in (Path.home() / ".gradle/wrapper/dists/gradle-8.11.1-bin").glob("*/gradle-8.11.1")
                if path.is_dir()
            ),
            None,
        )
        if gradle_home is None:
            raise AssertionError("Gradle 8.11.1 distribution was not installed by the wrapper")
        javac = shutil.which("javac")
        jar = shutil.which("jar")
        if javac is None or jar is None:
            raise AssertionError("A JDK with javac and jar is required for the Gradle fixture")
        compile_result = run_bounded_subprocess(
            [
                javac,
                "-classpath",
                str(gradle_home / "lib/*"),
                "-d",
                str(classes_root),
                str(source_file),
            ],
            cwd=cls.workspace,
            timeout=30,
        )
        if compile_result.returncode != 0:
            raise AssertionError(compile_result.stdout + compile_result.stderr)
        descriptor = classes_root / "META-INF/gradle-plugins/games.cafecito.foundry.java.properties"
        descriptor.parent.mkdir(parents=True)
        descriptor.write_text(
            "implementation-class=test.fixture.FoundryJavaFixturePlugin\n",
            encoding="utf-8",
        )
        jar_result = run_bounded_subprocess(
            [jar, "--create", "--file", str(cls.plugin), "-C", str(classes_root), "."],
            cwd=cls.workspace,
            timeout=30,
        )
        if jar_result.returncode != 0:
            raise AssertionError(jar_result.stdout + jar_result.stderr)

    @staticmethod
    def _stage_maven_artifact(
        repository: Path,
        group: str,
        artifact: str,
        version: str,
        source_jar: Path,
    ) -> None:
        artifact_root = repository / Path(*group.split(".")) / artifact / version
        artifact_root.mkdir(parents=True)
        shutil.copyfile(source_jar, artifact_root / f"{artifact}-{version}.jar")
        (artifact_root / f"{artifact}-{version}.pom").write_text(
            textwrap.dedent(
                f"""
                <project xmlns="http://maven.apache.org/POM/4.0.0">
                  <modelVersion>4.0.0</modelVersion>
                  <groupId>{group}</groupId>
                  <artifactId>{artifact}</artifactId>
                  <version>{version}</version>
                </project>
                """
            ).strip()
            + "\n",
            encoding="utf-8",
        )

    def run_gradle(
        self,
        properties: dict[str, str],
        tasks: tuple[str, ...] = ("help",),
        project_root: Path = APP_ROOT,
    ) -> subprocess.CompletedProcess[str]:
        command = [
            str(GRADLE_WRAPPER),
            "--no-daemon",
            "--console=plain",
            "-p",
            str(project_root),
            *tasks,
            *[f"-P{name}={value}" for name, value in sorted(properties.items())],
        ]
        environment = {**os.environ}
        java = shutil.which("java")
        if java is None:
            self.fail("A Java runtime is required for the Android Gradle property tests")
        environment.setdefault("JAVA_HOME", str(Path(java).resolve().parent.parent))
        return run_bounded_subprocess(
            command,
            cwd=project_root,
            environment=environment,
        )

    def assert_gradle_failed_with(self, properties: dict[str, str], message: str) -> None:
        result = self.run_gradle(properties)
        output = result.stdout + result.stderr
        self.assertNotEqual(0, result.returncode, output)
        self.assertIn(message, output)

    def test_unknown_registry_marker_fails_before_plugin_resolution(self) -> None:
        self.assert_gradle_failed_with(
            {"foundry_java_registry_marker": "manifest-v1"},
            "expected registry-index-v2",
        )

    def test_registry_marker_requires_an_explicit_plugin_source(self) -> None:
        self.assert_gradle_failed_with(
            {"foundry_java_registry_marker": "registry-index-v2"},
            "foundry_java_gradle_plugin",
        )

    def test_values_without_the_registry_marker_are_inert(self) -> None:
        self.network_requests.clear()
        isolated_app = self.workspace / "ordinary-app"
        shutil.copytree(
            APP_ROOT,
            isolated_app,
            ignore=shutil.ignore_patterns("build", ".gradle"),
        )
        stale_generated = isolated_app / "build/generated/foundryJava/assets"
        stale_generated.mkdir(parents=True)
        stale_generated.joinpath("FoundryJava.foundryextension").write_text(
            "stale",
            encoding="utf-8",
        )
        result = self.run_gradle(
            {
                "foundry_java_gradle_plugin": "test.fixture:unreachable-plugin:1.0.0",
                "foundry_java_gradle_plugin_kind": "maven",
                "foundry_java_maven_repositories": f"http://127.0.0.1:{self.network_probe.server_port}/repository",
                "foundry_java_maven_artifacts": "invalid",
            },
            tasks=("clean", "tasks", "--all", "dependencies"),
            project_root=isolated_app,
        )
        self.assertEqual(
            0,
            result.returncode,
            f"Ordinary Gradle configuration failed:\n{result.stdout}\n{result.stderr}",
        )
        output = result.stdout + result.stderr
        self.assertNotIn("verifyFoundryJavaFixture", output)
        self.assertNotIn("generateFoundryJavaRegistry", output)
        self.assertNotIn("foundryJavaModules", output)
        self.assertNotIn("generated/foundryJava", output)
        self.assertNotIn("FIXTURE_PLUGIN_APPLIED", output)
        self.assertNotIn("Could not resolve", output)
        self.assertEqual([], self.network_requests, output)
        self.assertFalse((isolated_app / "build/generated/foundryJava").exists())
        self.assertEqual(
            [],
            [
                path
                for path in isolated_app.rglob("*")
                if path.name
                in {
                    "FoundryJava.foundryextension",
                    "FoundryJavaRegistryBootstrap.java",
                    "registry-index-v2.txt",
                }
            ],
        )

    def test_ordinary_absent_and_explicit_empty_abi_properties_keep_default_matrix(self) -> None:
        isolated_app = self.workspace / "ordinary-abi-defaults"
        shutil.copytree(
            APP_ROOT,
            isolated_app,
            ignore=shutil.ignore_patterns("build", ".gradle"),
        )
        build_file = isolated_app / "build.gradle"
        build_file.write_text(
            build_file.read_text(encoding="utf-8")
            + textwrap.dedent(
                """

                tasks.register("printOrdinaryExportAbis") {
                    doLast {
                        println "ORDINARY_EXPORT_ABIS=" + getExportEnabledABIs().sort().join(",")
                    }
                }
                """
            ),
            encoding="utf-8",
        )
        for properties in ({}, {"export_enabled_abis": ""}):
            with self.subTest(properties=properties):
                result = self.run_gradle(
                    properties,
                    tasks=("printOrdinaryExportAbis",),
                    project_root=isolated_app,
                )
                output = result.stdout + result.stderr
                self.assertEqual(0, result.returncode, output)
                line = next(value for value in output.splitlines() if value.startswith("ORDINARY_EXPORT_ABIS="))
                self.assertEqual(
                    {"armeabi-v7a", "arm64-v8a", "x86", "x86_64"},
                    set(line.partition("=")[2].split(",")),
                )

    def enabled_local_properties(self) -> dict[str, str]:
        return {
            "foundry_java_registry_marker": "registry-index-v2",
            "foundry_java_gradle_plugin_kind": "local",
            "foundry_java_gradle_plugin": str(self.plugin),
            "foundry_java_local_artifacts": str(self.artifact),
        }

    def test_dynamic_maven_plugin_fails_before_dependency_resolution(self) -> None:
        properties = {
            "foundry_java_registry_marker": "registry-index-v2",
            "foundry_java_gradle_plugin_kind": "maven",
            "foundry_java_gradle_plugin": "games.cafecito.foundry:foundry-java-gradle-plugin:+",
            "foundry_java_maven_artifacts": "games.cafecito.foundry:foundry-java-android:1.0.0",
        }
        self.assert_gradle_failed_with(properties, "must use an exact Maven")

    def test_latest_is_dynamic_only_in_the_maven_version(self) -> None:
        properties = self.enabled_local_properties()
        properties["foundry_java_maven_artifacts"] = "latest.test.fixture:module-latest:1.0.0"
        result = self.run_gradle(properties)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

        for version in ("latest", "LaTeSt", "1.0-latest"):
            with self.subTest(version=version):
                dynamic = self.enabled_local_properties()
                dynamic["foundry_java_maven_artifacts"] = f"test.fixture:module:{version}"
                self.assert_gradle_failed_with(dynamic, "must use an exact Maven")

    def test_duplicate_application_artifacts_fail_deterministically(self) -> None:
        properties = self.enabled_local_properties()
        properties["foundry_java_maven_artifacts"] = (
            "games.cafecito.foundry:foundry-java-runtime:1.0.0|games.cafecito.foundry:foundry-java-runtime:1.0.0"
        )
        self.assert_gradle_failed_with(properties, "contains a duplicate entry")

    def test_invalid_repository_scheme_fails_before_dependency_resolution(self) -> None:
        properties = {
            "foundry_java_registry_marker": "registry-index-v2",
            "foundry_java_gradle_plugin_kind": "maven",
            "foundry_java_gradle_plugin": "games.cafecito.foundry:foundry-java-gradle-plugin:1.0.0",
            "foundry_java_maven_repositories": "ftp://example.invalid/repository",
            "foundry_java_maven_artifacts": "games.cafecito.foundry:foundry-java-android:1.0.0",
        }
        self.assert_gradle_failed_with(properties, "must use HTTPS or a local file URL")

    def test_insecure_or_secret_bearing_repositories_fail_without_echoing_the_url(self) -> None:
        base = {
            "foundry_java_registry_marker": "registry-index-v2",
            "foundry_java_gradle_plugin_kind": "local",
            "foundry_java_gradle_plugin": str(self.plugin),
            "foundry_java_local_artifacts": str(self.artifact),
        }
        for repository in (
            "http://example.invalid/insecure",
            "https://user:password@example.invalid/repository",
            "https://example.invalid/repository?token=secret-query-token",
            "https://example.invalid/repository#secret-fragment-token",
            "file:/repository",
            "file:////remote-path/repository",
            "file://remote-host/repository",
        ):
            with self.subTest(repository=repository):
                result = self.run_gradle(
                    {
                        **base,
                        "foundry_java_maven_repositories": repository,
                    }
                )
                output = result.stdout + result.stderr
                self.assertNotEqual(0, result.returncode, output)
                self.assertIn("must use HTTPS or a local file URL without credentials, query, or fragment", output)
                self.assertNotIn(repository, output)

    def test_repository_raw_uri_syntax_rejects_non_ascii_malformed_escapes_and_illegal_characters(self) -> None:
        base = self.enabled_local_properties()
        repositories = (
            "file:///tmp/repository-é",
            "file:///tmp/repository-%",
            "file:///tmp/repository-%0",
            "file:///tmp/repository-%GG",
            "https://example.invalid/repository-%GG",
            "https://invalid_host.example/repository",
            "https://-invalid.example/repository",
            "https://example.invalid:0/repository",
            "https://example.invalid:65536/repository",
            "https://[::1]:0/repository",
            "https://[::1]:65536/repository",
            "https://[::1]invalid/repository",
            "https://[::1]:8443invalid/repository",
            "https://[example.invalid]/repository",
            "https://[127.0.0.1]/repository",
            " file:///tmp/repository",
            "file:///tmp/repository ",
            "file:///tmp/repository bad",
            "file:///tmp/repository\\bad",
            'file:///tmp/repository"bad',
            "file:///tmp/repository<bad",
            "file:///tmp/repository>bad",
            "file:///tmp/repository{bad",
            "file:///tmp/repository}bad",
            "file:///tmp/repository^bad",
            "file:///tmp/repository`bad",
            "file:///tmp/repository|bad",
            "file:///tmp/repository\x7fbad",
        )
        diagnostic = "must use HTTPS or a local file URL without credentials, query, or fragment"
        for repository in repositories:
            with self.subTest(repository=repository):
                result = self.run_gradle(
                    {
                        **base,
                        "foundry_java_maven_repositories": repository,
                    }
                )
                output = result.stdout + result.stderr
                self.assertNotEqual(0, result.returncode, output)
                self.assertIn(diagnostic, output)
                self.assertNotIn(repository, output)

    def test_repository_raw_uri_syntax_accepts_percent_escapes_and_file_root(self) -> None:
        properties = {
            **self.enabled_local_properties(),
            "foundry_java_maven_repositories": "|".join(
                (
                    "file:///",
                    "file:///tmp/repository%20space",
                    "https://example.invalid/repository/%4a",
                )
            ),
        }
        result = self.run_gradle(properties)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_mixed_case_local_plugin_extension_is_accepted(self) -> None:
        mixed_case_plugin = self.workspace / "mixed-plugin.JAR"
        shutil.copyfile(self.plugin, mixed_case_plugin)
        properties = {
            **self.enabled_local_properties(),
            "foundry_java_gradle_plugin": str(mixed_case_plugin),
        }
        result = self.run_gradle(properties)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_missing_local_application_artifact_fails_deterministically(self) -> None:
        properties = self.enabled_local_properties()
        properties["foundry_java_local_artifacts"] = str(self.workspace / "missing.jar")
        self.assert_gradle_failed_with(properties, "must be a regular .jar or .aar file")

    def test_enabled_export_requires_an_application_artifact(self) -> None:
        properties = self.enabled_local_properties()
        del properties["foundry_java_local_artifacts"]
        self.assert_gradle_failed_with(properties, "requires at least one Maven or local application artifact")

    def test_every_malformed_application_input_fails_before_plugin_network_resolution(self) -> None:
        probe_url = (self.workspace / "unreachable-repository").resolve().as_uri()
        base = {
            "foundry_java_registry_marker": "registry-index-v2",
            "foundry_java_gradle_plugin_kind": "maven",
            "foundry_java_gradle_plugin": "test.fixture:unreachable-plugin:1.0.0",
            "foundry_java_maven_repositories": probe_url,
        }
        cases = (
            (
                {"foundry_java_maven_artifacts": "test.fixture:module:+"},
                "must use an exact Maven",
            ),
            (
                {"foundry_java_maven_artifacts": "test.fixture:module:1.0.0|test.fixture:module:1.0.0"},
                "contains a duplicate entry",
            ),
            (
                {"foundry_java_maven_artifacts": "test.fixture:module:1.0.0|"},
                "contains a blank entry",
            ),
            (
                {"foundry_java_maven_artifacts": " test.fixture:module:1.0.0\n"},
                "must not contain carriage returns or newlines",
            ),
            (
                {"foundry_java_local_artifacts": str(self.workspace / "missing.jar")},
                "must be a regular .jar or .aar file",
            ),
            (
                {"foundry_java_local_artifacts": str(self.plain_artifact)},
                "must be a regular .jar or .aar file",
            ),
            (
                {
                    "foundry_java_maven_repositories": f"{probe_url}|ftp://example.invalid/repository",
                    "foundry_java_maven_artifacts": "test.fixture:module:1.0.0",
                },
                "must use HTTPS or a local file URL",
            ),
            ({}, "requires at least one Maven or local application artifact"),
        )
        for additions, message in cases:
            with self.subTest(additions=additions):
                properties = {**base, **additions}
                self.network_requests.clear()
                result = self.run_gradle(properties)
                output = result.stdout + result.stderr
                self.assertNotEqual(0, result.returncode, output)
                self.assertIn(message, output)
                self.assertNotIn("Could not resolve", output)
                self.assertNotIn("Connection refused", output)
                self.assertEqual([], self.network_requests, output)

    def test_real_fixture_plugin_proves_sorted_mixed_inputs_and_requested_abis(self) -> None:
        repositories = [
            self.maven_repository_z.resolve().as_uri(),
            self.maven_repository_a.resolve().as_uri(),
        ]
        properties = {
            "foundry_java_registry_marker": "registry-index-v2",
            "foundry_java_gradle_plugin_kind": "maven",
            "foundry_java_gradle_plugin": "test.fixture:foundry-java-fixture-plugin:1.0.0",
            "foundry_java_maven_repositories": "|".join(repositories),
            "foundry_java_maven_artifacts": "test.fixture:module-z:1.0.0|test.fixture:module-a:1.0.0",
            "foundry_java_local_artifacts": str(self.artifact),
            "export_enabled_abis": "x86_64|arm64-v8a",
        }
        result = self.run_gradle(properties, tasks=("verifyFoundryJavaFixture",))
        output = result.stdout + result.stderr
        self.assertEqual(0, result.returncode, output)
        self.assertIn("FIXTURE_PLUGIN_APPLIED=true", output)
        self.assertIn("FIXTURE_REQUESTED_ABIS=arm64-v8a,x86_64", output)
        self.assertIn(
            "test.fixture:module-a:1.0.0,test.fixture:module-z:1.0.0,module.jar",
            output,
        )
        repository_line = next(line for line in output.splitlines() if line.startswith("FIXTURE_REPOSITORIES="))
        self.assertLess(repository_line.index("maven-a"), repository_line.index("maven-z"))

    def test_configured_repository_wins_application_artifact_provenance(self) -> None:
        coordinate = ("test.fixture", "repository-provenance", "1.0.0")
        public_repository = self.workspace / "maven-public"
        configured_jar = self.workspace / "configured-provenance.jar"
        public_jar = self.workspace / "public-provenance.jar"
        with zipfile.ZipFile(configured_jar, "w") as archive:
            archive.writestr("provenance.txt", "configured\n")
        with zipfile.ZipFile(public_jar, "w") as archive:
            archive.writestr("provenance.txt", "public\n")
        self._stage_maven_artifact(self.maven_repository_a, *coordinate, configured_jar)
        self._stage_maven_artifact(public_repository, *coordinate, public_jar)

        isolated_app = self.workspace / "repository-provenance-app"
        shutil.copytree(
            APP_ROOT,
            isolated_app,
            ignore=shutil.ignore_patterns("build", ".gradle"),
        )
        build_file = isolated_app / "build.gradle"
        build = build_file.read_text(encoding="utf-8")
        self.assertEqual(1, build.count("mavenCentral()"))
        build = build.replace(
            "mavenCentral()",
            f'maven {{ url "{public_repository.resolve().as_uri()}" }}',
        )
        build_file.write_text(
            build
            + textwrap.dedent(
                """

                tasks.register("verifyFoundryJavaApplicationArtifactProvenance") {
                    doLast {
                        String coordinate = getFoundryJavaMavenArtifacts().get(0)
                        File artifact = configurations.detachedConfiguration(
                            dependencies.create(coordinate)
                        ).singleFile
                        java.util.zip.ZipFile archive = new java.util.zip.ZipFile(artifact)
                        try {
                            println "FIXTURE_APPLICATION_PROVENANCE=" +
                                archive.getInputStream(archive.getEntry("provenance.txt")).getText("UTF-8").trim()
                        } finally {
                            archive.close()
                        }
                    }
                }
                """
            ),
            encoding="utf-8",
        )
        result = self.run_gradle(
            {
                "foundry_java_registry_marker": "registry-index-v2",
                "foundry_java_gradle_plugin_kind": "local",
                "foundry_java_gradle_plugin": str(self.plugin),
                "foundry_java_maven_repositories": self.maven_repository_a.resolve().as_uri(),
                "foundry_java_maven_artifacts": ":".join(coordinate),
            },
            tasks=("verifyFoundryJavaApplicationArtifactProvenance",),
            project_root=isolated_app,
        )
        output = result.stdout + result.stderr
        self.assertEqual(0, result.returncode, output)
        self.assertIn("FIXTURE_APPLICATION_PROVENANCE=configured", output)
        self.assertNotIn("FIXTURE_APPLICATION_PROVENANCE=public", output)


def find_java_17_home() -> Path:
    candidates = [
        Path(value)
        for value in (
            os.environ.get("JAVA_HOME", ""),
            "/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home",
            "/usr/local/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home",
        )
        if value
    ]
    for candidate in candidates:
        release = candidate / "release"
        if not (candidate / "bin/java").is_file() or not release.is_file():
            continue
        if 'JAVA_VERSION="17.' in release.read_text(encoding="utf-8"):
            return candidate
    raise AssertionError("Foundry-Java Android integration tests require a Java 17 JDK")


class FoundryJavaAndroidIntegrationTests(unittest.TestCase):
    workspace_manager: tempfile.TemporaryDirectory[str]
    workspace: Path
    foundry_java_repo: Path
    java_home: Path
    android_sdk: Path
    plugin_jar: Path
    runtime_jar: Path
    api_model_jar: Path
    annotations_jar: Path
    processor_jar: Path
    binding_aar: Path
    module_jar: Path
    module_generated_sources: Path
    host_fixture_root: Path
    host_java_root: Path
    host_native_root: Path
    host_aars: dict[tuple[str, str], Path]
    local_debug_evidence: dict[str, dict[str, object]]

    @classmethod
    def setUpClass(cls) -> None:
        configured_repository = os.environ.get("FOUNDRY_JAVA_REPO")
        cls.foundry_java_repo = (
            Path(configured_repository).resolve()
            if configured_repository
            else (REPO_ROOT.parent / "Foundry-Java").resolve()
        )
        if not (cls.foundry_java_repo / ".git").exists():
            raise unittest.SkipTest(
                "Foundry-Java checkout is unavailable; set FOUNDRY_JAVA_REPO to run integration tests"
            )
        head = cls._git("rev-parse", "HEAD")
        if head != EXACT_FOUNDRY_JAVA_COMMIT:
            raise AssertionError(
                "Foundry-Java integration must use exact merged commit "
                f"{EXACT_FOUNDRY_JAVA_COMMIT}; found {head} at {cls.foundry_java_repo}"
            )

        cls.java_home = find_java_17_home()
        cls.android_sdk = find_android_sdk()
        cls.workspace_manager = tempfile.TemporaryDirectory(
            prefix="foundry-java-android-integration.",
            dir=test_scratch_directory(),
        )
        cls.addClassCleanup(cls.workspace_manager.cleanup)
        cls.workspace = Path(cls.workspace_manager.name)
        cls._build_exact_foundry_java()
        cls.plugin_jar = cls._only_artifact("foundry-java-gradle-plugin/build/libs/foundry-java-gradle-plugin-*.jar")
        cls.runtime_jar = cls._only_artifact("foundry-java-runtime/build/libs/foundry-java-runtime-*.jar")
        cls.api_model_jar = cls._only_artifact("foundry-java-api-model/build/libs/foundry-java-api-model-*.jar")
        cls.annotations_jar = cls._only_artifact("foundry-java-annotations/build/libs/foundry-java-annotations-*.jar")
        cls.processor_jar = cls._only_artifact("foundry-java-processor/build/libs/foundry-java-processor-*.jar")
        cls.binding_aar = cls._only_artifact("foundry-java-android/build/outputs/aar/foundry-java-android-release.aar")
        cls.module_jar = cls._compile_module_fixture()
        cls._prepare_host_fixture()
        cls.host_aars = {}
        cls.local_debug_evidence = {}

    @classmethod
    def _git(cls, *arguments: str) -> str:
        result = run_bounded_subprocess(
            ["git", *arguments],
            cwd=cls.foundry_java_repo,
            timeout=30,
        )
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)
        return result.stdout.strip()

    @classmethod
    def _environment(cls) -> dict[str, str]:
        return {
            **os.environ,
            "ANDROID_HOME": str(cls.android_sdk),
            "ANDROID_SDK_ROOT": str(cls.android_sdk),
            "JAVA_HOME": str(cls.java_home),
        }

    @classmethod
    def _build_exact_foundry_java(cls) -> None:
        result = run_bounded_subprocess(
            [
                str(cls.foundry_java_repo / "gradlew"),
                "--no-daemon",
                ":foundry-java-annotations:jar",
                ":foundry-java-processor:jar",
                ":foundry-java-gradle-plugin:jar",
                ":foundry-java-runtime:jar",
                ":foundry-java-android:assembleRelease",
            ],
            cwd=cls.foundry_java_repo,
            environment=cls._environment(),
            timeout=900,
        )
        if result.returncode != 0:
            raise AssertionError(f"Exact Foundry-Java dependency build failed:\n{result.stdout}\n{result.stderr}")

    @classmethod
    def _only_artifact(cls, pattern: str) -> Path:
        matches = sorted(cls.foundry_java_repo.glob(pattern))
        if len(matches) != 1:
            raise AssertionError(f"Expected one exact dependency artifact for {pattern}: {matches}")
        return matches[0].resolve()

    @classmethod
    def _compile_module_fixture(cls) -> Path:
        classes = cls.workspace / "module-classes"
        classes.mkdir()
        cls.module_generated_sources = cls.workspace / "module-generated-sources"
        cls.module_generated_sources.mkdir()
        sources = sorted((INTEGRATION_FIXTURE / "module/src/main/java").rglob("*.java"))
        result = run_bounded_subprocess(
            [
                str(cls.java_home / "bin/javac"),
                "--release",
                "17",
                "-classpath",
                os.pathsep.join((str(cls.runtime_jar), str(cls.annotations_jar))),
                "-processorpath",
                os.pathsep.join((str(cls.processor_jar), str(cls.annotations_jar))),
                "-processor",
                "games.cafecito.foundry.processor.FoundryExtensionProcessor",
                "-Afoundry.module=demo",
                "-s",
                str(cls.module_generated_sources),
                "-d",
                str(classes),
                *[str(source) for source in sources],
            ],
            cwd=cls.workspace,
            environment=cls._environment(),
            timeout=60,
        )
        if result.returncode != 0:
            raise AssertionError(f"Module fixture compilation failed:\n{result.stdout}\n{result.stderr}")

        archive = cls.workspace / "foundry-java-demo-module-1.0.0.jar"
        entries = [(path.relative_to(classes).as_posix(), path) for path in classes.rglob("*") if path.is_file()]
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for name, source in sorted(entries):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                output.writestr(info, source.read_bytes())
        return archive

    @classmethod
    def _prepare_host_fixture(cls) -> None:
        cls.host_fixture_root = cls.workspace / "host-aar"
        cls.host_java_root = copy_gradle_fixture(cls.host_fixture_root)
        repository = cls.host_fixture_root / "repo"
        revision = git_object(repository, "HEAD")
        tree = git_object(repository, "HEAD^{tree}")
        cls.host_native_root = cls.host_fixture_root / "native"
        populate_native_matrix(cls.host_native_root, revision=revision, tree=tree)

    @classmethod
    def _build_host_aar(cls, build_type: str, requested_abi: str) -> Path:
        cache_key = (build_type, requested_abi)
        cached = cls.host_aars.get(cache_key)
        if cached is not None:
            return cached
        selected_abi = {
            "armeabi-v7a": "arm32",
            "arm64-v8a": "arm64",
            "x86": "x86_32",
            "x86_64": "x86_64",
        }[requested_abi]
        variant = build_type.capitalize()
        result = run_bounded_subprocess(
            [
                str(GRADLE_WRAPPER),
                "--no-daemon",
                "--console=plain",
                "-p",
                str(cls.host_java_root),
                f":lib:assembleTemplate{variant}",
                f"-PpythonExecutable={sys.executable}",
                f"-PfoundryNativeRoot={cls.host_native_root}",
                f"-PselectedAbis={selected_abi}",
            ],
            cwd=cls.host_java_root,
            environment={
                **cls._environment(),
                "FOUNDRY_TEST_SCRATCH": str(cls.workspace),
            },
            timeout=900,
        )
        if result.returncode != 0:
            raise AssertionError(
                f"Foundry host {build_type} AAR build for {requested_abi} failed:\n{result.stdout}\n{result.stderr}"
            )
        assembled = cls.host_java_root / f"lib/build/outputs/aar/foundry-{build_type}.aar"
        if not assembled.is_file():
            raise AssertionError(f"Foundry host AAR was not produced: {assembled}")
        destination = cls.workspace / "host-aars" / f"foundry-{build_type}-{requested_abi}.aar"
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(assembled, destination)
        with zipfile.ZipFile(destination) as archive:
            native_entries = sorted(name for name in archive.namelist() if name.endswith(".so"))
        expected = [
            f"jni/{requested_abi}/libc++_shared.so",
            f"jni/{requested_abi}/libfoundry_android.so",
        ]
        if native_entries != expected:
            raise AssertionError(
                f"Single-ABI host AAR mismatch for {requested_abi}: expected {expected}, found {native_entries}"
            )
        cls.host_aars[cache_key] = destination
        return destination

    def _prepare_app(
        self,
        name: str,
        *,
        build_type: str = "debug",
        requested_abi: str = "x86_64",
    ) -> Path:
        app = self.workspace / name
        shutil.copytree(
            APP_ROOT,
            app,
            ignore=shutil.ignore_patterns("build", ".gradle", ".kotlin", "libs"),
        )
        shutil.copy2(GRADLE_WRAPPER, app / "gradlew")
        shutil.copytree(JAVA_ROOT / "gradle", app / "gradle")
        host_aar = self._build_host_aar(build_type, requested_abi)
        libraries = app / f"libs/{build_type}"
        libraries.mkdir(parents=True)
        shutil.copy2(host_aar, libraries / f"foundry-{build_type}.aar")
        (app / "local.properties").write_text(
            f"sdk.dir={self.android_sdk}\n",
            encoding="utf-8",
        )
        return app

    def _run_app(
        self,
        app: Path,
        properties: dict[str, str],
        *tasks: str,
    ) -> subprocess.CompletedProcess[str]:
        command = [
            str(app / "gradlew"),
            "--no-daemon",
            "--console=plain",
            "--configuration-cache",
            *tasks,
            *[f"-P{name}={value}" for name, value in sorted(properties.items())],
        ]
        return run_bounded_subprocess(
            command,
            cwd=app,
            environment={
                **self._environment(),
                "FOUNDRY_TEST_SCRATCH": str(self.workspace),
            },
            timeout=900,
        )

    @staticmethod
    def _pom(
        group: str,
        artifact: str,
        version: str,
        *,
        packaging: str | None = None,
        dependencies: tuple[tuple[str, str, str, str], ...] = (),
    ) -> str:
        packaging_xml = f"\n  <packaging>{packaging}</packaging>" if packaging else ""
        dependencies_xml = ""
        if dependencies:
            rendered = []
            for dependency_group, dependency_artifact, dependency_version, scope in dependencies:
                rendered.append(
                    textwrap.dedent(
                        f"""
                          <dependency>
                            <groupId>{dependency_group}</groupId>
                            <artifactId>{dependency_artifact}</artifactId>
                            <version>{dependency_version}</version>
                            <scope>{scope}</scope>
                          </dependency>
                        """
                    ).rstrip()
                )
            dependencies_xml = "\n  <dependencies>\n" + "\n".join(rendered) + "\n  </dependencies>"
        return (
            textwrap.dedent(
                f"""
                <project xmlns="http://maven.apache.org/POM/4.0.0">
                  <modelVersion>4.0.0</modelVersion>
                  <groupId>{group}</groupId>
                  <artifactId>{artifact}</artifactId>
                  <version>{version}</version>{packaging_xml}{dependencies_xml}
                </project>
                """
            ).strip()
            + "\n"
        )

    @classmethod
    def _stage_maven(
        cls,
        repository: Path,
        group: str,
        artifact: str,
        version: str,
        *,
        source: Path | None = None,
        extension: str = "jar",
        packaging: str | None = None,
        dependencies: tuple[tuple[str, str, str, str], ...] = (),
    ) -> None:
        root = repository / Path(*group.split(".")) / artifact / version
        root.mkdir(parents=True, exist_ok=True)
        if source is not None:
            shutil.copy2(source, root / f"{artifact}-{version}.{extension}")
        (root / f"{artifact}-{version}.pom").write_text(
            cls._pom(
                group,
                artifact,
                version,
                packaging=packaging,
                dependencies=dependencies,
            ),
            encoding="utf-8",
        )

    def _stage_maven_graph(self) -> tuple[Path, str]:
        repository = self.workspace / "maven-repository"
        runtime = (FOUNDRY_JAVA_GROUP, "foundry-java-runtime", FOUNDRY_JAVA_VERSION, "compile")
        annotations = (
            FOUNDRY_JAVA_GROUP,
            "foundry-java-annotations",
            FOUNDRY_JAVA_VERSION,
            "runtime",
        )
        api_model = (
            FOUNDRY_JAVA_GROUP,
            "foundry-java-api-model",
            FOUNDRY_JAVA_VERSION,
            "compile",
        )
        plugin = (
            FOUNDRY_JAVA_GROUP,
            "foundry-java-gradle-plugin",
            FOUNDRY_JAVA_VERSION,
            "compile",
        )
        self._stage_maven(
            repository,
            FOUNDRY_JAVA_GROUP,
            "foundry-java-annotations",
            FOUNDRY_JAVA_VERSION,
            source=self.annotations_jar,
        )
        self._stage_maven(
            repository,
            FOUNDRY_JAVA_GROUP,
            "foundry-java-api-model",
            FOUNDRY_JAVA_VERSION,
            source=self.api_model_jar,
            dependencies=(annotations,),
        )
        self._stage_maven(
            repository,
            FOUNDRY_JAVA_GROUP,
            "foundry-java-runtime",
            FOUNDRY_JAVA_VERSION,
            source=self.runtime_jar,
            dependencies=(api_model, annotations),
        )
        self._stage_maven(
            repository,
            FOUNDRY_JAVA_GROUP,
            "foundry-java-android",
            FOUNDRY_JAVA_VERSION,
            source=self.binding_aar,
            extension="aar",
            packaging="aar",
            dependencies=(runtime,),
        )
        self._stage_maven(
            repository,
            FOUNDRY_JAVA_GROUP,
            "foundry-java-gradle-plugin",
            FOUNDRY_JAVA_VERSION,
            source=self.plugin_jar,
        )
        marker_group = "games.cafecito.foundry.java"
        marker_artifact = "games.cafecito.foundry.java.gradle.plugin"
        self._stage_maven(
            repository,
            marker_group,
            marker_artifact,
            FOUNDRY_JAVA_VERSION,
            packaging="pom",
            dependencies=(plugin,),
        )
        self._stage_maven(
            repository,
            "test.fixture",
            "foundry-java-demo-module",
            "1.0.0",
            source=self.module_jar,
            dependencies=(runtime,),
        )
        return (
            repository,
            f"{marker_group}:{marker_artifact}:{FOUNDRY_JAVA_VERSION}",
        )

    def _local_properties(
        self,
        requested_abis: tuple[str, ...],
        *,
        include_module: bool = True,
        artifacts: tuple[Path, ...] | None = None,
    ) -> dict[str, str]:
        local_artifacts = (
            list(artifacts) if artifacts is not None else [self.binding_aar, self.runtime_jar, self.annotations_jar]
        )
        if include_module and artifacts is None:
            local_artifacts.append(self.module_jar)
        return {
            "export_enabled_abis": "|".join(requested_abis),
            "foundry_java_gradle_plugin": str(self.plugin_jar),
            "foundry_java_gradle_plugin_kind": "local",
            "foundry_java_local_artifacts": "|".join(str(artifact) for artifact in local_artifacts),
            "foundry_java_registry_marker": "registry-index-v2",
        }

    def _maven_properties(
        self,
        repository: Path,
        marker: str,
        requested_abis: tuple[str, ...],
    ) -> dict[str, str]:
        return {
            "export_enabled_abis": "|".join(requested_abis),
            "foundry_java_gradle_plugin": marker,
            "foundry_java_gradle_plugin_kind": "maven",
            "foundry_java_maven_artifacts": "|".join(
                (
                    f"{FOUNDRY_JAVA_GROUP}:foundry-java-android:{FOUNDRY_JAVA_VERSION}",
                    "test.fixture:foundry-java-demo-module:1.0.0",
                )
            ),
            "foundry_java_maven_repositories": repository.resolve().as_uri(),
            "foundry_java_registry_marker": "registry-index-v2",
        }

    def _binding_configuration(self) -> bytes:
        with zipfile.ZipFile(self.binding_aar) as binding:
            classes_entries = [name for name in binding.namelist() if name == "classes.jar"]
            self.assertEqual(["classes.jar"], classes_entries)
            classes = binding.read("classes.jar")
        with zipfile.ZipFile(io.BytesIO(classes)) as classes_jar:
            configuration_entries = [name for name in classes_jar.namelist() if name == "FoundryJava.foundryextension"]
            self.assertEqual(["FoundryJava.foundryextension"], configuration_entries)
            return classes_jar.read("FoundryJava.foundryextension")

    def _module_descriptor_evidence(self) -> tuple[str, str]:
        descriptor = "META-INF/foundry-java/modules/demo.descriptor"
        with zipfile.ZipFile(self.module_jar) as module:
            descriptor_entries = [name for name in module.namelist() if name.endswith(".descriptor")]
            self.assertEqual([descriptor], descriptor_entries)
            descriptor_bytes = module.read(descriptor)
        descriptor_text = descriptor_bytes.decode("utf-8")
        self.assertIn(
            "registry=games.cafecito.foundry.generated.demo.DemoRegistry\n",
            descriptor_text,
        )
        self.assertIn("class=example.DemoExtension|DemoExtension|", descriptor_text)
        self.assertIn(
            "method=example.DemoExtension|callback_probe|callbackProbe|long(long)\n",
            descriptor_text,
        )
        return descriptor, hashlib.sha256(descriptor_bytes).hexdigest()

    @staticmethod
    def _rewrite_zip_bytes(
        source: bytes,
        *,
        replacements: dict[str, bytes] | None = None,
        renames: dict[str, str] | None = None,
        drops: tuple[str, ...] = (),
        retain: tuple[str, ...] | None = None,
    ) -> bytes:
        replacements = replacements or {}
        renames = renames or {}
        output = io.BytesIO()
        with zipfile.ZipFile(io.BytesIO(source)) as input_archive:
            with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as output_archive:
                for entry in input_archive.infolist():
                    if entry.is_dir() or entry.filename in drops:
                        continue
                    if retain is not None and entry.filename not in retain:
                        continue
                    name = renames.get(entry.filename, entry.filename)
                    contents = replacements.get(entry.filename, input_archive.read(entry))
                    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    output_archive.writestr(info, contents)
        return output.getvalue()

    def _mutate_archive(
        self,
        source: Path,
        name: str,
        *,
        replacements: dict[str, bytes] | None = None,
        renames: dict[str, str] | None = None,
        drops: tuple[str, ...] = (),
    ) -> Path:
        target = self.workspace / "opaque-mutations" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(
            self._rewrite_zip_bytes(
                source.read_bytes(),
                replacements=replacements,
                renames=renames,
                drops=drops,
            )
        )
        return target

    def _mutate_module(
        self,
        name: str,
        *,
        replacements: tuple[tuple[bytes, bytes], ...] = (),
        descriptor_path: str = "META-INF/foundry-java/modules/demo.descriptor",
    ) -> Path:
        original_path = "META-INF/foundry-java/modules/demo.descriptor"
        with zipfile.ZipFile(self.module_jar) as archive:
            descriptor = archive.read(original_path)
        for old, new in replacements:
            self.assertIn(old, descriptor)
            descriptor = descriptor.replace(old, new, 1)
        return self._mutate_archive(
            self.module_jar,
            name,
            replacements={original_path: descriptor},
            renames={original_path: descriptor_path},
        )

    def _mutate_binding(
        self,
        name: str,
        *,
        keep_configuration: bool = True,
        keep_bridge_abis: tuple[str, ...] = ("armeabi-v7a", "arm64-v8a", "x86", "x86_64"),
        configuration_only_classes: bool = False,
    ) -> Path:
        with zipfile.ZipFile(self.binding_aar) as binding:
            classes = binding.read("classes.jar")
            bridge_entries = tuple(entry for entry in binding.namelist() if entry.endswith("/libfoundry_java.so"))
        configuration = "FoundryJava.foundryextension"
        classes_drops = () if keep_configuration else (configuration,)
        classes_retain = (configuration,) if configuration_only_classes and keep_configuration else None
        mutated_classes = self._rewrite_zip_bytes(
            classes,
            drops=classes_drops,
            retain=classes_retain,
        )
        dropped_bridges = tuple(entry for entry in bridge_entries if entry.split("/")[1] not in keep_bridge_abis)
        return self._mutate_archive(
            self.binding_aar,
            name,
            replacements={"classes.jar": mutated_classes},
            drops=dropped_bridges,
        )

    def _assert_no_failed_export_outputs(self, app: Path) -> None:
        generated_assets = app / "build/generated/assets/generateStandardDebugFoundryJavaRegistry"
        generated_java = app / "build/generated/java/generateStandardDebugFoundryJavaRegistry"
        generated_manifest = app / "build/generated/manifests/generateStandardDebugFoundryJavaRegistry"
        for root in (generated_assets, generated_java, generated_manifest):
            self.assertFalse(any(path.is_file() for path in root.rglob("*")), root)
        for output in (
            generated_assets / "FoundryJava.foundryextension",
            generated_assets / "foundry_java/registry-index-v2.txt",
            generated_java / "games/cafecito/foundry/generated/FoundryGeneratedBootstrap.java",
            generated_java / "games/cafecito/foundry/generated/FoundryGeneratedStartupProvider.java",
            app / "build/outputs/apk/standard/debug/android_debug.apk",
        ):
            self.assertFalse(output.exists(), output)

    def _assert_plugin_rejects(
        self,
        case: str,
        artifacts: tuple[Path, ...],
        expected_fragments: tuple[str, ...],
        *,
        requested_abis: tuple[str, ...] = ("x86_64",),
    ) -> None:
        supported_abis = ("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
        host_abi = requested_abis[0] if requested_abis and requested_abis[0] in supported_abis else "x86_64"
        app = self._prepare_app(f"reject-{case}", requested_abi=host_abi)
        result = self._run_app(
            app,
            self._local_properties(
                requested_abis,
                artifacts=artifacts,
            ),
            "generateStandardDebugFoundryJavaRegistry",
        )
        output = result.stdout + result.stderr
        self.assertNotEqual(0, result.returncode, output)
        for fragment in expected_fragments:
            with self.subTest(case=case, fragment=fragment):
                self.assertIn(fragment, output)
        self._assert_no_failed_export_outputs(app)

    def _assert_final_provider(
        self,
        app: Path,
        *,
        variant: str,
        expected_application_id: str,
    ) -> None:
        merged_root = app / "build/intermediates/merged_manifest" / variant
        manifests = sorted(merged_root.rglob("AndroidManifest.xml"))
        self.assertEqual(1, len(manifests), manifests)
        root = ET.parse(manifests[0]).getroot()
        android = "{http://schemas.android.com/apk/res/android}"
        provider_class = "games.cafecito.foundry.generated.FoundryGeneratedStartupProvider"
        providers = [
            provider for provider in root.findall(".//provider") if provider.get(f"{android}name") == provider_class
        ]
        self.assertEqual(1, len(providers))
        provider = providers[0]
        self.assertEqual(
            f"{expected_application_id}.foundry-java-startup",
            provider.get(f"{android}authorities"),
        )
        self.assertEqual("false", provider.get(f"{android}exported"))
        self.assertEqual("100", provider.get(f"{android}initOrder"))
        self.assertIsNone(provider.get(f"{android}process"))

    def _assert_outputs(
        self,
        app: Path,
        first: subprocess.CompletedProcess[str],
        second: subprocess.CompletedProcess[str] | None = None,
        *,
        requested_abis: tuple[str, ...],
        expected_application_id: str,
        build_type: str = "debug",
        preserve_native_debug_symbols: bool = False,
    ) -> dict[str, object]:
        first_output = first.stdout + first.stderr
        self.assertEqual(0, first.returncode, first_output)
        if second is not None:
            second_output = second.stdout + second.stderr
            self.assertEqual(0, second.returncode, second_output)
            self.assertIn("Reusing configuration cache.", second_output)

        variant = f"Standard{build_type.capitalize()}"
        variant_directory = f"standard{build_type.capitalize()}"
        generated = app / f"build/generated/assets/generate{variant}FoundryJavaRegistry"
        index = generated / "foundry_java/registry-index-v2.txt"
        configuration = generated / "FoundryJava.foundryextension"
        generated_java = app / f"build/generated/java/generate{variant}FoundryJavaRegistry"
        bootstrap = generated_java / "games/cafecito/foundry/generated/FoundryGeneratedBootstrap.java"
        startup_provider = generated_java / "games/cafecito/foundry/generated/FoundryGeneratedStartupProvider.java"
        registry_class = "games.cafecito.foundry.generated.demo.DemoRegistry"
        self.assertIn(f"module=demo|{registry_class}", index.read_text(encoding="utf-8"))
        self.assertIn(f"{registry_class}.PROVIDER", bootstrap.read_text(encoding="utf-8"))
        self.assertIn(
            "return FoundryGeneratedBootstrap.bootstrap();",
            startup_provider.read_text(encoding="utf-8"),
        )
        binding_configuration = self._binding_configuration()
        self.assertEqual(binding_configuration, configuration.read_bytes())
        descriptor_name, descriptor_sha256 = self._module_descriptor_evidence()

        apk = app / f"build/outputs/apk/standard/{build_type}/android_{build_type}.apk"
        evidence = load_device_acceptance_module().inspect_foundry_java_apk(
            apk,
            requested_abis=requested_abis,
            enabled=True,
        )
        sorted_abis = tuple(sorted(requested_abis))
        expected_bridges = tuple(f"lib/{abi}/libfoundry_java.so" for abi in sorted_abis)
        self.assertEqual(sorted_abis, evidence["requested_abis"])
        self.assertEqual(expected_bridges, evidence["bridge_entries"])
        expected_hosts = tuple(f"lib/{abi}/libfoundry_android.so" for abi in sorted_abis)
        expected_unrelated = tuple(f"lib/{abi}/libc++_shared.so" for abi in sorted_abis)
        self.assertEqual(expected_hosts, evidence["host_entries"])
        native_contract = load_native_contract_module()
        host_surfaces: set[tuple[str, ...]] = set()
        unrelated_surfaces: set[tuple[str, ...]] = set()
        debug_symbols_preserved = True
        host_aar = app / f"libs/{build_type}/foundry-{build_type}.aar"
        with zipfile.ZipFile(host_aar) as input_archive, zipfile.ZipFile(apk) as output_archive:
            output_names = set(output_archive.namelist())
            self.assertEqual(
                set(expected_unrelated),
                {name for name in output_names if name.endswith("/libc++_shared.so")},
            )
            for abi in sorted_abis:
                input_host = input_archive.read(f"jni/{abi}/libfoundry_android.so")
                output_host = output_archive.read(f"lib/{abi}/libfoundry_android.so")
                input_unrelated = input_archive.read(f"jni/{abi}/libc++_shared.so")
                output_unrelated = output_archive.read(f"lib/{abi}/libc++_shared.so")
                debug_symbols_preserved &= input_host == output_host and input_unrelated == output_unrelated
                host_surfaces.add(
                    native_contract.read_elf(
                        output_host,
                        f"{build_type}/{abi}/libfoundry_android.so",
                    ).exported_symbols
                )
                unrelated_surfaces.add(
                    native_contract.read_elf(
                        output_unrelated,
                        f"{build_type}/{abi}/libc++_shared.so",
                    ).exported_symbols
                )
        expected_host_symbols = tuple(sorted(FOUNDRY_SYMBOLS + EXTERNAL_JNI_SYMBOLS))
        self.assertEqual({expected_host_symbols}, host_surfaces)
        self.assertEqual({()}, unrelated_surfaces)
        if preserve_native_debug_symbols:
            self.assertTrue(debug_symbols_preserved)
        self.assertEqual(
            hashlib.sha256(binding_configuration).hexdigest(),
            evidence["configuration_sha256"],
        )
        self.assertEqual(
            hashlib.sha256(index.read_bytes()).hexdigest(),
            evidence["registry_index_sha256"],
        )

        metadata = json.loads(
            (app / f"build/outputs/apk/standard/{build_type}/output-metadata.json").read_text(encoding="utf-8")
        )
        self.assertEqual(expected_application_id, metadata["applicationId"])
        self._assert_final_provider(
            app,
            variant=variant_directory,
            expected_application_id=expected_application_id,
        )

        if build_type == "release":
            mapping = app / "build/outputs/mapping/standardRelease/mapping.txt"
            mapping_text = mapping.read_text(encoding="utf-8")
            for retained_class in (
                "games.cafecito.foundry.generated.FoundryGeneratedStartupProvider",
                "games.cafecito.foundry.generated.FoundryGeneratedBootstrap",
                "games.cafecito.foundry.generated.demo.DemoRegistry",
                "example.DemoExtension_FoundryTrampoline",
            ):
                self.assertIn(
                    f"{retained_class} -> {retained_class}:",
                    mapping_text,
                )

        return {
            "requested_abis": evidence["requested_abis"],
            "bridge_entries": evidence["bridge_entries"],
            "host_entries": evidence["host_entries"],
            "unrelated_native_entries": expected_unrelated,
            "host_exported_symbols": next(iter(host_surfaces)),
            "unrelated_exported_symbols": next(iter(unrelated_surfaces)),
            "native_debug_symbols_preserved": debug_symbols_preserved,
            "configuration_sha256": evidence["configuration_sha256"],
            "registry_index_sha256": evidence["registry_index_sha256"],
            "descriptor_name": descriptor_name,
            "descriptor_sha256": descriptor_sha256,
            "application_id": expected_application_id,
        }

    def _local_debug_evidence(self, requested_abi: str) -> dict[str, object]:
        cached = self.local_debug_evidence.get(requested_abi)
        if cached is not None:
            return cached
        requested_abis = (requested_abi,)
        app = self._prepare_app(
            f"local-debug-{requested_abi}",
            requested_abi=requested_abi,
        )
        properties = self._local_properties(requested_abis)
        properties["doNotStrip"] = "true"
        result = self._run_app(
            app,
            properties,
            "assembleStandardDebug",
        )
        evidence = self._assert_outputs(
            app,
            result,
            requested_abis=requested_abis,
            expected_application_id="games.cafecito.foundry.game",
            preserve_native_debug_symbols=True,
        )
        self.local_debug_evidence[requested_abi] = evidence
        return evidence

    @staticmethod
    def _required_command_first_path(name: str) -> Path:
        value = os.environ.get(name)
        if value is None or not value.strip():
            raise AssertionError(f"{name} must name an explicit path")
        return Path(value).expanduser().absolute()

    @classmethod
    def _android_sdk_tool(cls, name: str) -> Path:
        candidates = (
            cls.android_sdk / "cmdline-tools/latest/bin" / name,
            *sorted(
                cls.android_sdk.glob(f"build-tools/*/{name}"),
                reverse=True,
            ),
        )
        for candidate in candidates:
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return candidate
        raise AssertionError(f"Android SDK tool is unavailable: {name}")

    def _write_command_first_project(
        self,
        project: Path,
        *,
        application_id: str,
        source_template: Path,
        keystore: Path,
    ) -> None:
        project.mkdir(parents=True)
        project.joinpath("project.foundry").write_text(
            textwrap.dedent(
                """\
                config_version=5

                [application]
                config/name="Foundry Java Command First"
                run/main_scene="res://main.tscn"

                [rendering]
                renderer/rendering_method="gl_compatibility"
                renderer/rendering_method.mobile="gl_compatibility"
                textures/vram_compression/import_etc2_astc=true
                """
            ),
            encoding="utf-8",
        )
        project.joinpath("main.tscn").write_text(
            textwrap.dedent(
                """\
                [gd_scene load_steps=2 format=3]

                [ext_resource type="Script" path="res://main.fs" id="1_acceptance"]

                [node name="Main" type="Node"]
                script = ExtResource("1_acceptance")
                """
            ),
            encoding="utf-8",
        )
        project.joinpath("main.fs").write_text(
            'extends Node\n\nfunc _ready() -> void:\n\tprint("FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY")\n',
            encoding="utf-8",
        )
        FoundryJavaExporterContractTests._write_export_preset(
            project,
            plugin_local=self.plugin_jar,
            local_artifacts=(
                self.binding_aar,
                self.runtime_jar,
                self.annotations_jar,
                self.module_jar,
            ),
            use_gradle=True,
            extra_options=(
                f'gradle_build/android_source_template="{source_template}"',
                "gradle_build/export_format=0",
                'gradle_build/target_sdk="36"',
                f'package/unique_name="{application_id}"',
                "package/signed=true",
                f'keystore/debug="{keystore}"',
                'keystore/debug_user="foundry-java-acceptance"',
                'keystore/debug_password="foundry-java-acceptance"',
                f'keystore/release="{keystore}"',
                'keystore/release_user="foundry-java-acceptance"',
                'keystore/release_password="foundry-java-acceptance"',
                "architectures/armeabi-v7a=false",
                "architectures/arm64-v8a=false",
                "architectures/x86=false",
                "architectures/x86_64=true",
            ),
        )

    def _run_command_first_export(
        self,
        editor: Path,
        project: Path,
        output: Path,
        *,
        mode: str,
    ) -> subprocess.CompletedProcess[str]:
        return run_bounded_subprocess(
            [
                str(editor),
                "--headless",
                "project",
                "export",
                "--project",
                str(project),
                "--preset",
                "Android",
                "--output",
                str(output),
                "--mode",
                mode,
                "--install-android-build-template",
            ],
            cwd=project,
            environment={
                **self._environment(),
                "FOUNDRY_TEST_SCRATCH": str(self.workspace),
            },
            timeout=900,
        )

    def _assert_command_first_apk(
        self,
        project: Path,
        apk: Path,
        *,
        application_id: str,
        build_type: str,
        expected_configuration: bytes,
        expected_index: bytes,
    ) -> dict[str, object]:
        inspector = load_device_acceptance_module()
        evidence = inspector.inspect_foundry_java_apk(
            apk,
            requested_abis=("x86_64",),
            enabled=True,
        )
        expected_configuration_sha256 = hashlib.sha256(expected_configuration).hexdigest()
        expected_index_sha256 = hashlib.sha256(expected_index).hexdigest()
        self.assertEqual(
            ("lib/x86_64/libfoundry_java.so",),
            evidence["bridge_entries"],
        )
        self.assertEqual(
            expected_configuration_sha256,
            evidence["configuration_sha256"],
        )
        self.assertEqual(
            expected_index_sha256,
            evidence["registry_index_sha256"],
        )
        with zipfile.ZipFile(apk) as archive:
            self.assertEqual(
                expected_configuration,
                archive.read("assets/FoundryJava.foundryextension"),
            )
            self.assertEqual(
                expected_index,
                archive.read("assets/foundry_java/registry-index-v2.txt"),
            )

        apkanalyzer = self._android_sdk_tool("apkanalyzer")
        application_id_result = run_bounded_subprocess(
            [str(apkanalyzer), "manifest", "application-id", str(apk)],
            cwd=project,
            environment=self._environment(),
            timeout=60,
        )
        self.assertEqual(
            0,
            application_id_result.returncode,
            application_id_result.stdout + application_id_result.stderr,
        )
        self.assertEqual(application_id, application_id_result.stdout.strip())
        manifest = run_bounded_subprocess(
            [str(apkanalyzer), "manifest", "print", str(apk)],
            cwd=project,
            environment=self._environment(),
            timeout=60,
        )
        manifest_output = manifest.stdout + manifest.stderr
        self.assertEqual(0, manifest.returncode, manifest_output)
        self.assertIn(
            "games.cafecito.foundry.generated.FoundryGeneratedStartupProvider",
            manifest_output,
        )
        authority = f"{application_id}.foundry-java-startup"
        self.assertIn(authority, manifest_output)

        signing = run_bounded_subprocess(
            [str(self._android_sdk_tool("apksigner")), "verify", "--verbose", str(apk)],
            cwd=project,
            environment=self._environment(),
            timeout=60,
        )
        self.assertEqual(
            0,
            signing.returncode,
            signing.stdout + signing.stderr,
        )

        retained_classes: tuple[str, ...] = ()
        if build_type == "release":
            mapping = project / "android/build/build/outputs/mapping/standardRelease/mapping.txt"
            mapping_text = mapping.read_text(encoding="utf-8")
            retained_classes = (
                "games.cafecito.foundry.generated.FoundryGeneratedStartupProvider",
                "games.cafecito.foundry.generated.FoundryGeneratedBootstrap",
                "games.cafecito.foundry.generated.demo.DemoRegistry",
                "example.DemoExtension_FoundryTrampoline",
            )
            for retained_class in retained_classes:
                self.assertIn(
                    f"{retained_class} -> {retained_class}:",
                    mapping_text,
                )

        return {
            "apk": apk.name,
            "apk_sha256": sha256_file(apk),
            "application_id": application_id,
            "build_type": build_type,
            "requested_abis": list(evidence["requested_abis"]),
            "bridge_entries": list(evidence["bridge_entries"]),
            "host_entries": list(evidence["host_entries"]),
            "configuration_sha256": evidence["configuration_sha256"],
            "registry_index_sha256": evidence["registry_index_sha256"],
            "provider_authority": authority,
            "retained_classes": list(retained_classes),
            "signed": True,
        }

    def _build_twice(
        self,
        app: Path,
        properties: dict[str, str],
        *,
        build_type: str = "debug",
    ) -> tuple[subprocess.CompletedProcess[str], subprocess.CompletedProcess[str]]:
        variant = f"Standard{build_type.capitalize()}"
        first = self._run_app(app, properties, f"assemble{variant}")
        if first.returncode != 0:
            self.fail(first.stdout + first.stderr)
        generated_roots = {
            "assets": app / f"build/generated/assets/generate{variant}FoundryJavaRegistry",
            "java": app / f"build/generated/java/generate{variant}FoundryJavaRegistry",
            "manifests": app / f"build/generated/manifests/generate{variant}FoundryJavaRegistry",
        }

        def snapshot() -> dict[str, bytes]:
            return {
                f"{kind}/{path.relative_to(root).as_posix()}": path.read_bytes()
                for kind, root in generated_roots.items()
                for path in root.rglob("*")
                if path.is_file()
            }

        before = snapshot()
        for root in generated_roots.values():
            shutil.rmtree(root)
            root.mkdir(parents=True)
        second = self._run_app(app, properties, f"assemble{variant}")
        second_output = second.stdout + second.stderr
        generator_task = f"> Task :generate{variant}FoundryJavaRegistry"
        self.assertIn("Reusing configuration cache.", second_output)
        self.assertIn(generator_task, second_output.splitlines())
        after = snapshot()
        self.assertEqual(before, after)
        return first, second

    def test_module_fixture_is_processor_generated(self) -> None:
        descriptor_path = "META-INF/foundry-java/modules/demo.descriptor"
        keep_rules_path = "META-INF/proguard/foundry-java-demo.pro"
        registry_class = "games/cafecito/foundry/generated/demo/DemoRegistry.class"
        trampoline_class = "example/DemoExtension_FoundryTrampoline.class"
        with zipfile.ZipFile(self.module_jar) as module:
            names = set(module.namelist())
            self.assertTrue(
                {descriptor_path, keep_rules_path, registry_class, trampoline_class}.issubset(names),
                names,
            )
            descriptor = module.read(descriptor_path).decode("utf-8")
            keep_rules = module.read(keep_rules_path).decode("utf-8")

        self.assertIn(
            "registry=games.cafecito.foundry.generated.demo.DemoRegistry\n",
            descriptor,
        )
        self.assertIn("class=example.DemoExtension|DemoExtension|", descriptor)
        self.assertIn(
            "method=example.DemoExtension|callback_probe|callbackProbe|long(long)\n",
            descriptor,
        )
        self.assertIn("-keep class games.cafecito.foundry.generated.demo.DemoRegistry", keep_rules)
        self.assertIn("-keep class example.DemoExtension_FoundryTrampoline", keep_rules)

        generated_sources = getattr(self, "module_generated_sources", None)
        self.assertIsNotNone(generated_sources)
        trampoline = generated_sources / "example/DemoExtension_FoundryTrampoline.java"
        registry = generated_sources / "games/cafecito/foundry/generated/demo/DemoRegistry.java"
        self.assertTrue(trampoline.is_file(), trampoline)
        self.assertTrue(registry.is_file(), registry)
        self.assertIn('case "callback_probe"', trampoline.read_text(encoding="utf-8"))
        self.assertIn(
            "example.DemoExtension_FoundryTrampoline.invoke(",
            registry.read_text(encoding="utf-8"),
        )

    def test_command_first_source_template_acceptance(self) -> None:
        if os.environ.get("FOUNDRY_JAVA_COMMAND_FIRST_ACCEPTANCE") != "1":
            raise unittest.SkipTest(
                "Set FOUNDRY_JAVA_COMMAND_FIRST_ACCEPTANCE=1 with explicit editor, source-template, and output paths"
            )
        editor = self._required_command_first_path("FOUNDRY_EDITOR_BINARY")
        source_template = self._required_command_first_path("FOUNDRY_ANDROID_SOURCE_TEMPLATE")
        output = self._required_command_first_path("FOUNDRY_JAVA_COMMAND_FIRST_OUTPUT")
        self.assertTrue(editor.is_file() and os.access(editor, os.X_OK), editor)
        self.assertTrue(source_template.is_file() and not source_template.is_symlink(), source_template)
        output.mkdir(parents=True, exist_ok=True)
        self.assertTrue(output.is_dir() and not output.is_symlink(), output)
        evidence_path = output / "foundry-java-command-first-evidence.json"
        evidence_path.unlink(missing_ok=True)

        source_entries = load_source_template_module().inspect_source_template(source_template)
        with zipfile.ZipFile(source_template) as source_archive:
            for host_aar in sorted(load_source_template_module().EXPECTED_AARS):
                with self.subTest(host_aar=host_aar):
                    with zipfile.ZipFile(io.BytesIO(source_archive.read(host_aar))) as archive:
                        nested_entries = tuple(archive.namelist())
                    self.assertFalse(
                        any(
                            "foundry-java" in entry.lower()
                            or "foundryjava" in entry.lower()
                            or "libfoundry_java.so" in entry
                            for entry in nested_entries
                        ),
                        host_aar,
                    )

        keystore = self.workspace / "foundry-java-command-first.jks"
        keytool = self.java_home / "bin/keytool"
        keytool_result = run_bounded_subprocess(
            [
                str(keytool),
                "-genkeypair",
                "-noprompt",
                "-keystore",
                str(keystore),
                "-storepass",
                "foundry-java-acceptance",
                "-keypass",
                "foundry-java-acceptance",
                "-alias",
                "foundry-java-acceptance",
                "-dname",
                "CN=Foundry Java Acceptance,O=Cafecito Games,C=US",
                "-keyalg",
                "RSA",
                "-keysize",
                "2048",
                "-validity",
                "365",
            ],
            cwd=self.workspace,
            environment=self._environment(),
            timeout=60,
        )
        self.assertEqual(
            0,
            keytool_result.returncode,
            keytool_result.stdout + keytool_result.stderr,
        )
        self.assertTrue(keystore.is_file())

        expected_configuration = self._binding_configuration()
        expected_index = (
            "format=2\n"
            "api_sha256=85e91174c1a8a48629223d6459bb2ef595ad1da405b2ce88435c24fe221aec51\n"
            "generator_version=1\n"
            "runtime_contract_version=1\n"
            "bridge_contract_version=1\n"
            "module=demo|games.cafecito.foundry.generated.demo.DemoRegistry\n"
        ).encode()
        scenarios = (
            (
                "default-debug",
                "games.cafecito.foundry.game",
                "debug",
                output / "foundry-java-default-debug.apk",
            ),
            (
                "custom-release",
                "dev.example.foundryjava",
                "release",
                output / "foundry-java-custom-release.apk",
            ),
        )
        exports: dict[str, dict[str, object]] = {}
        for name, application_id, mode, apk in scenarios:
            with self.subTest(name=name):
                apk.unlink(missing_ok=True)
                project = self.workspace / f"command-first-{name}"
                self._write_command_first_project(
                    project,
                    application_id=application_id,
                    source_template=source_template,
                    keystore=keystore,
                )
                result = self._run_command_first_export(
                    editor,
                    project,
                    apk,
                    mode=mode,
                )
                command_output = result.stdout + result.stderr
                self.assertEqual(0, result.returncode, command_output)
                self.assertTrue(apk.is_file(), apk)
                exports[name] = self._assert_command_first_apk(
                    project,
                    apk,
                    application_id=application_id,
                    build_type=mode,
                    expected_configuration=expected_configuration,
                    expected_index=expected_index,
                )

        revision_result = run_bounded_subprocess(
            ["git", "rev-parse", "HEAD"],
            cwd=REPO_ROOT,
            timeout=30,
        )
        revision_output = revision_result.stdout + revision_result.stderr
        self.assertEqual(0, revision_result.returncode, revision_output)
        foundry_revision = revision_result.stdout.strip()
        self.assertRegex(foundry_revision, r"^[0-9a-f]{40}$")
        evidence = {
            "schema_version": 1,
            "foundry_java_commit": EXACT_FOUNDRY_JAVA_COMMIT,
            "foundry_revision": foundry_revision,
            "editor": str(editor),
            "editor_sha256": sha256_file(editor),
            "source_template": {
                "name": source_template.name,
                "sha256": sha256_file(source_template),
                "entries": list(source_entries),
            },
            "expected_configuration_sha256": hashlib.sha256(expected_configuration).hexdigest(),
            "expected_registry_index_sha256": hashlib.sha256(expected_index).hexdigest(),
            "exports": exports,
        }
        evidence_path.write_text(
            json.dumps(evidence, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        self.assertEqual(evidence, json.loads(evidence_path.read_text(encoding="utf-8")))

    def test_rejects_opaque_descriptor_mutations(self) -> None:
        descriptor_path = "META-INF/foundry-java/modules/demo.descriptor"
        cases = (
            (
                "descriptor-format.jar",
                ((b"format=2", b"format=1"),),
                descriptor_path,
                ("format=1", "expected 2"),
            ),
            (
                "descriptor-path.jar",
                (),
                "META-INF/foundry-java/modules/wrong.descriptor",
                (
                    "META-INF/foundry-java/modules/wrong.descriptor",
                    "descriptor path must be META-INF/foundry-java/modules/demo.descriptor",
                ),
            ),
            (
                "descriptor-header.jar",
                ((b"generator_version=1\n", b""),),
                descriptor_path,
                ("expected field generator_version",),
            ),
            (
                "descriptor-name.jar",
                ((b"module=demo", b"module=Demo_Name"),),
                descriptor_path,
                ("module=Demo_Name",),
            ),
        )
        for name, replacements, path, diagnostics in cases:
            with self.subTest(name=name):
                mutant = self._mutate_module(
                    name,
                    replacements=replacements,
                    descriptor_path=path,
                )
                self._assert_plugin_rejects(
                    name.removesuffix(".jar"),
                    (self.binding_aar, self.runtime_jar, mutant),
                    (str(mutant), path, *diagnostics),
                )

    def test_rejects_opaque_graph_identity_and_provenance_mutations(self) -> None:
        descriptor_root = "META-INF/foundry-java/modules"
        identity_cases = (
            (
                "duplicate-module.jar",
                (
                    (
                        b"registry=games.cafecito.foundry.generated.demo.DemoRegistry",
                        b"registry=example.DuplicateModule",
                    ),
                ),
                f"{descriptor_root}/demo.descriptor",
                "duplicate module=demo",
            ),
            (
                "duplicate-registry.jar",
                ((b"module=demo", b"module=duplicate-registry"),),
                f"{descriptor_root}/duplicate-registry.descriptor",
                "duplicate registry=games.cafecito.foundry.generated.demo.DemoRegistry",
            ),
        )
        for name, replacements, path, diagnostic in identity_cases:
            with self.subTest(name=name):
                mutant = self._mutate_module(
                    name,
                    replacements=replacements,
                    descriptor_path=path,
                )
                self._assert_plugin_rejects(
                    name.removesuffix(".jar"),
                    (self.binding_aar, self.runtime_jar, self.module_jar, mutant),
                    (
                        diagnostic,
                        str(self.module_jar),
                        str(mutant),
                        f"{descriptor_root}/demo.descriptor",
                        path,
                    ),
                )

        provenance_cases = (
            (
                "api_sha256",
                b"85e91174c1a8a48629223d6459bb2ef595ad1da405b2ce88435c24fe221aec51",
                b"15e91174c1a8a48629223d6459bb2ef595ad1da405b2ce88435c24fe221aec51",
            ),
            ("generator_version", b"generator_version=1", b"generator_version=2"),
            (
                "runtime_contract_version",
                b"runtime_contract_version=1",
                b"runtime_contract_version=2",
            ),
            (
                "bridge_contract_version",
                b"bridge_contract_version=1",
                b"bridge_contract_version=2",
            ),
        )
        for field, old, new in provenance_cases:
            with self.subTest(field=field):
                module_name = f"mixed-{field.replace('_', '-')}"
                registry_name = "example.Mixed" + "".join(word.capitalize() for word in field.split("_"))
                replacements = (
                    (b"module=demo", f"module={module_name}".encode()),
                    (
                        b"registry=games.cafecito.foundry.generated.demo.DemoRegistry",
                        f"registry={registry_name}".encode(),
                    ),
                    (old, new),
                )
                path = f"{descriptor_root}/{module_name}.descriptor"
                mutant = self._mutate_module(
                    f"mixed-{field}.jar",
                    replacements=replacements,
                    descriptor_path=path,
                )
                old_value = old.decode().split("=", maxsplit=1)[-1]
                new_value = new.decode().split("=", maxsplit=1)[-1]
                self._assert_plugin_rejects(
                    f"mixed-{field}",
                    (self.binding_aar, self.runtime_jar, self.module_jar, mutant),
                    (
                        f"mixed {field}",
                        str(self.module_jar),
                        str(mutant),
                        f"{field}={old_value}",
                        f"{field}={new_value}",
                        path,
                    ),
                )

    def test_rejects_opaque_binding_payload_mutations(self) -> None:
        config_only = self._mutate_binding(
            "configuration-only.aar",
            keep_bridge_abis=(),
            configuration_only_classes=True,
        )
        bridge_only = self._mutate_binding(
            "bridge-only.aar",
            keep_configuration=False,
            keep_bridge_abis=("x86_64",),
            configuration_only_classes=True,
        )
        duplicate = self._mutate_binding(
            "duplicate-binding.aar",
            keep_bridge_abis=("x86_64",),
            configuration_only_classes=True,
        )
        missing_requested_abi = self._mutate_binding(
            "arm64-binding.aar",
            keep_bridge_abis=("arm64-v8a",),
        )
        cases = (
            (
                "missing-bridge",
                (config_only, self.runtime_jar, self.module_jar),
                (
                    "bridge payload count=0; expected 1",
                    str(config_only),
                    "bridge_payload=false",
                    "configuration_payload=true",
                ),
            ),
            (
                "missing-configuration",
                (bridge_only, self.runtime_jar, self.module_jar),
                (
                    "configuration payload count=0; expected 1",
                    str(bridge_only),
                    "bridge_payload=true",
                    "configuration_payload=false",
                ),
            ),
            (
                "duplicate-payloads",
                (self.binding_aar, duplicate, self.runtime_jar, self.module_jar),
                (
                    "bridge payload count=2; expected 1",
                    "configuration payload count=2; expected 1",
                    str(self.binding_aar),
                    str(duplicate),
                    "bridge_payload=true",
                    "configuration_payload=true",
                ),
            ),
            (
                "split-payloads",
                (bridge_only, config_only, self.runtime_jar, self.module_jar),
                (
                    "bridge and configuration must use the same binding artifact",
                    str(bridge_only),
                    str(config_only),
                ),
            ),
            (
                "missing-requested-abi",
                (missing_requested_abi, self.runtime_jar, self.module_jar),
                (
                    str(missing_requested_abi),
                    "missing abi=x86_64",
                ),
            ),
        )
        for name, artifacts, diagnostics in cases:
            with self.subTest(name=name):
                self._assert_plugin_rejects(name, artifacts, diagnostics)

    def test_rejects_empty_and_unsupported_requested_abis(self) -> None:
        artifacts = (self.binding_aar, self.runtime_jar, self.module_jar)
        for name, requested_abis, diagnostics in (
            (
                "empty-requested-abis",
                (),
                ("requested_abis must contain at least one Android ABI",),
            ),
            (
                "unsupported-requested-abi",
                ("mips",),
                ("Unsupported Foundry-Java ABI mips",),
            ),
        ):
            with self.subTest(name=name):
                self._assert_plugin_rejects(
                    name,
                    artifacts,
                    diagnostics,
                    requested_abis=requested_abis,
                )

    def test_local_debug_single_abi_matrix(self) -> None:
        for requested_abi in ("armeabi-v7a", "arm64-v8a", "x86", "x86_64"):
            with self.subTest(requested_abi=requested_abi):
                evidence = self._local_debug_evidence(requested_abi)
                self.assertEqual(
                    (f"lib/{requested_abi}/libfoundry_android.so",),
                    evidence.get("host_entries"),
                )
                self.assertEqual(
                    (f"lib/{requested_abi}/libc++_shared.so",),
                    evidence.get("unrelated_native_entries"),
                )
                self.assertEqual(
                    tuple(sorted(FOUNDRY_SYMBOLS + EXTERNAL_JNI_SYMBOLS)),
                    evidence.get("host_exported_symbols"),
                )
                self.assertEqual((), evidence.get("unrelated_exported_symbols"))
                self.assertIs(True, evidence.get("native_debug_symbols_preserved"))

    def test_staged_maven_x86_64_debug_matches_local(self) -> None:
        requested_abis = ("x86_64",)
        repository, marker = self._stage_maven_graph()
        local_evidence = self._local_debug_evidence("x86_64")
        app = self._prepare_app("maven-debug-parity")
        properties = self._maven_properties(repository, marker, requested_abis)
        properties["doNotStrip"] = "true"
        first, second = self._build_twice(
            app,
            properties,
        )
        maven_evidence = self._assert_outputs(
            app,
            first,
            second,
            requested_abis=requested_abis,
            expected_application_id="games.cafecito.foundry.game",
            preserve_native_debug_symbols=True,
        )
        self.assertEqual(local_evidence, maven_evidence)

    def test_ordinary_marker_absent_debug_and_release_remain_inert(self) -> None:
        for build_type, application_id in (
            ("debug", "games.cafecito.foundry.game"),
            ("release", "dev.example.ordinary"),
        ):
            with self.subTest(build_type=build_type):
                app = self._prepare_app(f"ordinary-{build_type}", build_type=build_type)
                properties = {"export_enabled_abis": "x86_64"}
                if build_type == "release":
                    properties["export_package_name"] = application_id
                first = self._run_app(app, properties, f"assembleStandard{build_type.capitalize()}")
                second = self._run_app(app, properties, f"assembleStandard{build_type.capitalize()}")
                for result in (first, second):
                    output = result.stdout + result.stderr
                    self.assertEqual(0, result.returncode, output)
                    self.assertNotIn("FoundryJavaRegistry", output)
                    self.assertNotIn("games.cafecito.foundry.java", output)
                self.assertIn("Reusing configuration cache.", second.stdout + second.stderr)

                generated = app / "build/generated"
                self.assertFalse(any(generated.glob("**/*FoundryJava*")))
                apk = app / f"build/outputs/apk/standard/{build_type}/android_{build_type}.apk"
                with zipfile.ZipFile(apk) as archive:
                    names = set(archive.namelist())
                    self.assertNotIn("assets/FoundryJava.foundryextension", names)
                    self.assertNotIn("assets/foundry_java/registry-index-v2.txt", names)
                    self.assertFalse(any(name.endswith("/libfoundry_java.so") for name in names))
                    self.assertEqual(
                        {"lib/x86_64/libfoundry_android.so"},
                        {name for name in names if name.endswith("/libfoundry_android.so")},
                    )
                metadata = json.loads(
                    (app / f"build/outputs/apk/standard/{build_type}/output-metadata.json").read_text(encoding="utf-8")
                )
                self.assertEqual(application_id, metadata["applicationId"])
                merged = sorted(
                    (app / f"build/intermediates/merged_manifest/standard{build_type.capitalize()}").rglob(
                        "AndroidManifest.xml"
                    )
                )
                self.assertEqual(1, len(merged), merged)
                self.assertNotIn(
                    "games.cafecito.foundry.generated.FoundryGeneratedStartupProvider",
                    merged[0].read_text(encoding="utf-8"),
                )
                self.assertFalse((app / f"build/outputs/mapping/standard{build_type.capitalize()}").exists())

    def test_local_x86_64_minified_release_is_reproducible(self) -> None:
        requested_abis = ("x86_64",)
        application_id = "dev.example.foundryjava"
        app = self._prepare_app(
            "local-release-x86_64",
            build_type="release",
        )
        properties = self._local_properties(requested_abis)
        properties["export_package_name"] = application_id
        first, second = self._build_twice(
            app,
            properties,
            build_type="release",
        )
        second_output = second.stdout + second.stderr
        self.assertNotIn(
            "> Task :generateStandardReleaseFoundryJavaRegistry UP-TO-DATE",
            second_output,
        )
        self._assert_outputs(
            app,
            first,
            second,
            requested_abis=requested_abis,
            expected_application_id=application_id,
            build_type="release",
        )
        build = APP_BUILD.read_text(encoding="utf-8")
        self.assertIn("minifyEnabled getFoundryJavaEnabled()", build)
        self.assertNotIn("-keep class games.cafecito.foundry.**", build)

    def test_zero_module_real_apk_is_rejected_by_final_inspector(self) -> None:
        requested_abis = ("x86_64",)
        app = self._prepare_app("zero-descriptor")
        result = self._run_app(
            app,
            self._local_properties(requested_abis, include_module=False),
            "assembleStandardDebug",
        )
        output = result.stdout + result.stderr
        self.assertEqual(0, result.returncode, output)

        generated_assets = app / "build/generated/assets/generateStandardDebugFoundryJavaRegistry"
        generated_java = app / "build/generated/java/generateStandardDebugFoundryJavaRegistry"
        self.assertFalse((generated_assets / "FoundryJava.foundryextension").exists())
        self.assertFalse((generated_assets / "foundry_java/registry-index-v2.txt").exists())
        self.assertFalse((generated_java / "games/cafecito/foundry/generated/FoundryGeneratedBootstrap.java").exists())
        apk = app / "build/outputs/apk/standard/debug/android_debug.apk"
        inspector = load_device_acceptance_module()
        with self.assertRaisesRegex(
            inspector.AcceptanceError,
            "exactly one assets/FoundryJava.foundryextension; found 0",
        ):
            inspector.inspect_foundry_java_apk(
                apk,
                requested_abis=requested_abis,
                enabled=True,
            )


class FoundryJavaExporterContractTests(unittest.TestCase):
    @staticmethod
    def _development_binary() -> Path:
        binaries = [
            path for path in (REPO_ROOT / "bin").glob("foundry.*editor*") if path.is_file() and os.access(path, os.X_OK)
        ]
        if not binaries:
            raise unittest.SkipTest("A development editor binary is required for command-first export validation")
        return max(binaries, key=lambda path: path.stat().st_mtime_ns)

    @staticmethod
    def _write_export_preset(
        project: Path,
        *,
        plugin_local: Path | None = None,
        local_artifacts: tuple[Path, ...] = (),
        use_gradle: bool,
        extra_options: tuple[str, ...] = (),
    ) -> None:
        options = [
            f"gradle_build/use_gradle_build={'true' if use_gradle else 'false'}",
            "gradle_build/foundry_java/enabled=true",
        ]
        if plugin_local is not None:
            options.append(f'gradle_build/foundry_java/gradle_plugin_local="{plugin_local}"')
        if local_artifacts:
            encoded_paths = ", ".join(f'"{path}"' for path in local_artifacts)
            options.append(f"gradle_build/foundry_java/local_artifacts=PackedStringArray({encoded_paths})")
        options.extend(extra_options)
        options_text = "\n".join(options)
        project.joinpath("export_presets.cfg").write_text(
            textwrap.dedent(
                f"""
                [preset.0]

                name="Android"
                platform="Android"
                runnable=false
                export_filter="all_resources"
                include_filter=""
                exclude_filter=""

                [preset.0.options]

                {options_text}
                """
            ).strip()
            + "\n",
            encoding="utf-8",
        )

    def _run_export(
        self,
        project: Path,
        output_name: str = "should-not-exist.apk",
        *,
        verbose: bool = False,
        timeout: int = 60,
    ) -> subprocess.CompletedProcess[str]:
        return run_bounded_subprocess(
            [
                str(self._development_binary()),
                *(("--verbose",) if verbose else ()),
                "--headless",
                "project",
                "export",
                "--project",
                str(project),
                "--preset",
                "Android",
                "--output",
                str(project / output_name),
            ],
            cwd=project,
            timeout=timeout,
        )

    @classmethod
    def _write_fake_gradle_wrapper(
        cls,
        project: Path,
        entries: tuple[str, ...],
        *,
        reported_entry_count: int | None = None,
        central_directory_mutation: str | None = None,
        force_zip64: bool = False,
        force_local_zip64_sizes: bool = False,
        streaming_data_descriptor: bool = False,
        lock_output_directory: bool = False,
        sfx_prefix: bytes = b"",
    ) -> Path:
        gradle_root = project / "android"
        build = gradle_root / "build"
        (build / "src/debug").mkdir(parents=True)
        (build / "src/release").mkdir(parents=True)
        shutil.copytree(APP_ROOT / "res", build / "res")
        (build / "build.gradle").write_text("// Controlled exporter acceptance fixture.\n", encoding="utf-8")
        binary_version = subprocess.run(
            [str(cls._development_binary()), "--version"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        gradle_root.joinpath(".build_version").write_text(
            binary_version.rsplit(".", maxsplit=2)[0] + "\n",
            encoding="utf-8",
        )
        wrapper = build / "gradlew"
        wrapper.write_text(
            textwrap.dedent(
                f"""\
                #!{sys.executable}
                import struct
                import sys
                import zipfile
                from pathlib import Path

                class NonSeekableWriter:
                    def __init__(self, output):
                        self.output = output

                    def write(self, data):
                        return self.output.write(data)

                    def flush(self):
                        self.output.flush()

                def write_entries(archive):
                    for entry in {entries!r}:
                        if {force_local_zip64_sizes!r}:
                            with archive.open(entry, "w", force_zip64=True) as output:
                                output.write(entry.encode("utf-8"))
                        elif {force_zip64!r}:
                            entry_info = zipfile.ZipInfo(entry)
                            entry_info.extra = struct.pack("<HHQ", 0x0001, 8, 0)
                            archive.writestr(entry_info, entry)
                        else:
                            archive.writestr(entry, entry)

                arguments = sys.argv[1:]
                export_path = next(
                    (value.removeprefix("-Pexport_path=file:") for value in arguments if value.startswith("-Pexport_path=file:")),
                    None,
                )
                export_filename = next(
                    (value.removeprefix("-Pexport_filename=") for value in arguments if value.startswith("-Pexport_filename=")),
                    None,
                )
                if export_path is not None and export_filename is not None:
                    destination = Path(export_path) / export_filename
                    if {streaming_data_descriptor!r}:
                        with destination.open("wb") as output:
                            with zipfile.ZipFile(NonSeekableWriter(output), "w") as archive:
                                write_entries(archive)
                    else:
                        with zipfile.ZipFile(destination, "w") as archive:
                            write_entries(archive)
                    if {lock_output_directory!r}:
                        destination.parent.chmod(0o500)
                    if {streaming_data_descriptor!r}:
                        streaming_contents = destination.read_bytes()
                        first_local_entry = streaming_contents.find(b"PK\\x03\\x04")
                        if first_local_entry < 0:
                            raise RuntimeError("streaming ZIP has no local file entry")
                        local_flags = int.from_bytes(
                            streaming_contents[first_local_entry + 6 : first_local_entry + 8],
                            "little",
                        )
                        if not local_flags & 0x0008 or b"PK\\x07\\x08" not in streaming_contents:
                            raise RuntimeError("streaming ZIP did not emit a real data descriptor")
                    reported_entry_count = {reported_entry_count!r}
                    central_directory_mutation = {central_directory_mutation!r}
                    force_zip64 = {force_zip64!r}
                    if reported_entry_count is not None or central_directory_mutation is not None or force_zip64:
                        contents = bytearray(destination.read_bytes())
                        eocd = contents.rfind(b"PK\\x05\\x06")
                        if eocd < 0:
                            raise RuntimeError("fake Gradle output has no classic ZIP end record")
                        if reported_entry_count is not None:
                            count = reported_entry_count.to_bytes(2, "little")
                            contents[eocd + 8 : eocd + 12] = count + count
                        if central_directory_mutation == "last-comment-bounds":
                            central_entry = contents.rfind(b"PK\\x01\\x02", 0, eocd)
                            if central_entry < 0:
                                raise RuntimeError("fake Gradle output has no central directory entry")
                            contents[central_entry + 32 : central_entry + 34] = b"\\xff\\xff"
                        elif central_directory_mutation == "central-offset":
                            central_offset = int.from_bytes(contents[eocd + 16 : eocd + 20], "little")
                            if central_offset == 0:
                                raise RuntimeError("fake Gradle output has no mutable central directory offset")
                            contents[eocd + 16 : eocd + 20] = (central_offset - 1).to_bytes(4, "little")
                        elif central_directory_mutation == "last-local-offset":
                            central_offset = int.from_bytes(contents[eocd + 16 : eocd + 20], "little")
                            first_central_entry = contents.find(b"PK\\x01\\x02", central_offset, eocd)
                            final_central_entry = contents.rfind(b"PK\\x01\\x02", central_offset, eocd)
                            if first_central_entry < 0 or final_central_entry <= first_central_entry:
                                raise RuntimeError("fake Gradle output has no distinct central directory entries")
                            contents[final_central_entry + 42 : final_central_entry + 46] = contents[
                                first_central_entry + 42 : first_central_entry + 46
                            ]
                        elif central_directory_mutation == "first-local-name":
                            first_local_entry = contents.find(b"PK\\x03\\x04")
                            if first_local_entry < 0:
                                raise RuntimeError("fake Gradle output has no local file entry")
                            first_filename_size = int.from_bytes(
                                contents[first_local_entry + 26 : first_local_entry + 28],
                                "little",
                            )
                            if first_filename_size == 0:
                                raise RuntimeError("fake Gradle output has no mutable local filename")
                            contents[first_local_entry + 30] ^= 0x01
                        elif central_directory_mutation == "first-central-compressed-overlap":
                            central_offset = int.from_bytes(contents[eocd + 16 : eocd + 20], "little")
                            first_central_entry = contents.find(b"PK\\x01\\x02", central_offset, eocd)
                            if first_central_entry < 0:
                                raise RuntimeError("fake Gradle output has no mutable first central entry")
                            contents[first_central_entry + 20 : first_central_entry + 24] = central_offset.to_bytes(
                                4,
                                "little",
                            )
                        elif central_directory_mutation == "bit3-local-sentinels":
                            central_offset = int.from_bytes(contents[eocd + 16 : eocd + 20], "little")
                            first_central_entry = contents.find(b"PK\\x01\\x02", central_offset, eocd)
                            first_local_entry = contents.find(b"PK\\x03\\x04")
                            if first_central_entry < 0 or first_local_entry < 0:
                                raise RuntimeError("fake Gradle output has no mutable first entry")
                            central_flags = int.from_bytes(
                                contents[first_central_entry + 8 : first_central_entry + 10],
                                "little",
                            )
                            local_flags = int.from_bytes(
                                contents[first_local_entry + 6 : first_local_entry + 8],
                                "little",
                            )
                            contents[first_central_entry + 8 : first_central_entry + 10] = (
                                central_flags | 0x0008
                            ).to_bytes(2, "little")
                            contents[first_local_entry + 6 : first_local_entry + 8] = (
                                local_flags | 0x0008
                            ).to_bytes(2, "little")
                            contents[first_local_entry + 14 : first_local_entry + 18] = b"\\x00" * 4
                            contents[first_local_entry + 18 : first_local_entry + 26] = b"\\xff" * 8
                        elif central_directory_mutation in (
                            "central-digital-signature",
                            "central-digital-signature-size",
                        ):
                            central_size = int.from_bytes(contents[eocd + 12 : eocd + 16], "little")
                            signature_payload = b"Foundry-Java"
                            signature_size = len(signature_payload)
                            if central_directory_mutation == "central-digital-signature-size":
                                signature_size += 1
                            signature = struct.pack("<IH", 0x05054B50, signature_size) + signature_payload
                            contents[eocd + 12 : eocd + 16] = (central_size + len(signature)).to_bytes(4, "little")
                            contents[eocd:eocd] = signature
                        elif central_directory_mutation == "classic-locator-magic":
                            central_size = int.from_bytes(contents[eocd + 12 : eocd + 16], "little")
                            final_central_entry = contents.rfind(b"PK\\x01\\x02", 0, eocd)
                            if final_central_entry < 0:
                                raise RuntimeError("fake Gradle output has no final central directory entry")
                            locator_lookalike = b"PK\\x06\\x07" + b"classic-comment!"
                            contents[final_central_entry + 32 : final_central_entry + 34] = len(
                                locator_lookalike
                            ).to_bytes(2, "little")
                            contents[eocd + 12 : eocd + 16] = (central_size + len(locator_lookalike)).to_bytes(
                                4,
                                "little",
                            )
                            contents[eocd:eocd] = locator_lookalike
                        elif central_directory_mutation not in (None, "zip64-classic-count"):
                            raise RuntimeError(f"unknown central directory mutation: {{central_directory_mutation}}")
                        if force_zip64:
                            entry_count = int.from_bytes(contents[eocd + 10 : eocd + 12], "little")
                            central_size = int.from_bytes(contents[eocd + 12 : eocd + 16], "little")
                            central_offset = int.from_bytes(contents[eocd + 16 : eocd + 20], "little")
                            final_central_entry = contents.rfind(b"PK\\x01\\x02", central_offset, eocd)
                            if final_central_entry < 0:
                                raise RuntimeError("fake Gradle output has no final central directory entry")
                            final_filename_size = int.from_bytes(
                                contents[final_central_entry + 28 : final_central_entry + 30],
                                "little",
                            )
                            final_extra = final_central_entry + 46 + final_filename_size
                            if contents[final_extra : final_extra + 4] != struct.pack("<HH", 0x0001, 8):
                                raise RuntimeError("fake Gradle output has no ZIP64 offset field")
                            final_local_offset = contents[final_central_entry + 42 : final_central_entry + 46]
                            contents[final_extra + 4 : final_extra + 12] = int.from_bytes(
                                final_local_offset,
                                "little",
                            ).to_bytes(8, "little")
                            contents[final_central_entry + 42 : final_central_entry + 46] = b"\\xff" * 4
                            zip64_end = struct.pack(
                                "<IQHHIIQQQQ",
                                0x06064B50,
                                44,
                                45,
                                45,
                                0,
                                0,
                                entry_count,
                                entry_count,
                                central_size,
                                central_offset,
                            )
                            zip64_locator = struct.pack("<IIQI", 0x07064B50, 0, len({sfx_prefix!r}) + eocd, 1)
                            contents[eocd + 8 : eocd + 12] = b"\\xff\\xff\\xff\\xff"
                            contents[eocd + 12 : eocd + 20] = b"\\xff" * 8
                            if central_directory_mutation == "zip64-classic-count":
                                contradictory_count = (entry_count - 1).to_bytes(2, "little")
                                contents[eocd + 8 : eocd + 12] = contradictory_count * 2
                            contents[eocd:eocd] = zip64_end + zip64_locator
                        destination.write_bytes({sfx_prefix!r} + contents)
                    """
            ),
            encoding="utf-8",
        )
        wrapper.chmod(0o755)
        return gradle_root

    def test_preflight_names_every_fail_closed_boundary(self) -> None:
        exporter = EXPORTER.read_text(encoding="utf-8")
        for fragment in (
            "Invalid export option %s value '%s': %s.",
            "must be an exact group:artifact:version value",
            "must use HTTPS or a local file URL without credentials, query, or fragment",
            "must name a regular .jar or .aar file",
            "must not traverse a symbolic link",
            "must not contain carriage returns, newlines, or '|'",
            "must select exactly one Maven or local Gradle plugin",
            "require at least one Maven or local application artifact",
            "contains forbidden libfoundry_android.so",
            "resolves to a duplicate local artifact",
            "archive entry name is too long",
            "archive entry name contains an embedded NUL byte",
            "archive entry name is not valid UTF-8",
            "archive could not be opened",
            "archive central directory metadata is corrupt",
            "archive reports too many entries",
            "requires at least one enabled Android architecture",
            "-Pfoundry_java_maven_repositories=<redacted>",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, exporter)

    def test_opt_in_empty_architectures_are_rejected_before_gradle(self) -> None:
        exporter = EXPORTER.read_text(encoding="utf-8")
        configuration_check = exporter.find("bool EditorExportPlatformAndroid::has_valid_project_configuration")
        export_helper = exporter.find("Error EditorExportPlatformAndroid::export_project_helper")
        gradle_execution = exporter.find("execute_and_show_output", export_helper)
        for start in (configuration_check, export_helper):
            check = exporter.find("requires at least one enabled Android architecture", start)
            self.assertGreater(check, start)
            if start == export_helper:
                self.assertLess(check, gradle_execution)

    def test_properties_are_emitted_only_inside_the_enabled_branch(self) -> None:
        exporter = EXPORTER.read_text(encoding="utf-8")
        marker = 'cmdline.push_back("-Pfoundry_java_registry_marker=registry-index-v2")'
        enabled_branch = exporter.find("if (foundry_java.enabled)")
        marker_position = exporter.find(marker)
        build_execution = exporter.find("execute_and_show_output", marker_position)
        self.assertGreaterEqual(enabled_branch, 0)
        self.assertGreater(marker_position, enabled_branch)
        self.assertGreater(build_execution, marker_position)

    def test_gradle_export_inspects_the_copied_final_artifact_before_success(self) -> None:
        exporter = EXPORTER.read_text(encoding="utf-8")
        copy_result = exporter.find("int copy_result =")
        inspection = exporter.find("_inspect_foundry_java_artifact(p_path", copy_result)
        success = exporter.find('print_verbose("Successfully completed Android gradle build.")', copy_result)
        self.assertGreaterEqual(copy_result, 0)
        self.assertGreater(inspection, copy_result)
        self.assertGreater(success, inspection)

    def test_command_first_coordinate_latest_rule_applies_only_to_the_version(self) -> None:
        for coordinate, should_succeed in (
            ("latest.games:foundry-latest-plugin:1.0.0", True),
            ("games.cafecito.foundry:foundry-java-plugin:LaTeSt", False),
        ):
            with self.subTest(coordinate=coordinate):
                with tempfile.TemporaryDirectory(
                    prefix="foundry-java-coordinate.",
                    dir=test_scratch_directory(),
                ) as directory:
                    project = Path(directory)
                    project.joinpath("project.foundry").write_text(
                        textwrap.dedent(
                            """\
                            [application]
                            config/name="Foundry Java Coordinate Validation"

                            [rendering]
                            textures/vram_compression/import_etc2_astc=true
                            """
                        ),
                        encoding="utf-8",
                    )
                    module = project / "module.jar"
                    with zipfile.ZipFile(module, "w"):
                        pass
                    gradle_root = self._write_fake_gradle_wrapper(
                        project,
                        (
                            "assets/FoundryJava.foundryextension",
                            "assets/foundry_java/registry-index-v2.txt",
                            "lib/arm64-v8a/libfoundry_java.so",
                        ),
                    )
                    self._write_export_preset(
                        project,
                        local_artifacts=(module,),
                        use_gradle=True,
                        extra_options=(
                            f'gradle_build/foundry_java/gradle_plugin_maven="{coordinate}"',
                            f'gradle_build/gradle_build_directory="{gradle_root}"',
                            "gradle_build/export_format=0",
                            "package/signed=false",
                            "architectures/armeabi-v7a=false",
                            "architectures/arm64-v8a=true",
                            "architectures/x86=false",
                            "architectures/x86_64=false",
                        ),
                    )
                    output_name = "coordinate.apk"
                    result = self._run_export(project, output_name)
                    output = result.stdout + result.stderr
                    if should_succeed:
                        self.assertEqual(0, result.returncode, output)
                        self.assertTrue((project / output_name).is_file())
                    else:
                        self.assertNotEqual(0, result.returncode, output)
                        self.assertIn("must be an exact group:artifact:version value", output)
                        self.assertFalse((project / output_name).exists())

    def test_command_first_export_rejects_malformed_copied_apk_and_aab(self) -> None:
        for extension, export_format, root in (("apk", 0, ""), ("aab", 1, "base/")):
            configuration = f"{root}assets/FoundryJava.foundryextension"
            registry_index = f"{root}assets/foundry_java/registry-index-v2.txt"
            bridge = f"{root}lib/arm64-v8a/libfoundry_java.so"
            valid = (configuration, registry_index, bridge)
            cases = (
                ("missing-config", valid[1:], configuration),
                ("missing-index", (valid[0], valid[2]), registry_index),
                ("duplicate-config", (*valid, configuration), configuration),
                ("duplicate-index", (*valid, registry_index), registry_index),
                ("missing-bridge", valid[:2], "bridge entries differ"),
                ("duplicate-bridge", (*valid, bridge), "bridge entries differ"),
                (
                    "unrequested-bridge",
                    (*valid, f"{root}lib/x86_64/libfoundry_java.so"),
                    "bridge entries differ",
                ),
            )
            for defect, entries, diagnostic in cases:
                with self.subTest(extension=extension, defect=defect):
                    with tempfile.TemporaryDirectory(
                        prefix=f"foundry-java-final-{extension}.",
                        dir=test_scratch_directory(),
                    ) as directory:
                        project = Path(directory)
                        project.joinpath("project.foundry").write_text(
                            textwrap.dedent(
                                """\
                                [application]
                                config/name="Foundry Java Final Artifact Inspection"

                                [rendering]
                                textures/vram_compression/import_etc2_astc=true
                                """
                            ),
                            encoding="utf-8",
                        )
                        plugin = project / "plugin.jar"
                        module = project / "module.jar"
                        with zipfile.ZipFile(plugin, "w") as archive:
                            archive.writestr(
                                "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                                "implementation-class=test.Fixture\n",
                            )
                        with zipfile.ZipFile(module, "w"):
                            pass
                        unrelated_sentinel = project / "unrelated-sentinel.txt"
                        unrelated_sentinel.write_bytes(b"preserve unrelated output\n")
                        gradle_root = self._write_fake_gradle_wrapper(project, entries)
                        self._write_export_preset(
                            project,
                            plugin_local=plugin,
                            local_artifacts=(module,),
                            use_gradle=True,
                            extra_options=(
                                f'gradle_build/gradle_build_directory="{gradle_root}"',
                                f"gradle_build/export_format={export_format}",
                                "package/signed=false",
                                "architectures/armeabi-v7a=false",
                                "architectures/arm64-v8a=true",
                                "architectures/x86=false",
                                "architectures/x86_64=false",
                            ),
                        )
                        output_name = f"malformed.{extension}"
                        result = self._run_export(project, output_name)
                        output = result.stdout + result.stderr
                        self.assertNotEqual(0, result.returncode, output)
                        self.assertIn(diagnostic, output)
                        self.assertFalse((project / output_name).exists())
                        self.assertEqual(b"preserve unrelated output\n", unrelated_sentinel.read_bytes())

    def test_rejected_final_artifact_reports_cleanup_failure(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-final-cleanup.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                textwrap.dedent(
                    """\
                    [application]
                    config/name="Foundry Java Final Artifact Cleanup"

                    [rendering]
                    textures/vram_compression/import_etc2_astc=true
                    """
                ),
                encoding="utf-8",
            )
            plugin = project / "plugin.jar"
            module = project / "module.jar"
            with zipfile.ZipFile(plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            with zipfile.ZipFile(module, "w"):
                pass
            gradle_root = self._write_fake_gradle_wrapper(
                project,
                (
                    "assets/foundry_java/registry-index-v2.txt",
                    "lib/arm64-v8a/libfoundry_java.so",
                ),
                lock_output_directory=True,
            )
            self._write_export_preset(
                project,
                plugin_local=plugin,
                local_artifacts=(module,),
                use_gradle=True,
                extra_options=(
                    f'gradle_build/gradle_build_directory="{gradle_root}"',
                    "gradle_build/export_format=0",
                    "package/signed=false",
                    "architectures/armeabi-v7a=false",
                    "architectures/arm64-v8a=true",
                    "architectures/x86=false",
                    "architectures/x86_64=false",
                ),
            )
            try:
                result = self._run_export(project, "malformed.apk")
            finally:
                project.chmod(0o700)
            output = result.stdout + result.stderr
            self.assertNotEqual(0, result.returncode, output)
            self.assertIn("assets/FoundryJava.foundryextension", output)
            self.assertIn("could not remove rejected final artifact", output)

    def test_command_first_export_rejects_inconsistent_final_archive_metadata(self) -> None:
        for extension, export_format, root in (("apk", 0, ""), ("aab", 1, "base/")):
            valid_entries = (
                f"{root}assets/FoundryJava.foundryextension",
                f"{root}assets/foundry_java/registry-index-v2.txt",
                f"{root}lib/arm64-v8a/libfoundry_java.so",
            )
            cases = (
                (
                    "underreported",
                    (*valid_entries, f"{root}lib/x86_64/libfoundry_java.so"),
                    3,
                    None,
                    False,
                    "entry count does not match its central directory",
                    False,
                ),
                (
                    "last-comment-bounds",
                    valid_entries,
                    None,
                    "last-comment-bounds",
                    False,
                    "central directory metadata is corrupt",
                    False,
                ),
                (
                    "central-offset",
                    valid_entries,
                    None,
                    "central-offset",
                    False,
                    "central directory metadata is corrupt",
                    False,
                ),
                (
                    "last-local-offset",
                    valid_entries,
                    None,
                    "last-local-offset",
                    False,
                    "central directory metadata is corrupt",
                    False,
                ),
                (
                    "first-local-name",
                    valid_entries,
                    None,
                    "first-local-name",
                    False,
                    "central directory metadata is corrupt",
                    False,
                ),
                (
                    "compressed-payload-overlap",
                    valid_entries,
                    None,
                    "first-central-compressed-overlap",
                    False,
                    "central directory metadata is corrupt",
                    True,
                ),
                (
                    "central-digital-signature-size",
                    valid_entries,
                    None,
                    "central-digital-signature-size",
                    False,
                    "central directory metadata is corrupt",
                    False,
                ),
                (
                    "zip64-classic-count",
                    valid_entries,
                    None,
                    "zip64-classic-count",
                    True,
                    "central directory metadata is corrupt",
                    False,
                ),
            )
            for (
                defect,
                entries,
                reported_entry_count,
                central_directory_mutation,
                force_zip64,
                diagnostic,
                streaming_data_descriptor,
            ) in cases:
                with self.subTest(extension=extension, defect=defect):
                    with tempfile.TemporaryDirectory(
                        prefix=f"foundry-java-final-{defect}-{extension}.",
                        dir=test_scratch_directory(),
                    ) as directory:
                        project = Path(directory)
                        project.joinpath("project.foundry").write_text(
                            textwrap.dedent(
                                """\
                                [application]
                                config/name="Foundry Java Final ZIP Consistency"

                                [rendering]
                                textures/vram_compression/import_etc2_astc=true
                                """
                            ),
                            encoding="utf-8",
                        )
                        plugin = project / "plugin.jar"
                        module = project / "module.jar"
                        with zipfile.ZipFile(plugin, "w") as archive:
                            archive.writestr(
                                "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                                "implementation-class=test.Fixture\n",
                            )
                        with zipfile.ZipFile(module, "w"):
                            pass
                        unrelated_sentinel = project / "unrelated-sentinel.txt"
                        unrelated_sentinel.write_bytes(b"preserve unrelated output\n")
                        gradle_root = self._write_fake_gradle_wrapper(
                            project,
                            entries,
                            reported_entry_count=reported_entry_count,
                            central_directory_mutation=central_directory_mutation,
                            force_zip64=force_zip64,
                            streaming_data_descriptor=streaming_data_descriptor,
                        )
                        self._write_export_preset(
                            project,
                            plugin_local=plugin,
                            local_artifacts=(module,),
                            use_gradle=True,
                            extra_options=(
                                f'gradle_build/gradle_build_directory="{gradle_root}"',
                                f"gradle_build/export_format={export_format}",
                                "package/signed=false",
                                "architectures/armeabi-v7a=false",
                                "architectures/arm64-v8a=true",
                                "architectures/x86=false",
                                "architectures/x86_64=false",
                            ),
                        )

                        output_name = f"{defect}.{extension}"
                        result = self._run_export(project, output_name)
                        output = result.stdout + result.stderr
                        self.assertNotEqual(0, result.returncode, output)
                        self.assertIn(diagnostic, output)
                        self.assertFalse((project / output_name).exists())
                        self.assertEqual(b"preserve unrelated output\n", unrelated_sentinel.read_bytes())

    def test_command_first_export_accepts_valid_zip64_final_archive_metadata(self) -> None:
        for extension, export_format, root in (("apk", 0, ""), ("aab", 1, "base/")):
            compatibility_cases = (
                (
                    "zip64-directory",
                    True,
                    False,
                    None,
                    b"Foundry-Java SFX fixture\n" if extension == "aab" else b"",
                ),
                ("zip64-local-sizes", False, True, None, b""),
                ("bit3-local-sentinels", False, False, "bit3-local-sentinels", b""),
                ("streaming-data-descriptor", False, False, None, b""),
                ("central-digital-signature", False, False, "central-digital-signature", b""),
            )
            for (
                compatibility,
                force_zip64,
                force_local_zip64_sizes,
                central_directory_mutation,
                sfx_prefix,
            ) in compatibility_cases:
                with self.subTest(extension=extension, compatibility=compatibility):
                    self._run_valid_final_archive_compatibility_case(
                        extension=extension,
                        export_format=export_format,
                        root=root,
                        compatibility=compatibility,
                        force_zip64=force_zip64,
                        force_local_zip64_sizes=force_local_zip64_sizes,
                        streaming_data_descriptor=compatibility == "streaming-data-descriptor",
                        central_directory_mutation=central_directory_mutation,
                        sfx_prefix=sfx_prefix,
                    )

    def _run_valid_final_archive_compatibility_case(
        self,
        *,
        extension: str,
        export_format: int,
        root: str,
        compatibility: str,
        force_zip64: bool,
        force_local_zip64_sizes: bool,
        streaming_data_descriptor: bool,
        central_directory_mutation: str | None,
        sfx_prefix: bytes,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix=f"foundry-java-final-{compatibility}-{extension}.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                textwrap.dedent(
                    """\
                    [application]
                    config/name="Foundry Java Final ZIP Compatibility"

                    [rendering]
                    textures/vram_compression/import_etc2_astc=true
                    """
                ),
                encoding="utf-8",
            )
            plugin = project / "plugin.jar"
            module = project / "module.jar"
            with zipfile.ZipFile(plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            with zipfile.ZipFile(module, "w"):
                pass
            gradle_root = self._write_fake_gradle_wrapper(
                project,
                (
                    f"{root}assets/FoundryJava.foundryextension",
                    f"{root}assets/foundry_java/registry-index-v2.txt",
                    f"{root}lib/arm64-v8a/libfoundry_java.so",
                ),
                force_zip64=force_zip64,
                force_local_zip64_sizes=force_local_zip64_sizes,
                streaming_data_descriptor=streaming_data_descriptor,
                central_directory_mutation=central_directory_mutation,
                sfx_prefix=sfx_prefix,
            )
            self._write_export_preset(
                project,
                plugin_local=plugin,
                local_artifacts=(module,),
                use_gradle=True,
                extra_options=(
                    f'gradle_build/gradle_build_directory="{gradle_root}"',
                    f"gradle_build/export_format={export_format}",
                    "package/signed=false",
                    "architectures/armeabi-v7a=false",
                    "architectures/arm64-v8a=true",
                    "architectures/x86=false",
                    "architectures/x86_64=false",
                ),
            )

            output_name = f"valid-{compatibility}.{extension}"
            result = self._run_export(project, output_name)
            output = result.stdout + result.stderr
            self.assertEqual(0, result.returncode, output)
            self.assertTrue((project / output_name).is_file())

    def test_command_first_export_accepts_classic_locator_magic_in_final_entry_comment(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-final-classic-locator-magic.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                textwrap.dedent(
                    """\
                    [application]
                    config/name="Foundry Java Classic Locator Magic"

                    [rendering]
                    textures/vram_compression/import_etc2_astc=true
                    """
                ),
                encoding="utf-8",
            )
            plugin = project / "plugin.jar"
            module = project / "module.jar"
            with zipfile.ZipFile(plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            with zipfile.ZipFile(module, "w"):
                pass
            gradle_root = self._write_fake_gradle_wrapper(
                project,
                (
                    "assets/FoundryJava.foundryextension",
                    "assets/foundry_java/registry-index-v2.txt",
                    "lib/arm64-v8a/libfoundry_java.so",
                ),
                central_directory_mutation="classic-locator-magic",
            )
            self._write_export_preset(
                project,
                plugin_local=plugin,
                local_artifacts=(module,),
                use_gradle=True,
                extra_options=(
                    f'gradle_build/gradle_build_directory="{gradle_root}"',
                    "package/signed=false",
                    "architectures/armeabi-v7a=false",
                    "architectures/arm64-v8a=true",
                    "architectures/x86=false",
                    "architectures/x86_64=false",
                ),
            )

            result = self._run_export(project, "classic-locator-magic.apk")
            output = result.stdout + result.stderr
            self.assertEqual(0, result.returncode, output)
            self.assertTrue((project / "classic-locator-magic.apk").is_file())

    def test_command_first_export_rejects_opt_in_without_gradle_before_build(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-export-preflight.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                '[application]\nconfig/name="Foundry Java Export Preflight"\n',
                encoding="utf-8",
            )
            self._write_export_preset(project, use_gradle=False)
            output_path = project / "should-not-exist.apk"
            result = self._run_export(project)
            output = result.stdout + result.stderr
            self.assertNotEqual(0, result.returncode, output)
            self.assertIn("gradle_build/foundry_java/enabled", output)
            self.assertIn("gradle_build/use_gradle_build", output)
            self.assertIn("value 'true'", output)
            self.assertIn("value is 'false'", output)
            self.assertFalse(output_path.exists())
            self.assertNotIn("Starting a Gradle Daemon", output)

    def test_command_first_export_diagnostics_name_exact_option_and_value(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-option-preflight.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                '[application]\nconfig/name="Foundry Java Option Preflight"\n',
                encoding="utf-8",
            )
            plugin = project / "plugin.jar"
            module = project / "module.jar"
            with zipfile.ZipFile(plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            with zipfile.ZipFile(module, "w"):
                pass
            missing = project / "missing-module.jar"

            cases = (
                (
                    None,
                    (),
                    ('gradle_build/foundry_java/gradle_plugin_maven="not-coordinate"',),
                    ("gradle_build/foundry_java/gradle_plugin_maven", "not-coordinate"),
                ),
                (
                    plugin,
                    (module,),
                    ('gradle_build/foundry_java/gradle_plugin_maven="test:plugin:1.0"',),
                    (
                        "gradle_build/foundry_java/gradle_plugin_maven",
                        "test:plugin:1.0",
                        "gradle_build/foundry_java/gradle_plugin_local",
                        str(plugin),
                    ),
                ),
                (
                    plugin,
                    (module,),
                    ('gradle_build/foundry_java/maven_repositories=PackedStringArray("ftp://repo.invalid")',),
                    ("gradle_build/foundry_java/maven_repositories", "<redacted>"),
                ),
                (
                    plugin,
                    (module,),
                    ('gradle_build/foundry_java/maven_artifacts=PackedStringArray("not-coordinate")',),
                    ("gradle_build/foundry_java/maven_artifacts", "not-coordinate"),
                ),
                (
                    plugin,
                    (missing,),
                    (),
                    ("gradle_build/foundry_java/local_artifacts", str(missing)),
                ),
                (
                    plugin,
                    (),
                    (),
                    (
                        "gradle_build/foundry_java/maven_artifacts",
                        "gradle_build/foundry_java/local_artifacts",
                        "<empty>",
                    ),
                ),
            )
            for plugin_local, local_artifacts, extra_options, expected in cases:
                with self.subTest(expected=expected):
                    self._write_export_preset(
                        project,
                        plugin_local=plugin_local,
                        local_artifacts=local_artifacts,
                        use_gradle=True,
                        extra_options=extra_options,
                    )
                    result = self._run_export(project)
                    output = result.stdout + result.stderr
                    self.assertNotEqual(0, result.returncode, output)
                    for fragment in expected:
                        self.assertIn(fragment, output)
                    self.assertFalse((project / "should-not-exist.apk").exists())
                    self.assertNotIn("Starting a Gradle Daemon", output)

    def test_command_first_repository_preflight_never_echoes_rejected_secrets(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-repository-secrets.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                '[application]\nconfig/name="Foundry Java Repository Secrets"\n',
                encoding="utf-8",
            )
            plugin = project / "plugin.jar"
            module = project / "module.jar"
            with zipfile.ZipFile(plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            with zipfile.ZipFile(module, "w"):
                pass

            for repository in (
                "http://example.invalid/insecure",
                " https://example.invalid/repository",
                "https://example.invalid/repository ",
                "https://invalid_host.example/repository",
                "https://-invalid.example/repository",
                "https://example.invalid:0/repository",
                "https://example.invalid:65536/repository",
                "https://[::1]:0/repository",
                "https://[::1]:65536/repository",
                "https://[::1]invalid/repository",
                "https://[::1]:8443invalid/repository",
                "https://[example.invalid]/repository",
                "https://[127.0.0.1]/repository",
                "https://user:password@example.invalid/repository",
                "https://example.invalid/repository?token=secret-query-token",
                "https://example.invalid/repository#secret-fragment-token",
                "file:/repository",
                "file:////remote-path/repository",
                "file:///tmp/repository-%",
                "file:///tmp/repository-%0",
                "file:///tmp/repository-%GG",
                "https://example.invalid/repository-%GG",
                "file:///tmp/repository{invalid",
                "file:///tmp/repository\\invalid",
                "file:///tmp/repository-é",
            ):
                with self.subTest(repository=repository):
                    encoded_repository = repository.replace("\\", "\\\\")
                    self._write_export_preset(
                        project,
                        plugin_local=plugin,
                        local_artifacts=(module,),
                        use_gradle=True,
                        extra_options=(
                            f'gradle_build/foundry_java/maven_repositories=PackedStringArray("{encoded_repository}")',
                        ),
                    )
                    result = self._run_export(project)
                    output = result.stdout + result.stderr
                    self.assertNotEqual(0, result.returncode, output)
                    self.assertIn("gradle_build/foundry_java/maven_repositories", output)
                    self.assertIn("<redacted>", output)
                    self.assertNotIn(repository, output)

    def test_command_first_accepts_mixed_case_plugin_and_redacts_accepted_repositories(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-repository-log.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                textwrap.dedent(
                    """\
                    [application]
                    config/name="Foundry Java Repository Log"

                    [rendering]
                    textures/vram_compression/import_etc2_astc=true
                    """
                ),
                encoding="utf-8",
            )
            plugin = project / "plugin.JAR"
            module = project / "module.jar"
            with zipfile.ZipFile(plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            with zipfile.ZipFile(module, "w"):
                pass
            gradle_root = self._write_fake_gradle_wrapper(
                project,
                (
                    "assets/FoundryJava.foundryextension",
                    "assets/foundry_java/registry-index-v2.txt",
                    "lib/arm64-v8a/libfoundry_java.so",
                ),
            )
            for index, repository in enumerate(
                (
                    (project / "repository secret token").resolve().as_uri(),
                    "file:///",
                    "https://example.invalid/repository/%4a",
                    "https://example.invalid./repository",
                    "https://[::1]:8443/repository",
                )
            ):
                with self.subTest(repository=repository):
                    self._write_export_preset(
                        project,
                        plugin_local=plugin,
                        local_artifacts=(module,),
                        use_gradle=True,
                        extra_options=(
                            f'gradle_build/gradle_build_directory="{gradle_root}"',
                            f'gradle_build/foundry_java/maven_repositories=PackedStringArray("{repository}")',
                            "package/signed=false",
                            "architectures/armeabi-v7a=false",
                            "architectures/arm64-v8a=true",
                            "architectures/x86=false",
                            "architectures/x86_64=false",
                        ),
                    )

                    result = self._run_export(project, f"redacted-{index}.apk", verbose=True)
                    output = result.stdout + result.stderr
                    self.assertEqual(0, result.returncode, output)
                    self.assertIn("-Pfoundry_java_maven_repositories=<redacted>", output)
                    self.assertNotIn(repository, output)

    def test_command_first_export_rejects_unsafe_local_archives_before_build(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-local-preflight.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                '[application]\nconfig/name="Foundry Java Local Preflight"\n',
                encoding="utf-8",
            )
            plugin = project / "plugin.jar"
            module = project / "module.jar"
            with zipfile.ZipFile(plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            with zipfile.ZipFile(module, "w"):
                pass

            host_archives = []
            for name, entry in (
                ("root-host.jar", "libfoundry_android.so"),
                ("nested-host.aar", "jni/arm64-v8a/libfoundry_android.so"),
            ):
                archive_path = project / name
                with zipfile.ZipFile(archive_path, "w") as archive:
                    archive.writestr(entry, b"host")
                host_archives.append(archive_path)

            linked_module = project / "linked-module.jar"
            linked_module.symlink_to(module)
            real_directory = project / "real-artifacts"
            real_directory.mkdir()
            parent_module = real_directory / "parent-module.jar"
            shutil.copyfile(module, parent_module)
            linked_directory = project / "linked-artifacts"
            linked_directory.symlink_to(real_directory, target_is_directory=True)
            hardlink_module = project / "hardlink-module.jar"
            os.link(module, hardlink_module)

            cases = (
                ((host_archives[0],), "gradle_build/foundry_java/local_artifacts", str(host_archives[0])),
                ((host_archives[1],), "gradle_build/foundry_java/local_artifacts", str(host_archives[1])),
                ((linked_module,), "gradle_build/foundry_java/local_artifacts", str(linked_module)),
                (
                    (linked_directory / parent_module.name,),
                    "gradle_build/foundry_java/local_artifacts",
                    str(linked_directory / parent_module.name),
                ),
                (
                    (module, project / "sub/../module.jar"),
                    "gradle_build/foundry_java/local_artifacts",
                    str(project / "sub/../module.jar"),
                ),
                (
                    (module, hardlink_module),
                    "gradle_build/foundry_java/local_artifacts",
                    str(hardlink_module),
                ),
            )
            for local_artifacts, option, value in cases:
                with self.subTest(local_artifacts=local_artifacts):
                    self._write_export_preset(
                        project,
                        plugin_local=plugin,
                        local_artifacts=local_artifacts,
                        use_gradle=True,
                    )
                    result = self._run_export(project)
                    output = result.stdout + result.stderr
                    self.assertNotEqual(0, result.returncode, output)
                    self.assertIn(option, output)
                    self.assertIn(value, output)
                    self.assertFalse((project / "should-not-exist.apk").exists())
                    self.assertNotIn("Starting a Gradle Daemon", output)

    def test_command_first_export_rejects_unreadable_archive_metadata_before_build(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-archive-preflight.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                '[application]\nconfig/name="Foundry Java Archive Preflight"\n',
                encoding="utf-8",
            )
            valid_plugin = project / "plugin.jar"
            valid_module = project / "module.jar"
            with zipfile.ZipFile(valid_plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            with zipfile.ZipFile(valid_module, "w"):
                pass

            oversized_plugin = project / "oversized-plugin.jar"
            with zipfile.ZipFile(oversized_plugin, "w") as archive:
                archive.writestr("x" * 16384, b"oversized")
            maximum_oversized_plugin = project / "maximum-oversized-plugin.jar"
            with zipfile.ZipFile(maximum_oversized_plugin, "w") as archive:
                archive.writestr("x" * 65535, b"maximum-oversized")
            corrupt_plugin = project / "corrupt-plugin.jar"
            corrupt_plugin.write_bytes(b"not a ZIP archive")
            hidden_host_plugin = project / "hidden-host-plugin.jar"
            with zipfile.ZipFile(hidden_host_plugin, "w") as archive:
                archive.writestr("jni/arm64-v8a/libfoundry_android.so", b"hidden-host")
            hidden_host_bytes = bytearray(hidden_host_plugin.read_bytes())
            hidden_host_eocd = hidden_host_bytes.rfind(b"PK\x05\x06")
            self.assertGreaterEqual(hidden_host_eocd, 0)
            hidden_host_bytes[hidden_host_eocd + 8 : hidden_host_eocd + 12] = b"\x00\x00\x00\x00"
            hidden_host_plugin.write_bytes(hidden_host_bytes)
            underreported_host_plugin = project / "underreported-host-plugin.jar"
            with zipfile.ZipFile(underreported_host_plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
                archive.writestr("jni/arm64-v8a/libfoundry_android.so", b"hidden-host")
            underreported_host_bytes = bytearray(underreported_host_plugin.read_bytes())
            underreported_host_eocd = underreported_host_bytes.rfind(b"PK\x05\x06")
            self.assertGreaterEqual(underreported_host_eocd, 0)
            underreported_host_bytes[underreported_host_eocd + 8 : underreported_host_eocd + 12] = b"\x01\x00\x01\x00"
            underreported_host_plugin.write_bytes(underreported_host_bytes)
            corrupt_comment_plugin = project / "corrupt-comment-plugin.jar"
            with zipfile.ZipFile(corrupt_comment_plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            corrupt_comment_bytes = bytearray(corrupt_comment_plugin.read_bytes())
            corrupt_comment_eocd = corrupt_comment_bytes.rfind(b"PK\x05\x06")
            corrupt_comment_central = corrupt_comment_bytes.rfind(b"PK\x01\x02", 0, corrupt_comment_eocd)
            self.assertGreaterEqual(corrupt_comment_central, 0)
            corrupt_comment_bytes[corrupt_comment_central + 32 : corrupt_comment_central + 34] = b"\xff\xff"
            corrupt_comment_plugin.write_bytes(corrupt_comment_bytes)
            corrupt_offset_plugin = project / "corrupt-offset-plugin.jar"
            with zipfile.ZipFile(corrupt_offset_plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            corrupt_offset_bytes = bytearray(corrupt_offset_plugin.read_bytes())
            corrupt_offset_eocd = corrupt_offset_bytes.rfind(b"PK\x05\x06")
            self.assertGreaterEqual(corrupt_offset_eocd, 0)
            corrupt_offset = int.from_bytes(
                corrupt_offset_bytes[corrupt_offset_eocd + 16 : corrupt_offset_eocd + 20],
                "little",
            )
            self.assertGreater(corrupt_offset, 0)
            corrupt_offset_bytes[corrupt_offset_eocd + 16 : corrupt_offset_eocd + 20] = (corrupt_offset - 1).to_bytes(
                4, "little"
            )
            corrupt_offset_plugin.write_bytes(corrupt_offset_bytes)
            excessive_entries_plugin = project / "excessive-entries-plugin.jar"
            with zipfile.ZipFile(excessive_entries_plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
            excessive_entries_bytes = bytearray(excessive_entries_plugin.read_bytes())
            excessive_entries_eocd = excessive_entries_bytes.rfind(b"PK\x05\x06")
            self.assertGreaterEqual(excessive_entries_eocd, 0)
            excessive_entries_bytes[excessive_entries_eocd + 8 : excessive_entries_eocd + 12] = b"\xff\xff\xff\xff"
            excessive_entries_plugin.write_bytes(excessive_entries_bytes)
            traversal_plugin = project / "traversal-plugin.jar"
            with zipfile.ZipFile(traversal_plugin, "w") as archive:
                archive.writestr("first-entry", b"valid")
            traversal_bytes = bytearray(traversal_plugin.read_bytes())
            eocd = traversal_bytes.rfind(b"PK\x05\x06")
            self.assertGreaterEqual(eocd, 0)
            traversal_bytes[eocd + 8 : eocd + 12] = b"\x02\x00\x02\x00"
            traversal_plugin.write_bytes(traversal_bytes)
            truncated_module = project / "truncated-module.jar"
            truncated_module.write_bytes(valid_module.read_bytes()[:-8])
            invalid_utf8_module = project / "invalid-utf8-module.jar"
            with zipfile.ZipFile(invalid_utf8_module, "w") as archive:
                archive.writestr("invalid-name.jar", b"invalid")
            invalid_bytes = invalid_utf8_module.read_bytes().replace(
                b"invalid-name.jar",
                b"\xffnvalid-name.jar",
            )
            invalid_utf8_module.write_bytes(invalid_bytes)
            embedded_nul_module = project / "embedded-nul-module.jar"
            with zipfile.ZipFile(embedded_nul_module, "w") as archive:
                archive.writestr("embedded-nul.jar", b"invalid")
            embedded_nul_bytes = embedded_nul_module.read_bytes().replace(
                b"embedded-nul.jar",
                b"embedded\x00nul.jar",
            )
            embedded_nul_module.write_bytes(embedded_nul_bytes)

            cases = (
                (
                    oversized_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive entry name is too long",
                ),
                (
                    maximum_oversized_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive entry name is too long",
                ),
                (
                    corrupt_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive could not be opened",
                ),
                (
                    hidden_host_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive entry count does not match its non-empty central directory",
                ),
                (
                    underreported_host_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive entry count does not match its central directory",
                ),
                (
                    corrupt_comment_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive central directory metadata is corrupt",
                ),
                (
                    corrupt_offset_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive central directory metadata is corrupt",
                ),
                (
                    excessive_entries_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive reports too many entries",
                ),
                (
                    traversal_plugin,
                    (valid_module,),
                    "gradle_build/foundry_java/gradle_plugin_local",
                    "archive traversal failed",
                ),
                (
                    valid_plugin,
                    (truncated_module,),
                    "gradle_build/foundry_java/local_artifacts",
                    "archive could not be opened",
                ),
                (
                    valid_plugin,
                    (invalid_utf8_module,),
                    "gradle_build/foundry_java/local_artifacts",
                    "archive entry name is not valid UTF-8",
                ),
                (
                    valid_plugin,
                    (embedded_nul_module,),
                    "gradle_build/foundry_java/local_artifacts",
                    "archive entry name contains an embedded NUL byte",
                ),
            )
            for plugin, local_artifacts, option, diagnostic in cases:
                with self.subTest(plugin=plugin, local_artifacts=local_artifacts):
                    self._write_export_preset(
                        project,
                        plugin_local=plugin,
                        local_artifacts=local_artifacts,
                        use_gradle=True,
                    )
                    result = self._run_export(project)
                    output = result.stdout + result.stderr
                    self.assertNotEqual(0, result.returncode, output)
                    self.assertIn(option, output)
                    offending = plugin if plugin != valid_plugin else local_artifacts[0]
                    self.assertIn(str(offending), output)
                    self.assertIn(diagnostic, output)
                    self.assertFalse((project / "should-not-exist.apk").exists())
                    self.assertNotIn("Starting a Gradle Daemon", output)

    def test_command_first_export_rejects_first_entry_local_header_corruption_before_build(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-first-local-header.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                '[application]\nconfig/name="Foundry Java First Local Header"\n',
                encoding="utf-8",
            )
            plugin = project / "first-entry-corrupt-plugin.jar"
            module = project / "module.jar"
            with zipfile.ZipFile(plugin, "w") as archive:
                archive.writestr(
                    "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                    "implementation-class=test.Fixture\n",
                )
                archive.writestr("second-entry.txt", b"second")
            with zipfile.ZipFile(module, "w"):
                pass

            contents = bytearray(plugin.read_bytes())
            first_local_entry = contents.find(b"PK\x03\x04")
            self.assertGreaterEqual(first_local_entry, 0)
            first_filename_size = int.from_bytes(contents[first_local_entry + 26 : first_local_entry + 28], "little")
            self.assertGreater(first_filename_size, 0)
            contents[first_local_entry + 30] ^= 0x01
            plugin.write_bytes(contents)

            self._write_export_preset(
                project,
                plugin_local=plugin,
                local_artifacts=(module,),
                use_gradle=True,
            )
            result = self._run_export(project)
            output = result.stdout + result.stderr
            self.assertNotEqual(0, result.returncode, output)
            self.assertIn("gradle_build/foundry_java/gradle_plugin_local", output)
            self.assertIn("archive central directory metadata is corrupt", output)
            self.assertFalse((project / "should-not-exist.apk").exists())
            self.assertNotIn("Starting a Gradle Daemon", output)

    def test_command_first_export_rejects_local_header_identity_mismatches_before_build(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="foundry-java-local-identity.",
            dir=test_scratch_directory(),
        ) as directory:
            project = Path(directory)
            project.joinpath("project.foundry").write_text(
                '[application]\nconfig/name="Foundry Java Local Identity"\n',
                encoding="utf-8",
            )
            module = project / "module.jar"
            with zipfile.ZipFile(module, "w"):
                pass

            cases = (
                ("flags", 6, 2, lambda value: value ^ 0x0800),
                ("method", 8, 2, lambda value: value ^ 0x0001),
                ("crc", 14, 4, lambda value: value ^ 0x00000001),
                ("effective-size", 22, 4, lambda value: value + 1),
            )
            for defect, field_offset, field_size, mutate in cases:
                with self.subTest(defect=defect):
                    plugin = project / f"{defect}-plugin.jar"
                    with zipfile.ZipFile(plugin, "w") as archive:
                        archive.writestr(
                            "META-INF/gradle-plugins/games.cafecito.foundry.java.properties",
                            "implementation-class=test.Fixture\n",
                        )
                        archive.writestr("second-entry.txt", b"second")

                    contents = bytearray(plugin.read_bytes())
                    first_local_entry = contents.find(b"PK\x03\x04")
                    self.assertGreaterEqual(first_local_entry, 0)
                    field_start = first_local_entry + field_offset
                    field_end = field_start + field_size
                    value = int.from_bytes(contents[field_start:field_end], "little")
                    contents[field_start:field_end] = mutate(value).to_bytes(field_size, "little")
                    plugin.write_bytes(contents)

                    self._write_export_preset(
                        project,
                        plugin_local=plugin,
                        local_artifacts=(module,),
                        use_gradle=True,
                    )
                    result = self._run_export(project)
                    output = result.stdout + result.stderr
                    self.assertNotEqual(0, result.returncode, output)
                    self.assertIn("gradle_build/foundry_java/gradle_plugin_local", output)
                    self.assertIn("archive central directory metadata is corrupt", output)
                    self.assertFalse((project / "should-not-exist.apk").exists())
                    self.assertNotIn("Starting a Gradle Daemon", output)


if __name__ == "__main__":
    unittest.main()
