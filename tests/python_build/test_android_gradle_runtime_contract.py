from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
JAVA_ROOT = REPO_ROOT / "platform/android/java"
SETTINGS = JAVA_ROOT / "settings.gradle"
ROOT_BUILD = JAVA_ROOT / "build.gradle"
APP_BUILD = JAVA_ROOT / "app/build.gradle"
APP_CONFIG = JAVA_ROOT / "app/config.gradle"
WRAPPER = JAVA_ROOT / "gradle/wrapper/gradle-wrapper.properties"
ACTIVE_GRADLE_FILES = (
    SETTINGS,
    ROOT_BUILD,
    APP_BUILD,
    APP_CONFIG,
    JAVA_ROOT / "app/assetPackInstallTime/build.gradle",
    JAVA_ROOT / "nativeSrcsConfigs/build.gradle",
)
STANDALONE_WRAPPER_SHA256 = "f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing Android Gradle contract file: {path}")
    return path.read_text(encoding="utf-8")


class AndroidGradleRuntimeContractTests(unittest.TestCase):
    def test_settings_keep_application_and_native_ide_modules_without_runtime_library(self) -> None:
        settings = read(SETTINGS)

        self.assertIn("include ':app'", settings)
        self.assertIn("include ':nativeSrcsConfigs'", settings)
        self.assertIn("include ':assetPackInstallTime'", settings)
        self.assertNotIn("include ':lib'", settings)
        self.assertNotIn("io.github.gradle-nexus.publish-plugin", settings)

    def test_root_build_prepares_one_explicit_standalone_runtime(self) -> None:
        build = read(ROOT_BUILD)

        self.assertIn('tasks.register("prepareFoundryAndroidRuntime", Exec)', build)
        for fragment in (
            "../android_runtime_build.py",
            "../android_runtime_contract.py",
            "../foundry_android_runtime.json",
            "'prepare'",
            "'--engine-source'",
            "'--scratch'",
            "'--output-aars'",
            "'--source-repository'",
            "'--allow-fetch'",
            "'--native-root'",
            "'--native-bundle'",
            "foundryAndroidSource",
            "foundryAndroidFetch",
            "foundryNativeRoot",
            "foundryNativeBundle",
            "foundryRuntimeScratch",
            "inputs.file",
            "outputs.dir",
        ):
            self.assertIn(fragment, build)

        self.assertRegex(
            build,
            r"generateFoundryTemplates\s*\{[^}]*dependsOn\s+prepareFoundryAndroidRuntime",
        )
        self.assertRegex(
            build,
            r"generateFoundryMonoTemplates\s*\{[^}]*dependsOn\s+prepareFoundryAndroidRuntime",
        )
        self.assertIn("exactly one standalone source", build)
        self.assertIn("exactly one native root or bundle", build)
        self.assertIn("def hasStandaloneSource = !sourceValue.isEmpty()", build)
        self.assertIn("if (hasStandaloneSource == fetchValue)", build)

    def test_app_variants_consume_generated_or_packaged_standalone_aars(self) -> None:
        app = read(APP_BUILD)

        self.assertIn("foundryRuntimeAarRoot", app)
        for build_type in ("debug", "dev", "release"):
            self.assertIn(f"{build_type}Implementation", app)
            self.assertIn(f"foundryRuntimeAarRoot/{build_type}", app)
        self.assertIn("prepareFoundryAndroidRuntime", app)
        self.assertRegex(app, r"task\.name\.startsWith\(\"assemble\"\)")
        self.assertRegex(app, r"task\.name\.startsWith\(\"merge\"\)")
        self.assertNotIn('project(":lib")', app)
        self.assertNotIn('project(":godot:lib")', app)
        self.assertNotIn("copyDebugAARToAppModule", app)

    def test_template_and_diagnostic_artifact_names_stay_stable(self) -> None:
        build = read(ROOT_BUILD)

        for filename in (
            "android_debug.apk",
            "android_dev.apk",
            "android_release.apk",
            "android_source.zip",
            "foundry-debug.aar",
            "foundry-dev.aar",
            "foundry-release.aar",
        ):
            self.assertIn(filename, build)
        self.assertIn('into "libs"', build)

    def test_active_gradle_has_no_in_tree_runtime_or_publication_logic(self) -> None:
        active = "\n".join(read(path) for path in ACTIVE_GRADLE_FILES)

        for fragment in (
            "../../../version.py",
            "getFoundryPublishVersion",
            "generateFoundryLibraryVersion",
            "lib/libs",
            "compileFoundryNativeLibs",
            "generateNativeLibs",
            "io.github.gradle-nexus.publish-plugin",
            "publish-root.gradle",
            "publish-module.gradle",
            "nexusPublishing",
        ):
            self.assertNotIn(fragment, active)
        self.assertIsNone(re.search(r"executable\s+.*scons", active, re.IGNORECASE))

    def test_wrapper_verifies_the_pinned_standalone_distribution(self) -> None:
        wrapper = read(WRAPPER)

        self.assertIn(f"distributionSha256Sum={STANDALONE_WRAPPER_SHA256}", wrapper)
        self.assertIn("gradle-8.11.1-bin.zip", wrapper)


if __name__ == "__main__":
    unittest.main()
