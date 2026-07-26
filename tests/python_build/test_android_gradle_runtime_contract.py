from __future__ import annotations

import importlib.util
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
LIB_BUILD = JAVA_ROOT / "lib/build.gradle"
LIB_MANIFEST = JAVA_ROOT / "lib/src/main/AndroidManifest.xml"
LIB_JAVA = JAVA_ROOT / "lib/src/main/java"
LIB_AIDL = JAVA_ROOT / "lib/src/main/aidl"
LIB_RESOURCES = JAVA_ROOT / "lib/src/main/res"
LIB_TESTS = JAVA_ROOT / "lib/src/test"
LIB_ANDROID_TESTS = JAVA_ROOT / "lib/src/androidTest"
THIRDPARTY = JAVA_ROOT / "THIRDPARTY.md"
WRAPPER = JAVA_ROOT / "gradle/wrapper/gradle-wrapper.properties"
NATIVE_BUNDLE_TOOL = REPO_ROOT / "platform/android/android_native_bundle.py"
NATIVE_STAGING_TOOL = REPO_ROOT / "platform/android/android_native_staging.py"
SOURCE_TEMPLATE_TOOL = REPO_ROOT / "platform/android/android_source_template.py"
ANDROID_README = REPO_ROOT / "platform/android/README.md"
ANDROID_RUNTIME_DOC = REPO_ROOT / "platform/android/ANDROID_RUNTIME.md"
PRE_COMMIT = REPO_ROOT / ".pre-commit-config.yaml"
ACTIVE_GRADLE_FILES = (
    SETTINGS,
    ROOT_BUILD,
    APP_BUILD,
    APP_CONFIG,
    LIB_BUILD,
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
    def test_settings_include_the_internal_runtime_library(self) -> None:
        settings = read(SETTINGS)

        self.assertIn("include ':app'", settings)
        self.assertIn("include ':lib'", settings)
        self.assertIn("include ':nativeSrcsConfigs'", settings)
        self.assertIn("include ':assetPackInstallTime'", settings)
        self.assertNotIn("io.github.gradle-nexus.publish-plugin", settings)

    def test_root_build_assembles_the_internal_runtime(self) -> None:
        build = read(ROOT_BUILD)

        for fragment in (
            'dependsOn ":lib:assembleTemplate${capitalizedTarget}"',
            'from("lib/build/outputs/aar/foundry-${target}.aar")',
            'into("libs/${target}")',
            '"libs/**"',
            'dependsOn ":app:assemble${capitalizedEdition}${capitalizedTarget}"',
            "foundry-debug.aar",
            "foundry-dev.aar",
            "foundry-release.aar",
        ):
            self.assertIn(fragment, build)

    def test_app_consumes_internal_project_with_exported_template_fallback(self) -> None:
        app = read(APP_BUILD)

        self.assertIn('implementation project(":lib")', app)
        for build_type in ("debug", "dev", "release"):
            self.assertIn(f"{build_type}Implementation", app)
            self.assertIn(f"libs/{build_type}", app)
        self.assertNotIn('project(":godot:lib")', app)
        self.assertNotIn("foundryRuntimeAarRoot", app)
        self.assertNotIn("prepareFoundryAndroidRuntime", app)

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
        self.assertIn('into("libs/${target}")', build)
        self.assertNotIn('into("app/libs/${target}")', build)

    def test_apk_copy_tasks_freeze_and_require_each_variant_output(self) -> None:
        build = read(ROOT_BUILD)

        for fragment in (
            "return runtimeBuildTypes.collect { String target ->",
            'def sourceApk = file("app/build/outputs/apk/${edition}/${target}/android_${filenameSuffix}.apk")',
            'dependsOn ":app:assemble${capitalizedEdition}${capitalizedTarget}"',
            "inputs.file(sourceApk)",
            "from(sourceApk)",
        ):
            self.assertIn(fragment, build)
        self.assertNotIn('from("app/build/outputs/apk/${edition}/${target}")', build)

    def test_internal_library_build_preserves_runtime_compilation_contract(self) -> None:
        build = read(LIB_BUILD)

        for fragment in (
            "id 'com.android.library'",
            "id 'org.jetbrains.kotlin.android'",
            "namespace = 'games.cafecito.foundry'",
            "compileSdkVersion versions.compileSdk",
            "minSdkVersion versions.minSdk",
            "targetSdkVersion versions.targetSdk",
            "testInstrumentationRunner",
            "aidl = true",
            "buildConfig = true",
            "template {}",
            "abortOnError true",
            "testImplementation",
            "androidTestImplementation",
            "FOUNDRY_BINDINGS_VERSION",
            "FOUNDRY_ENGINE_VERSION",
            "FOUNDRY_ENGINE_REVISION",
            "FOUNDRY_JNI_CONTRACT_VERSION",
            '"swappy=yes"',
            "bin/android-native/${foundryEngineRevision}/${buildType}/${androidAbi}",
            "supportedAbis",
            "orElse(supportedAbis)",
            "androidNativeStage",
            "../android_native_staging.py",
        ):
            self.assertIn(fragment, build)
        for stale in (
            "debug.jniLibs.srcDirs = ['libs/debug']",
            "dev.jniLibs.srcDirs = ['libs/dev']",
            "release.jniLibs.srcDirs = ['libs/release']",
            'into("libs/${buildType}/${androidAbi}")',
        ):
            self.assertNotIn(stale, build)

    def test_internal_runtime_source_trees_are_complete(self) -> None:
        required = (
            LIB_MANIFEST,
            LIB_JAVA / "games/cafecito/foundry/Foundry.kt",
            LIB_JAVA / "games/cafecito/foundry/FoundryLib.java",
            LIB_JAVA / "games/cafecito/foundry/service/FoundryService.kt",
            LIB_AIDL / "com/android/vending/licensing/ILicenseResultListener.aidl",
            LIB_AIDL / "com/android/vending/licensing/ILicensingService.aidl",
            LIB_RESOURCES / "layout/foundry_app_layout.xml",
            LIB_RESOURCES / "values/strings.xml",
            LIB_RESOURCES / "xml/foundry_provider_paths.xml",
            LIB_TESTS / "java/games/cafecito/foundry/RuntimeIdentityTest.java",
            LIB_TESTS / "java/games/cafecito/foundry/plugin/FoundryPluginRegistryTest.java",
            LIB_ANDROID_TESTS / "java/games/cafecito/foundry/RuntimeIdentityInstrumentedTest.kt",
            LIB_ANDROID_TESTS / "java/games/cafecito/foundry/plugin/FoundryPluginProtocolInstrumentedTest.java",
        )
        missing = [str(path.relative_to(REPO_ROOT)) for path in required if not path.is_file()]
        self.assertEqual([], missing)

    def test_runtime_preserves_identity_api36_resources_and_jni_names(self) -> None:
        manifest = read(LIB_MANIFEST)
        for metadata in (
            "games.cafecito.foundry.library.version",
            "games.cafecito.foundry.engine.version",
            "games.cafecito.foundry.engine.revision",
            "games.cafecito.foundry.jni.contract",
            ".FoundryDownloaderAlarmReceiver",
        ):
            self.assertIn(metadata, manifest)

        downloader = read(LIB_JAVA / "com/google/android/vending/expansion/downloader/impl/DownloaderService.java")
        service = read(LIB_JAVA / "games/cafecito/foundry/service/FoundryService.kt")
        compat = read(LIB_JAVA / "games/cafecito/foundry/utils/AndroidRuntimeCompat.kt")
        self.assertIn("PendingIntent.FLAG_ONE_SHOT | PendingIntent.FLAG_IMMUTABLE", downloader)
        self.assertIn("Build.VERSION_CODES.BAKLAVA", service)
        self.assertIn("hostInputTransferToken != null", service)
        self.assertIn("@RequiresApi(Build.VERSION_CODES.R)", compat)
        self.assertIn('@SuppressLint("MissingPermission")', compat)

        for resource in (
            JAVA_ROOT / "lib/src/main/resources/META-INF/foundry/LICENSE.txt",
            JAVA_ROOT / "lib/src/main/resources/META-INF/foundry/LICENSES/Apache-2.0.txt",
            JAVA_ROOT / "lib/src/main/resources/META-INF/foundry/NOTICE",
        ):
            self.assertTrue(resource.is_file(), resource)

        declaration_exports = {
            "games/cafecito/foundry/FoundryLib.java": (
                "native boolean initialize(",
                "Java_games_cafecito_foundry_FoundryLib_initialize",
            ),
            "games/cafecito/foundry/plugin/FoundryPlugin.java": (
                "native boolean nativeRegisterSingleton(",
                "Java_games_cafecito_foundry_plugin_FoundryPlugin_nativeRegisterSingleton",
            ),
            "games/cafecito/foundry/utils/DialogUtils.kt": (
                "external fun dialogCallback(",
                "Java_games_cafecito_foundry_utils_DialogUtils_dialogCallback",
            ),
            "games/cafecito/foundry/variant/Callable.kt": (
                "external fun nativeCall(",
                "Java_games_cafecito_foundry_variant_Callable_nativeCall",
            ),
        }
        native_sources = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (REPO_ROOT / "platform/android").rglob("*")
            if path.is_file() and path.suffix in {".c", ".cc", ".cpp", ".h"} and "thirdparty" not in path.parts
        )
        for relative_path, (declaration, export) in declaration_exports.items():
            self.assertIn(declaration, read(LIB_JAVA / relative_path))
            self.assertIn(export, native_sources)

    def test_active_gradle_keeps_only_the_temporary_caller_bridge(self) -> None:
        active = "\n".join(read(path) for path in ACTIVE_GRADLE_FILES)

        for fragment in (
            "prepareFoundryAndroidRuntime",
            "foundryRuntimeAarRoot",
            "../android_runtime_build.py",
            "../foundry_android_runtime.json",
            "io.github.gradle-nexus.publish-plugin",
            "maven-publish",
            "MavenPublication",
            "publish-root.gradle",
            "publish-module.gradle",
            "nexusPublishing",
            "Foundry-Android",
        ):
            self.assertNotIn(fragment, active)
        for fragment in (
            "foundryAndroidSource",
            "foundryAndroidFetch",
            "foundryNativeRoot",
            "foundryNativeBundle",
            "foundryRuntimeScratch",
            "WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE",
        ):
            self.assertIn(fragment, active)
        self.assertTrue(NATIVE_BUNDLE_TOOL.is_file())
        self.assertTrue(NATIVE_STAGING_TOOL.is_file())

    def test_production_template_generation_requires_the_four_abi_matrix(self) -> None:
        root_build = read(ROOT_BUILD)
        library_build = read(LIB_BUILD)

        for fragment in (
            "generateFoundryTemplates",
            "generateFoundryMonoTemplates",
            "supportedAbis",
            "selectedAbis",
            "all four Android ABIs",
        ):
            self.assertIn(fragment, root_build)
        self.assertIn('def supportedAbis = ["arm32", "arm64", "x86_32", "x86_64"]', library_build)
        self.assertIn("orElse(supportedAbis)", library_build)

    def test_native_staging_is_revision_and_input_scoped(self) -> None:
        build = read(LIB_BUILD)

        for fragment in (
            "foundryEngineRevision",
            "foundryNativeInputKey",
            "selectedAbis",
            "build/android-native-stage",
            "--output",
        ):
            self.assertIn(fragment, build)
        self.assertNotIn('delete("libs/${buildType}/${androidAbi}")', build)

    def test_revision_fallback_handles_missing_git_executable(self) -> None:
        config = read(APP_CONFIG)

        self.assertIn("getFoundryEngineRevision", config)
        self.assertIn("try {", config)
        self.assertIn("catch (Exception ignored)", config)
        self.assertIn("0000000000000000000000000000000000000000", config)

    def test_third_party_provenance_describes_the_in_tree_sources_precisely(self) -> None:
        third_party = read(THIRDPARTY)

        for fragment in (
            "lib/src/main/java/com/google/android/vending/expansion/downloader",
            "lib/src/main/aidl/com/android/vending/licensing",
            "lib/src/main/java/com/google/android/vending/licensing",
            "Handler ownership leaks",
            "Foundry resource package",
            "locale-stable",
            "PendingIntent",
            "immutable",
            "asynchronous preference behavior",
            "debug-only runtime check",
        ):
            self.assertIn(fragment, third_party)
        for stale in (
            "lib/src/com/google",
            "lib/aidl/com/android",
            "yet unclear",
        ):
            self.assertNotIn(stale, third_party)

    def test_wrapper_verifies_the_gradle_distribution(self) -> None:
        wrapper = read(WRAPPER)

        self.assertIn(f"distributionSha256Sum={STANDALONE_WRAPPER_SHA256}", wrapper)
        self.assertIn("gradle-8.11.1-bin.zip", wrapper)

    def test_source_template_inspector_accepts_only_packaged_internal_aars(self) -> None:
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
            '"promote"',
            '"--archive"',
            '"--destination"',
            "androidSourceTemplate",
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

    def test_android_runtime_documentation_covers_internal_build_and_acceptance(self) -> None:
        readme = read(ANDROID_README)
        runtime_doc = read(ANDROID_RUNTIME_DOC)

        self.assertIn("ANDROID_RUNTIME.md", readme)
        for fragment in (
            "platform/android/java/lib",
            "internal",
            ":lib:testTemplateDebugUnitTest",
            ":lib:lintTemplateDebug",
            ":lib:assembleTemplateDebugAndroidTest",
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
            "not published to Maven",
            "device or emulator",
            "android_source_template.py inspect",
            "android_device_acceptance.py source-template",
            "android_device_acceptance.py verify-apks",
            "games.cafecito.foundry.game",
            "dev.example.foundryacceptance",
            "games.cafecito.foundry.plugin.v1.",
            "compiled Java/Kotlin native declarations",
            "libfoundry_android.so",
            "Acceptance evidence map",
            "JNI declarations/exports",
            "AAR identity/content",
            "APK identity/content",
            "Source ZIP",
            "Device runtime",
            "Editor exporter",
            "foundry-native.zip",
            "foundryNativeRoot",
            "foundryNativeBundle",
            "WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE",
            "fresh",
        ):
            self.assertIn(fragment, runtime_doc)
        for forbidden in (
            "Foundry-Android repository",
            "--source-repository",
            "--allow-fetch",
            "sole Maven publisher",
        ):
            self.assertNotIn(forbidden, runtime_doc)

    def test_pre_commit_routes_the_android_device_acceptance_surface(self) -> None:
        pre_commit = read(PRE_COMMIT)

        self.assertIn("- id: foundry-android-device-acceptance", pre_commit)
        self.assertIn("tests.python_build.test_android_device_acceptance", pre_commit)
        self.assertIn("tests.python_build.test_android_gradle_runtime_contract", pre_commit)
        for fragment in (
            "android_device_acceptance",
            "test_android_device_acceptance",
            "FoundryAppTest",
            "AndroidManifest",
            "platform/android/java/app/src/instrumented/assets/",
            "ANDROID_RUNTIME",
            "android_builds",
            "test_android_runtime_workflows",
        ):
            self.assertIn(fragment, pre_commit)


if __name__ == "__main__":
    unittest.main()
