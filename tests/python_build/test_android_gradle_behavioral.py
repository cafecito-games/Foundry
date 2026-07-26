from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests.python_build.android_native_test_support import (
    ABI_SPECS,
    LIBRARIES,
    populate_native_matrix,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
JAVA_SOURCE = REPO_ROOT / "platform/android/java"
ANDROID_TOOLS = (
    "android_jni_contract.py",
    "android_native_contract.py",
    "android_native_staging.py",
    "android_source_template.py",
)
ALL_SCONS_ABIS = ("arm32", "arm64", "x86_32", "x86_64")
ALL_ANDROID_ABIS = tuple(sorted(ABI_SPECS))
ZERO_REVISION = "0" * 40


def find_java_home() -> Path:
    configured = os.environ.get("JAVA_HOME")
    if configured and (Path(configured) / "bin/java").is_file():
        return Path(configured)
    if sys.platform == "darwin":
        result = subprocess.run(
            ["/usr/libexec/java_home", "-v", "17+"],
            check=False,
            capture_output=True,
            text=True,
        )
        candidate = Path(result.stdout.strip())
        if result.returncode == 0 and (candidate / "bin/java").is_file():
            return candidate
    executable = shutil.which("java")
    if executable:
        candidate = Path(executable).resolve().parent.parent
        if (candidate / "bin/java").is_file():
            return candidate
    raise AssertionError("Android Gradle behavior tests require a usable JAVA_HOME")


def find_android_sdk() -> Path:
    for value in (
        os.environ.get("ANDROID_HOME"),
        os.environ.get("ANDROID_SDK_ROOT"),
        str(Path.home() / "Library/Android/sdk"),
        str(Path.home() / "Android/Sdk"),
    ):
        if value and (Path(value) / "platforms").is_dir():
            return Path(value)
    raise AssertionError("Android Gradle behavior tests require ANDROID_HOME or ANDROID_SDK_ROOT")


def scratch_parent() -> Path:
    configured = os.environ.get("FOUNDRY_TEST_SCRATCH")
    parent = Path(configured) if configured else REPO_ROOT / ".test_scratch"
    parent.mkdir(parents=True, exist_ok=True)
    return parent.resolve()


def copy_gradle_fixture(destination: Path) -> Path:
    repository = destination / "repo"
    java_root = repository / "platform/android/java"
    shutil.copytree(
        JAVA_SOURCE,
        java_root,
        ignore=shutil.ignore_patterns("build", ".gradle", ".kotlin", "libs"),
    )
    android_root = repository / "platform/android"
    for name in ANDROID_TOOLS:
        shutil.copy2(REPO_ROOT / "platform/android" / name, android_root / name)
    shutil.copy2(REPO_ROOT / "version.py", repository / "version.py")

    git_environment = {
        **os.environ,
        "GIT_AUTHOR_DATE": "2000-01-01T00:00:00+0000",
        "GIT_COMMITTER_DATE": "2000-01-01T00:00:00+0000",
    }
    subprocess.run(["git", "init", "-q"], cwd=repository, check=True, env=git_environment)
    subprocess.run(["git", "config", "user.name", "Foundry Tests"], cwd=repository, check=True)
    subprocess.run(["git", "config", "user.email", "tests@cafecito.games"], cwd=repository, check=True)
    subprocess.run(["git", "add", "version.py"], cwd=repository, check=True)
    subprocess.run(
        ["git", "commit", "-q", "-m", "Deterministic Gradle fixture"],
        cwd=repository,
        check=True,
        env=git_environment,
    )
    return java_root


def git_object(repository: Path, object_name: str) -> str:
    return subprocess.run(
        ["git", "rev-parse", object_name],
        cwd=repository,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def engine_version(repository: Path) -> str:
    values: dict[str, str] = {}
    required = ("major", "minor", "patch", "status", "module_config")
    for line in (repository / "version.py").read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        key, raw_value = (part.strip() for part in line.split("=", 1))
        if key in required:
            values[key] = raw_value.strip("\"'")
    return ".".join(values[key] for key in required if values.get(key)) or "custom_build"


def expected_stage(
    java_root: Path,
    *,
    revision: str,
    input_identity: str,
    selected_abis: tuple[str, ...],
    build_type: str = "debug",
) -> Path:
    digest = hashlib.sha256(f"{revision}\0{input_identity}\0{','.join(selected_abis)}".encode()).hexdigest()[:20]
    return java_root / "lib/build/android-native-stage" / revision / digest / "-".join(selected_abis) / build_type


def staged_libraries(stage: Path) -> list[str]:
    return sorted(path.relative_to(stage).as_posix() for path in stage.rglob("*.so"))


class AndroidGradleBehavioralTests(unittest.TestCase):
    workspace_manager: tempfile.TemporaryDirectory[str]
    workspace: Path
    repository: Path
    java_root: Path
    revision: str
    tree: str
    version: str
    java_home: Path
    android_sdk: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.workspace_manager = tempfile.TemporaryDirectory(
            prefix="android-gradle-behavioral.",
            dir=scratch_parent(),
        )
        cls.workspace = Path(cls.workspace_manager.name)
        cls.java_root = copy_gradle_fixture(cls.workspace)
        cls.repository = cls.workspace / "repo"
        cls.revision = git_object(cls.repository, "HEAD")
        cls.tree = git_object(cls.repository, "HEAD^{tree}")
        cls.version = engine_version(cls.repository)
        cls.java_home = find_java_home()
        cls.android_sdk = find_android_sdk()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.workspace_manager.cleanup()

    def run_gradle(
        self,
        *tasks: str,
        properties: dict[str, str] | None = None,
        path: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        command = [
            str(JAVA_SOURCE / "gradlew"),
            "--no-daemon",
            "--console=plain",
            "-p",
            str(self.java_root),
            *tasks,
            f"-PpythonExecutable={sys.executable}",
        ]
        for name, value in (properties or {}).items():
            command.append(f"-P{name}={value}")
        environment = {
            **os.environ,
            "ANDROID_HOME": str(self.android_sdk),
            "ANDROID_SDK_ROOT": str(self.android_sdk),
            "FOUNDRY_TEST_SCRATCH": str(self.workspace),
            "JAVA_HOME": str(self.java_home),
        }
        if path is not None:
            environment["PATH"] = path
        return subprocess.run(
            command,
            cwd=self.java_root,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )

    def create_native_root(self, name: str, suffix: bytes = b"") -> Path:
        native_root = self.workspace / name
        populate_native_matrix(
            native_root,
            revision=self.revision,
            tree=self.tree,
            payload_suffix=suffix,
        )
        return native_root

    def assert_gradle_succeeded(self, result: subprocess.CompletedProcess[str]) -> None:
        self.assertEqual(
            0,
            result.returncode,
            f"Gradle failed:\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )

    def test_native_root_stages_through_real_gradle(self) -> None:
        native_root = self.create_native_root("native-root", b":root")
        full_properties = {
            "foundryNativeRoot": str(native_root),
            "selectedAbis": ",".join(ALL_SCONS_ABIS),
        }

        root_result = self.run_gradle(":lib:stageFoundryNativeTemplateDebug", properties=full_properties)
        self.assert_gradle_succeeded(root_result)

        root_stage = expected_stage(
            self.java_root,
            revision=self.revision,
            input_identity=f"root:{native_root.resolve()}",
            selected_abis=ALL_SCONS_ABIS,
        )
        self.assertEqual(
            [f"{abi}/{library}" for abi in ALL_ANDROID_ABIS for library in sorted(LIBRARIES)],
            staged_libraries(root_stage),
        )

    def test_production_generation_rejects_incomplete_selected_abis(self) -> None:
        native_root = self.create_native_root("production-native-root", b":production")
        result = self.run_gradle(
            "generateFoundryTemplates",
            properties={
                "foundryNativeRoot": str(native_root),
                "selectedAbis": "arm64",
            },
        )

        self.assertNotEqual(0, result.returncode)
        self.assertIn(
            "Production template generation requires all four Android ABIs: arm32,arm64,x86_32,x86_64",
            result.stdout + result.stderr,
        )

    def test_missing_git_uses_zero_revision_during_real_gradle_configuration(self) -> None:
        no_git_path = self.workspace / "no-git-path"
        no_git_path.mkdir()
        for name in ("basename", "dirname", "javap", "sed", "sh", "uname", "xargs"):
            executable = self.java_home / "bin/javap" if name == "javap" else Path(shutil.which(name) or "")
            if not executable.is_file():
                self.fail(f"required Gradle wrapper utility is unavailable: {name}")
            (no_git_path / name).symlink_to(executable.resolve())
        self.assertIsNone(shutil.which("git", path=str(no_git_path)))

        result = self.run_gradle(
            ":lib:generateTemplateDebugBuildConfig",
            properties={"selectedAbis": ""},
            path=str(no_git_path),
        )
        self.assert_gradle_succeeded(result)
        build_config = (
            self.java_root
            / "lib/build/generated/source/buildConfig/template/debug/games/cafecito/foundry/BuildConfig.java"
        )
        self.assertIn(
            f'FOUNDRY_ENGINE_REVISION = "{ZERO_REVISION}"',
            build_config.read_text(encoding="utf-8"),
        )

        zero_root = self.workspace / "zero-native-root"
        populate_native_matrix(
            zero_root,
            revision=ZERO_REVISION,
            tree=ZERO_REVISION,
        )
        stage_result = self.run_gradle(
            ":lib:stageFoundryNativeTemplateDebug",
            properties={
                "foundryNativeRoot": str(zero_root),
                "selectedAbis": ",".join(ALL_SCONS_ABIS),
            },
            path=str(no_git_path),
        )
        self.assert_gradle_succeeded(stage_result)

    def test_changed_input_subset_uses_new_scope_without_stale_abis(self) -> None:
        first_root = self.create_native_root("first-native-root", b":first")
        second_root = self.create_native_root("second-native-root", b":second")
        first_selection = ALL_SCONS_ABIS
        second_selection = ("arm64",)

        first_result = self.run_gradle(
            ":lib:stageFoundryNativeTemplateDebug",
            properties={
                "foundryNativeRoot": str(first_root),
                "selectedAbis": ",".join(first_selection),
            },
        )
        self.assert_gradle_succeeded(first_result)
        second_result = self.run_gradle(
            ":lib:stageFoundryNativeTemplateDebug",
            properties={
                "foundryNativeRoot": str(second_root),
                "selectedAbis": ",".join(second_selection),
            },
        )
        self.assert_gradle_succeeded(second_result)

        first_stage = expected_stage(
            self.java_root,
            revision=self.revision,
            input_identity=f"root:{first_root.resolve()}",
            selected_abis=first_selection,
        )
        second_stage = expected_stage(
            self.java_root,
            revision=self.revision,
            input_identity=f"root:{second_root.resolve()}",
            selected_abis=second_selection,
        )
        self.assertNotEqual(first_stage, second_stage)
        self.assertEqual(
            [f"{abi}/{library}" for abi in ALL_ANDROID_ABIS for library in sorted(LIBRARIES)],
            staged_libraries(first_stage),
        )
        self.assertEqual(
            [f"arm64-v8a/{library}" for library in sorted(LIBRARIES)],
            staged_libraries(second_stage),
        )
        for library in LIBRARIES:
            self.assertTrue((second_stage / "arm64-v8a" / library).read_bytes().endswith(b":second"))


if __name__ == "__main__":
    unittest.main()
