from __future__ import annotations

import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import ModuleType

REPO_ROOT = Path(__file__).resolve().parents[2]

EXPORTER = REPO_ROOT / "platform/android/export/export_plugin.cpp"
APP_BUILD = REPO_ROOT / "platform/android/java/app/build.gradle"
APP_CONFIG = REPO_ROOT / "platform/android/java/app/config.gradle"
SOURCE_TEMPLATE_TOOL = REPO_ROOT / "platform/android/android_source_template.py"
GRADLE_WRAPPER = REPO_ROOT / "platform/android/java/gradlew"
APP_ROOT = REPO_ROOT / "platform/android/java/app"

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
    'implementation fileTree(dir: "$addonsDirectory", include: [\'**/*.jar\', \'**/*.aar\'])',
)


def read(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def load_source_template_module() -> ModuleType:
    spec = importlib.util.spec_from_file_location("android_source_template", SOURCE_TEMPLATE_TOOL)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to load {SOURCE_TEMPLATE_TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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
                "FoundryJava.foundryextension",
                "foundry_java/registry-index-v2.txt",
            ),
            inspector.FORBIDDEN_BINDING_FRAGMENTS,
        )


class FoundryJavaGradlePropertyTests(unittest.TestCase):
    workspace_manager: tempfile.TemporaryDirectory[str]
    workspace: Path
    plugin: Path
    artifact: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.workspace_manager = tempfile.TemporaryDirectory(prefix="foundry-java-gradle-properties.")
        cls.workspace = Path(cls.workspace_manager.name)
        cls.plugin = cls.workspace / "plugin.jar"
        cls.artifact = cls.workspace / "module.jar"
        for archive_path in (cls.plugin, cls.artifact):
            with zipfile.ZipFile(archive_path, "w"):
                pass

    @classmethod
    def tearDownClass(cls) -> None:
        cls.workspace_manager.cleanup()

    def run_gradle(self, properties: dict[str, str]) -> subprocess.CompletedProcess[str]:
        command = [
            str(GRADLE_WRAPPER),
            "--no-daemon",
            "--console=plain",
            "-p",
            str(APP_ROOT),
            "help",
            *[f"-P{name}={value}" for name, value in sorted(properties.items())],
        ]
        environment = {**os.environ}
        java = shutil.which("java")
        if java is None:
            self.fail("A Java runtime is required for the Android Gradle property tests")
        environment.setdefault("JAVA_HOME", str(Path(java).resolve().parent.parent))
        return subprocess.run(
            command,
            cwd=APP_ROOT,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
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
        result = self.run_gradle(
            {
                "foundry_java_gradle_plugin": "not:a:coordinate:+",
                "foundry_java_gradle_plugin_kind": "maven",
                "foundry_java_maven_artifacts": "invalid",
            }
        )
        self.assertEqual(
            0,
            result.returncode,
            f"Ordinary Gradle configuration failed:\n{result.stdout}\n{result.stderr}",
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

    def test_duplicate_application_artifacts_fail_deterministically(self) -> None:
        properties = self.enabled_local_properties()
        properties["foundry_java_maven_artifacts"] = (
            "games.cafecito.foundry:foundry-java-runtime:1.0.0|"
            "games.cafecito.foundry:foundry-java-runtime:1.0.0"
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
        self.assert_gradle_failed_with(properties, "must use HTTP(S) or file URLs")

    def test_missing_local_application_artifact_fails_deterministically(self) -> None:
        properties = self.enabled_local_properties()
        properties["foundry_java_local_artifacts"] = str(self.workspace / "missing.jar")
        self.assert_gradle_failed_with(properties, "must be a regular file")

    def test_enabled_export_requires_an_application_artifact(self) -> None:
        properties = self.enabled_local_properties()
        del properties["foundry_java_local_artifacts"]
        self.assert_gradle_failed_with(properties, "requires at least one Maven or local application artifact")


if __name__ == "__main__":
    unittest.main()
