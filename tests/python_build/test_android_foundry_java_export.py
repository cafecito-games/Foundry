from __future__ import annotations

import http.server
import importlib.util
import os
import shutil
import signal
import subprocess
import tempfile
import textwrap
import threading
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
    maven_repository_a: Path
    maven_repository_z: Path
    network_probe: http.server.ThreadingHTTPServer
    network_probe_thread: threading.Thread
    network_requests: list[str]

    @classmethod
    def setUpClass(cls) -> None:
        cls.workspace_manager = tempfile.TemporaryDirectory(prefix="foundry-java-gradle-properties.")
        cls.workspace = Path(cls.workspace_manager.name)
        cls.plugin = cls.workspace / "plugin.jar"
        cls.artifact = cls.workspace / "module.jar"
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
    def tearDownClass(cls) -> None:
        cls.network_probe.shutdown()
        cls.network_probe.server_close()
        cls.network_probe_thread.join(timeout=5)
        cls.workspace_manager.cleanup()

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

        wrapper_result = subprocess.run(
            [str(GRADLE_WRAPPER), "--version"],
            cwd=APP_ROOT,
            check=False,
            capture_output=True,
            text=True,
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
        compile_result = subprocess.run(
            [
                javac,
                "-classpath",
                str(gradle_home / "lib/*"),
                "-d",
                str(classes_root),
                str(source_file),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if compile_result.returncode != 0:
            raise AssertionError(compile_result.stdout + compile_result.stderr)
        descriptor = classes_root / "META-INF/gradle-plugins/games.cafecito.foundry.java.properties"
        descriptor.parent.mkdir(parents=True)
        descriptor.write_text(
            "implementation-class=test.fixture.FoundryJavaFixturePlugin\n",
            encoding="utf-8",
        )
        jar_result = subprocess.run(
            [jar, "--create", "--file", str(cls.plugin), "-C", str(classes_root), "."],
            check=False,
            capture_output=True,
            text=True,
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
    ) -> subprocess.CompletedProcess[str]:
        command = [
            str(GRADLE_WRAPPER),
            "--no-daemon",
            "--console=plain",
            "-p",
            str(APP_ROOT),
            *tasks,
            *[f"-P{name}={value}" for name, value in sorted(properties.items())],
        ]
        environment = {**os.environ}
        java = shutil.which("java")
        if java is None:
            self.fail("A Java runtime is required for the Android Gradle property tests")
        environment.setdefault("JAVA_HOME", str(Path(java).resolve().parent.parent))
        process = subprocess.Popen(
            command,
            cwd=APP_ROOT,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        try:
            stdout, stderr = process.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                stdout, stderr = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                stdout, stderr = process.communicate()
            self.fail(
                f"Gradle did not terminate within 60 seconds and its process group was stopped:\n{stdout}\n{stderr}"
            )
        return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)

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
        result = self.run_gradle(
            {
                "foundry_java_gradle_plugin": "test.fixture:unreachable-plugin:1.0.0",
                "foundry_java_gradle_plugin_kind": "maven",
                "foundry_java_maven_repositories": f"http://127.0.0.1:{self.network_probe.server_port}/repository",
                "foundry_java_maven_artifacts": "invalid",
            },
            tasks=("tasks", "--all", "dependencies"),
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
        self.assert_gradle_failed_with(properties, "must be a regular file")

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
                {"foundry_java_local_artifacts": str(self.workspace / "missing.jar")},
                "must be a regular file",
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


if __name__ == "__main__":
    unittest.main()
