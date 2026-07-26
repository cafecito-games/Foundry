from __future__ import annotations

import importlib.util
import re
import stat
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import ModuleType

REPO_ROOT = Path(__file__).resolve().parents[2]
JAVA_ROOT = REPO_ROOT / "platform/android/java"
SETTINGS = JAVA_ROOT / "settings.gradle"
ROOT_BUILD = JAVA_ROOT / "build.gradle"
APP_BUILD = JAVA_ROOT / "app/build.gradle"
APP_CONFIG = JAVA_ROOT / "app/config.gradle"
WRAPPER = JAVA_ROOT / "gradle/wrapper/gradle-wrapper.properties"
SOURCE_TEMPLATE_TOOL = REPO_ROOT / "platform/android/android_source_template.py"
ANDROID_README = REPO_ROOT / "platform/android/README.md"
ANDROID_RUNTIME_DOC = REPO_ROOT / "platform/android/ANDROID_RUNTIME.md"
ACTIVE_GRADLE_FILES = (
    SETTINGS,
    ROOT_BUILD,
    APP_BUILD,
    APP_CONFIG,
    JAVA_ROOT / "app/assetPackInstallTime/build.gradle",
    JAVA_ROOT / "nativeSrcsConfigs/build.gradle",
)
STANDALONE_WRAPPER_SHA256 = "f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6"
EXPECTED_SOURCE_AARS = {
    "libs/debug/foundry-debug.aar",
    "libs/dev/foundry-dev.aar",
    "libs/release/foundry-release.aar",
}
VALID_SOURCE_TEMPLATE = {
    "build.gradle": b"// app template\n",
    "config.gradle": b"// app config\n",
    "gradlew": b"#!/bin/sh\n",
    "gradle/wrapper/gradle-wrapper.jar": b"wrapper",
    "gradle/wrapper/gradle-wrapper.properties": b"distributionUrl=gradle\n",
    "src/main/AndroidManifest.xml": b"<manifest />\n",
    "src/main/java/games/cafecito/foundry/game/FoundryApp.java": b"package games.cafecito.foundry.game;\n",
    **{path: f"{path}\n".encode() for path in EXPECTED_SOURCE_AARS},
}


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing Android Gradle contract file: {path}")
    return path.read_text(encoding="utf-8")


def load_source_template_tool() -> ModuleType:
    if not SOURCE_TEMPLATE_TOOL.is_file():
        raise AssertionError(f"missing Android source-template inspector: {SOURCE_TEMPLATE_TOOL}")
    spec = importlib.util.spec_from_file_location("android_source_template", SOURCE_TEMPLATE_TOOL)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load Android source-template inspector: {SOURCE_TEMPLATE_TOOL}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def write_source_template(path: Path, entries: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        for name, contents in entries.items():
            archive.writestr(name, contents)


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

    def test_source_template_inspector_accepts_only_packaged_standalone_aars(self) -> None:
        tool = load_source_template_tool()
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "android_source.zip"
            write_source_template(archive, VALID_SOURCE_TEMPLATE)

            names = tool.inspect_source_template(archive)

        self.assertEqual(EXPECTED_SOURCE_AARS, {name for name in names if name.endswith(".aar")})

    def test_source_template_inspector_rejects_runtime_source_and_archive_drift(self) -> None:
        tool = load_source_template_tool()
        mutations = {
            "missing AAR": {
                name: contents for name, contents in VALID_SOURCE_TEMPLATE.items() if name != "libs/dev/foundry-dev.aar"
            },
            "unexpected AAR": {
                **VALID_SOURCE_TEMPLATE,
                "libs/debug/foundry-lib.template_debug.aar": b"stale",
            },
            "runtime source": {
                **VALID_SOURCE_TEMPLATE,
                "src/main/java/games/cafecito/foundry/Foundry.kt": b"package games.cafecito.foundry\n",
            },
            "unsafe path": {
                **VALID_SOURCE_TEMPLATE,
                "../runtime/Foundry.kt": b"unsafe",
            },
        }
        with tempfile.TemporaryDirectory() as temporary:
            for description, entries in mutations.items():
                with self.subTest(description=description):
                    archive = Path(temporary) / f"{description.replace(' ', '-')}.zip"
                    write_source_template(archive, entries)
                    with self.assertRaises(tool.SourceTemplateError):
                        tool.inspect_source_template(archive)

            symlink = Path(temporary) / "symlink.zip"
            with zipfile.ZipFile(symlink, "w") as archive_file:
                for name, contents in VALID_SOURCE_TEMPLATE.items():
                    archive_file.writestr(name, contents)
                entry = zipfile.ZipInfo("src/main/java/games/cafecito/foundry/game/Linked.java")
                entry.create_system = 3
                entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                archive_file.writestr(entry, "FoundryApp.java")
            with self.assertRaises(tool.SourceTemplateError):
                tool.inspect_source_template(symlink)

            archive_target = Path(temporary) / "target.zip"
            archive_link = Path(temporary) / "archive-link.zip"
            write_source_template(archive_target, VALID_SOURCE_TEMPLATE)
            archive_link.symlink_to(archive_target)
            with self.assertRaises(tool.SourceTemplateError):
                tool.inspect_source_template(archive_link)

    def test_source_template_promotion_is_fail_closed_and_atomic(self) -> None:
        tool = load_source_template_tool()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "candidate.zip"
            destination = root / "bin/android_source.zip"
            destination.parent.mkdir()
            destination.write_bytes(b"stale")
            write_source_template(archive, {"build.gradle": b"incomplete"})

            with self.assertRaises(tool.SourceTemplateError):
                tool.promote_source_template(archive, destination)
            self.assertFalse(destination.exists())

            write_source_template(archive, VALID_SOURCE_TEMPLATE)
            tool.promote_source_template(archive, destination)
            self.assertEqual(archive.read_bytes(), destination.read_bytes())

    def test_gradle_inspects_staged_source_template_before_promotion(self) -> None:
        build = read(ROOT_BUILD)

        for fragment in (
            "../android_source_template.py",
            "'promote'",
            "'--archive'",
            "'--destination'",
            "foundryAndroidSourceTemplate",
            'tasks.register("clearAndroidSourceTemplateOutput", Delete)',
            "mustRunAfter clearAndroidSourceTemplateOutput",
            'tasks.register("promoteAndroidSourceTemplate", Exec)',
            "mustRunAfter standardBuildTasks",
            "mustRunAfter monoBuildTasks",
            "promoteStandardAndroidSourceTemplate",
            "promoteMonoAndroidSourceTemplate",
        ):
            self.assertIn(fragment, build)
        self.assertNotIn("destinationDirectory = binDir", build)
        self.assertNotIn("finalizedBy zipGradleBuild", build)

    def test_android_runtime_documentation_covers_authoritative_and_offline_flows(self) -> None:
        readme = read(ANDROID_README)
        runtime_doc = read(ANDROID_RUNTIME_DOC)

        self.assertIn("ANDROID_RUNTIME.md", readme)
        pin = read(REPO_ROOT / "platform/android/foundry_android_runtime.json")
        for fragment in (
            "platform/android/foundry_android_runtime.json",
            "https://github.com/cafecito-games/Foundry-Android.git",
            '"revision":',
            '"tree":',
            "--source-repository",
            "--allow-fetch",
            "--native-root",
            "--native-bundle",
            "--scratch",
            "--output-aars",
            "production=no dev_mode=no dev_build=no debug_symbols=no",
            "production=no dev_mode=yes dev_build=yes debug_symbols=yes",
            "production=yes dev_mode=no dev_build=no debug_symbols=no",
            "armeabi-v7a",
            "arm64-v8a",
            "x86_32",
            "x86_64",
            "android_debug.apk",
            "android_dev.apk",
            "android_release.apk",
            "android_source.zip",
            "Foundry-Android is the sole Maven publisher",
            "clean Foundry checkout",
            "provenance.json",
            "offline",
            "non-authoritative",
            "#1224",
            "device or emulator",
            "android_source_template.py inspect",
        ):
            self.assertIn(fragment, runtime_doc)
        self.assertIn('"revision":', pin)


if __name__ == "__main__":
    unittest.main()
