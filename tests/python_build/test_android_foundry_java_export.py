from __future__ import annotations

import http.server
import importlib.util
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import threading
import unittest
import zipfile
from pathlib import Path
from types import ModuleType

from tests.python_build.android_native_test_support import populate_native_matrix
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
GRADLE_WRAPPER = REPO_ROOT / "platform/android/java/gradlew"
JAVA_ROOT = REPO_ROOT / "platform/android/java"
APP_ROOT = REPO_ROOT / "platform/android/java/app"
INTEGRATION_FIXTURE = REPO_ROOT / "tests/fixtures/android_foundry_java"
ANDROID_RUNTIME_GUIDE = REPO_ROOT / "platform/android/ANDROID_RUNTIME.md"
ANDROID_EXPORT_CLASS_REFERENCE = REPO_ROOT / "platform/android/doc_classes/EditorExportPlatformAndroid.xml"
PRE_COMMIT_CONFIG = REPO_ROOT / ".pre-commit-config.yaml"
EXACT_FOUNDRY_JAVA_COMMIT = "7eb98b37845b42ff67f3da1427bd78ebef19668f"
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


class FoundryJavaDocumentationTests(unittest.TestCase):
    def test_runtime_guide_documents_the_complete_opt_in_contract(self) -> None:
        guide = ANDROID_RUNTIME_GUIDE.read_text(encoding="utf-8")
        required_fragments = (
            "## Optional Foundry-Java extensions",
            "gradle_build/foundry_java/enabled",
            "gradle_build/foundry_java/gradle_plugin_maven",
            "gradle_build/foundry_java/gradle_plugin_local",
            "gradle_build/foundry_java/maven_repositories",
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

    def test_pre_commit_runs_the_contract_on_every_owned_surface(self) -> None:
        config = PRE_COMMIT_CONFIG.read_text(encoding="utf-8")
        self.assertIn("- id: foundry-java-android-export", config)
        self.assertIn("tests.python_build.test_android_foundry_java_export", config)
        required_scopes = (
            r"\.pre-commit-config\.yaml",
            r"platform/android/ANDROID_RUNTIME\.md",
            r"platform/android/android_source_template\.py",
            r"platform/android/doc_classes/EditorExportPlatformAndroid\.xml",
            r"platform/android/export/export_plugin\.(?:cpp|h)",
            r"platform/android/java/app/(?:build|config)\.gradle",
            r"tests/fixtures/android_foundry_java/.*",
            r"tests/python_build/test_android_foundry_java_export\.py",
        )
        for scope in required_scopes:
            with self.subTest(scope=scope):
                self.assertIn(scope, config)


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
        self.assertEqual(64, len(evidence["configuration_sha256"]))
        self.assertEqual(64, len(evidence["registry_index_sha256"]))

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
                (valid[0], valid[2]),
                ("arm64-v8a",),
                "exactly one assets/foundry_java/registry-index-v2.txt",
            ),
            (
                "missing-bridge.apk",
                valid[:2],
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

    def test_enabled_inspection_rejects_duplicate_fixed_entries(self) -> None:
        apk = self.workspace / "duplicates.apk"
        with self.assertWarns(UserWarning):
            with zipfile.ZipFile(apk, "w") as archive:
                archive.writestr("assets/FoundryJava.foundryextension", "first")
                archive.writestr("assets/FoundryJava.foundryextension", "second")
                archive.writestr("assets/foundry_java/registry-index-v2.txt", "index")
                archive.writestr("lib/arm64-v8a/libfoundry_java.so", "bridge")
        with self.assertRaisesRegex(
            self.tool.AcceptanceError,
            "exactly one assets/FoundryJava.foundryextension",
        ):
            self.tool.inspect_foundry_java_apk(
                apk,
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
        self.assert_gradle_failed_with(properties, "must use HTTP(S) or file URLs")

    def test_missing_local_application_artifact_fails_deterministically(self) -> None:
        properties = self.enabled_local_properties()
        properties["foundry_java_local_artifacts"] = str(self.workspace / "missing.jar")
        self.assert_gradle_failed_with(properties, "must be a regular .jar or .aar file")

    def test_enabled_export_requires_an_application_artifact(self) -> None:
        properties = self.enabled_local_properties()
        del properties["foundry_java_local_artifacts"]
        self.assert_gradle_failed_with(properties, "requires at least one Maven or local application artifact")

    def test_every_malformed_application_input_fails_before_plugin_network_resolution(self) -> None:
        probe_url = f"http://127.0.0.1:{self.network_probe.server_port}/repository"
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
                "must use HTTP(S) or file URLs",
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
    binding_aar: Path
    module_jar: Path
    host_aar: Path

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
        cls.binding_aar = cls._only_artifact("foundry-java-android/build/outputs/aar/foundry-java-android-release.aar")
        cls.module_jar = cls._compile_module_fixture()
        cls.host_aar = cls._build_host_aar()

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
        sources = sorted((INTEGRATION_FIXTURE / "module/src/main/java").rglob("*.java"))
        result = run_bounded_subprocess(
            [
                str(cls.java_home / "bin/javac"),
                "--release",
                "17",
                "-classpath",
                str(cls.runtime_jar),
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
        entries: list[tuple[str, Path]] = []
        for root in (
            classes,
            INTEGRATION_FIXTURE / "module/src/main/resources",
        ):
            entries.extend((path.relative_to(root).as_posix(), path) for path in root.rglob("*") if path.is_file())
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for name, source in sorted(entries):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                output.writestr(info, source.read_bytes())
        return archive

    @classmethod
    def _build_host_aar(cls) -> Path:
        fixture_root = cls.workspace / "host-aar"
        java_root = copy_gradle_fixture(fixture_root)
        repository = fixture_root / "repo"
        revision = git_object(repository, "HEAD")
        tree = git_object(repository, "HEAD^{tree}")
        native_root = fixture_root / "native"
        populate_native_matrix(native_root, revision=revision, tree=tree)
        result = run_bounded_subprocess(
            [
                str(GRADLE_WRAPPER),
                "--no-daemon",
                "--console=plain",
                "-p",
                str(java_root),
                ":lib:assembleTemplateDebug",
                f"-PpythonExecutable={sys.executable}",
                f"-PfoundryNativeRoot={native_root}",
                "-PselectedAbis=arm64",
            ],
            cwd=java_root,
            environment={
                **cls._environment(),
                "FOUNDRY_TEST_SCRATCH": str(cls.workspace),
            },
            timeout=900,
        )
        if result.returncode != 0:
            raise AssertionError(f"Foundry host AAR build failed:\n{result.stdout}\n{result.stderr}")
        host_aar = java_root / "lib/build/outputs/aar/foundry-debug.aar"
        if not host_aar.is_file():
            raise AssertionError(f"Foundry host AAR was not produced: {host_aar}")
        return host_aar

    def _prepare_app(self, name: str) -> Path:
        app = self.workspace / name
        shutil.copytree(
            APP_ROOT,
            app,
            ignore=shutil.ignore_patterns("build", ".gradle", ".kotlin", "libs"),
        )
        shutil.copy2(GRADLE_WRAPPER, app / "gradlew")
        shutil.copytree(JAVA_ROOT / "gradle", app / "gradle")
        debug_libs = app / "libs/debug"
        debug_libs.mkdir(parents=True)
        shutil.copy2(self.host_aar, debug_libs / "foundry-debug.aar")
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

    def _assert_debug_outputs(
        self,
        app: Path,
        first: subprocess.CompletedProcess[str],
        second: subprocess.CompletedProcess[str],
    ) -> None:
        first_output = first.stdout + first.stderr
        second_output = second.stdout + second.stderr
        self.assertEqual(0, first.returncode, first_output)
        self.assertEqual(0, second.returncode, second_output)
        self.assertIn("Reusing configuration cache.", second_output)

        generated = app / "build/generated/assets/generateStandardDebugFoundryJavaRegistry"
        index = generated / "foundry_java/registry-index-v2.txt"
        configuration = generated / "FoundryJava.foundryextension"
        bootstrap = (
            app
            / "build/generated/java/generateStandardDebugFoundryJavaRegistry"
            / "games/cafecito/foundry/generated/FoundryGeneratedBootstrap.java"
        )
        self.assertIn("module=demo|example.DemoExtension", index.read_text(encoding="utf-8"))
        self.assertIn("example.DemoExtension.PROVIDER", bootstrap.read_text(encoding="utf-8"))
        with zipfile.ZipFile(self.binding_aar) as binding:
            self.assertEqual(binding.read("FoundryJava.foundryextension"), configuration.read_bytes())

        apk = app / "build/outputs/apk/standard/debug/android_debug.apk"
        with zipfile.ZipFile(apk) as archive:
            names = archive.namelist()
            self.assertEqual(
                ["assets/FoundryJava.foundryextension"],
                sorted(name for name in names if name.endswith("FoundryJava.foundryextension")),
            )
            self.assertIn("assets/foundry_java/registry-index-v2.txt", names)
            self.assertEqual(
                ["lib/arm64-v8a/libfoundry_java.so"],
                sorted(name for name in names if name.endswith("/libfoundry_java.so")),
            )
            self.assertEqual(
                ["lib/arm64-v8a/libfoundry_android.so"],
                sorted(name for name in names if name.endswith("/libfoundry_android.so")),
            )

    def _build_twice(
        self,
        app: Path,
        properties: dict[str, str],
    ) -> tuple[subprocess.CompletedProcess[str], subprocess.CompletedProcess[str]]:
        first = self._run_app(app, properties, "assembleStandardDebug")
        if first.returncode != 0:
            self.fail(first.stdout + first.stderr)
        generated = app / "build/generated/assets/generateStandardDebugFoundryJavaRegistry"
        before = {
            path.relative_to(generated).as_posix(): path.read_bytes() for path in generated.rglob("*") if path.is_file()
        }
        second = self._run_app(app, properties, "assembleStandardDebug")
        after = {
            path.relative_to(generated).as_posix(): path.read_bytes() for path in generated.rglob("*") if path.is_file()
        }
        self.assertEqual(before, after)
        return first, second

    def test_exact_local_inputs(self) -> None:
        app = self._prepare_app("local-app")
        properties = {
            "export_enabled_abis": "arm64-v8a",
            "foundry_java_gradle_plugin": str(self.plugin_jar),
            "foundry_java_gradle_plugin_kind": "local",
            "foundry_java_local_artifacts": "|".join(
                (str(self.binding_aar), str(self.runtime_jar), str(self.module_jar))
            ),
            "foundry_java_registry_marker": "registry-index-v2",
        }
        first, second = self._build_twice(app, properties)
        self._assert_debug_outputs(app, first, second)

    def test_exact_staged_maven_inputs(self) -> None:
        repository, marker = self._stage_maven_graph()
        app = self._prepare_app("maven-app")
        properties = {
            "export_enabled_abis": "arm64-v8a",
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
        first, second = self._build_twice(app, properties)
        self._assert_debug_outputs(app, first, second)


class FoundryJavaExporterContractTests(unittest.TestCase):
    @staticmethod
    def _development_binary() -> Path:
        binaries = sorted((REPO_ROOT / "bin").glob("foundry.*editor.dev*"))
        if not binaries:
            raise unittest.SkipTest("A development editor binary is required for command-first export validation")
        return binaries[0]

    @staticmethod
    def _write_export_preset(
        project: Path,
        *,
        plugin_local: Path | None = None,
        local_artifacts: tuple[Path, ...] = (),
        use_gradle: bool,
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

    def _run_export(self, project: Path) -> subprocess.CompletedProcess[str]:
        return run_bounded_subprocess(
            [
                str(self._development_binary()),
                "--headless",
                "project",
                "export",
                "--project",
                str(project),
                "--preset",
                "Android",
                "--output",
                str(project / "should-not-exist.apk"),
            ],
            cwd=project,
            timeout=30,
        )

    def test_preflight_names_every_fail_closed_boundary(self) -> None:
        exporter = EXPORTER.read_text(encoding="utf-8")
        for fragment in (
            "Foundry-Java requires a Gradle Android export.",
            "Foundry-Java Maven coordinates must be exact group:artifact:version values.",
            "Foundry-Java Maven repositories must use HTTP(S) or file URLs.",
            "Foundry-Java local artifacts must be regular .jar or .aar files.",
            "Foundry-Java local artifact paths must not traverse a symbolic link.",
            "Foundry-Java values must not contain carriage returns, newlines, or '|'.",
            "Foundry-Java exports must select exactly one Maven or local Gradle plugin.",
            "Foundry-Java exports require at least one Maven or local application artifact.",
            "Foundry-Java local artifacts must not contain libfoundry_android.so",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, exporter)

    def test_properties_are_emitted_only_inside_the_enabled_branch(self) -> None:
        exporter = EXPORTER.read_text(encoding="utf-8")
        marker = 'cmdline.push_back("-Pfoundry_java_registry_marker=registry-index-v2")'
        enabled_branch = exporter.find("if (foundry_java.enabled)")
        marker_position = exporter.find(marker)
        build_execution = exporter.find("execute_and_show_output", marker_position)
        self.assertGreaterEqual(enabled_branch, 0)
        self.assertGreater(marker_position, enabled_branch)
        self.assertGreater(build_execution, marker_position)

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
            self.assertIn("Foundry-Java requires a Gradle Android export.", output)
            self.assertFalse(output_path.exists())
            self.assertNotIn("Starting a Gradle Daemon", output)

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

            cases = (
                ((host_archives[0],), "must not contain libfoundry_android.so"),
                ((host_archives[1],), "must not contain libfoundry_android.so"),
                ((linked_module,), "must not traverse a symbolic link"),
                ((linked_directory / parent_module.name,), "must not traverse a symbolic link"),
                ((module, project / "sub/../module.jar"), "must be non-empty and unique"),
            )
            for local_artifacts, message in cases:
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
                    self.assertIn(message, output)
                    self.assertEqual(1, output.count(message), output)
                    self.assertFalse((project / "should-not-exist.apk").exists())
                    self.assertNotIn("Starting a Gradle Daemon", output)


if __name__ == "__main__":
    unittest.main()
