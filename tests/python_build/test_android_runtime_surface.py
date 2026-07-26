from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
failures: list[str] = []


def source(path: str) -> str:
    candidate = ROOT / path
    if not candidate.is_file():
        failures.append(f"required file is missing: {path}")
        return ""
    return candidate.read_text(encoding="utf-8")


def require_path(path: str) -> None:
    if not (ROOT / path).exists():
        failures.append(f"required Android runtime path is missing: {path}")


def forbid_path(path: str) -> None:
    if (ROOT / path).exists():
        failures.append(f"removed Android path still exists: {path}")


def require_text(path: str, *fragments: str) -> None:
    contents = source(path)
    for fragment in fragments:
        if fragment not in contents:
            failures.append(f"{path} must preserve {fragment!r}")


def forbid_text(path: str, *fragments: str) -> None:
    contents = source(path)
    for fragment in fragments:
        if fragment in contents:
            failures.append(f"{path} still contains removed Android surface {fragment!r}")


def forbid_text_in_section(path: str, start: str, end: str, *fragments: str) -> None:
    contents = source(path)
    if start not in contents or end not in contents:
        failures.append(f"{path} must contain section boundaries {start!r} and {end!r}")
        return
    section = contents.split(start, 1)[1].split(end, 1)[0]
    for fragment in fragments:
        if fragment in section:
            failures.append(f"{path} section {start.strip()!r} still contains removed Android surface {fragment!r}")


# Foundry owns its native producer, application template, and Java/Kotlin/AIDL
# host runtime. Publication remains outside the engine repository.
for path in (
    "platform/android/java/scripts/publish-module.gradle",
    "platform/android/java/scripts/publish-root.gradle",
    "platform/android/java/PUBLISHING.md",
):
    forbid_path(path)

for path in (
    "platform/android/java/lib/build.gradle",
    "platform/android/java/lib/src/main/AndroidManifest.xml",
    "platform/android/java/lib/src/main/aidl/com/android/vending/licensing/ILicenseResultListener.aidl",
    "platform/android/java/lib/src/main/aidl/com/android/vending/licensing/ILicensingService.aidl",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryHost.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryLib.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/service/FoundryService.kt",
    "platform/android/java/lib/src/main/res/layout/foundry_app_layout.xml",
    "platform/android/java/lib/src/main/res/values/strings.xml",
    "platform/android/java/lib/src/main/res/xml/foundry_provider_paths.xml",
    "platform/android/java/lib/src/main/resources/META-INF/foundry/LICENSE.txt",
    "platform/android/java/lib/src/main/resources/META-INF/foundry/NOTICE",
    "platform/android/java/lib/src/test/java/games/cafecito/foundry/RuntimeIdentityTest.java",
    "platform/android/java/lib/src/test/java/games/cafecito/foundry/utils/CommandLineFileParserTest.kt",
    "platform/android/java/lib/src/androidTest/java/games/cafecito/foundry/RuntimeIdentityInstrumentedTest.kt",
    "platform/android/java/app/src/instrumented/java/games/cafecito/foundry/game/test/FoundryAppInstrumentedTestBridge.java",
):
    require_path(path)

# The legacy Android plugin model is not part of the in-tree host runtime.
for path in (
    "platform/android/plugin/foundry_plugin_jni.h",
    "platform/android/plugin/foundry_plugin_jni.cpp",
    "platform/android/api/jni_singleton.h",
    "doc/classes/JNISingleton.xml",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/FoundryPlugin.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/FoundryPluginRegistry.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/UsedByFoundry.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/SignalInfo.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/AndroidRuntimePlugin.kt",
    "platform/android/java/lib/src/test/java/games/cafecito/foundry/plugin/FoundryPluginRegistryTest.java",
    "platform/android/java/lib/src/androidTest/java/games/cafecito/foundry/plugin/FoundryPluginProtocolInstrumentedTest.java",
    "platform/android/java/app/src/instrumented/java/games/cafecito/foundry/game/test/FoundryAppInstrumentedTestPlugin.kt",
):
    forbid_path(path)

for path in (
    "platform/android/SCsub",
    "platform/android/api/api.cpp",
    "platform/android/java_foundry_lib_jni.cpp",
    "platform/android/export/export_plugin.cpp",
    "platform/android/java/app/build.gradle",
    "platform/android/java/app/config.gradle",
    "platform/android/java/app/src/instrumented/AndroidManifest.xml",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryHost.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryFragment.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/gl/FoundryRenderer.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/vulkan/VkRenderer.kt",
):
    forbid_text(
        path,
        "FoundryPlugin",
        "FoundryPluginRegistry",
        "AndroidRuntimePlugin",
        "games.cafecito.foundry.plugin.v1.",
        "foundry_plugin_jni",
        "plugins_maven_repos",
        "plugins_remote_binaries",
        "plugins_local_binaries",
        "getFoundryPluginsMavenRepos",
        "getFoundryPluginsRemoteBinaries",
        "getFoundryPluginsLocalBinaries",
        "-keep class games.cafecito.foundry.plugin.**",
    )

# Removed on-device editor application and native host surfaces.
for path in (
    "platform/android/java/editor",
    "platform/android/editor",
    "platform/android/export/android_editor_gradle_runner.cpp",
    "platform/android/export/android_editor_gradle_runner.h",
    "editor/gui/touch_actions_panel.cpp",
    "editor/gui/touch_actions_panel.h",
):
    forbid_path(path)

forbid_text("platform/android/java/settings.gradle", "include ':editor'")
forbid_text(
    "platform/android/java/build.gradle",
    '"editor"',
    '"horizonos"',
    '"picoos"',
    "androidEditorBuildsDir",
    "generateFoundryEditor",
    "generateFoundryHorizonOSEditor",
    "generateFoundryPicoOSEditor",
    "cleanFoundryEditor",
    "android-editor-",
    ":editor:",
    "prepareFoundryAndroidRuntime",
    "../android_runtime_build.py",
    "../foundry_android_runtime.json",
)
require_text(
    "platform/android/java/build.gradle",
    'dependsOn ":lib:assembleTemplate${capitalizedTarget}"',
)
forbid_text(
    "platform/android/java/build.gradle",
    "foundryAndroidSource",
    "foundryAndroidFetch",
    "foundryNativeBundle",
    "foundryRuntimeScratch",
    "WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE",
)
forbid_text(
    "platform/android/java/lib/build.gradle",
    "maven-publish",
    "signing",
    "MavenPublication",
    "publishing {",
    "compatibility/foundry-engine.json",
    "Foundry-Android",
)
forbid_text(
    "platform/android/platform_android_builders.py",
    'env["target"] == "editor"',
    "generateFoundryEditor",
    "generateFoundryHorizonOSEditor",
    "generateFoundryPicoOSEditor",
)
forbid_text(
    "platform/android/SCsub",
    "editor/game_menu_utils_jni.cpp",
    "editor/editor_utils_jni.cpp",
    'lib_tools_dir = "tools/"',
    "android-editor-",
    "#platform/android/java/lib/libs/",
)
require_text(
    "platform/android/SCsub",
    "#bin/android-native/",
    "provenance.json",
    "write_android_native_provenance",
)
forbid_text("platform/android/detect.py", "store_release")
require_text("platform/android/detect.py", "Android does not support target=editor")

forbid_text(
    "platform/android/java_foundry_wrapper.cpp",
    "nativeBeginBenchmarkMeasure",
    "nativeEndBenchmarkMeasure",
    "nativeDumpBenchmark",
    "nativeSignApk",
    "nativeVerifyApk",
    "nativeOnEditorWorkspaceSelected",
    "nativeBuildEnvConnect",
    "nativeBuildEnvDisconnect",
    "nativeBuildEnvExecute",
    "nativeBuildEnvCancel",
    "nativeBuildEnvCleanProject",
)
forbid_text(
    "platform/android/java_foundry_lib_jni.cpp",
    "FoundryLib_getEditorSetting",
    "FoundryLib_setEditorSetting",
    "FoundryLib_getEditorProjectMetadata",
    "FoundryLib_setEditorProjectMetadata",
    "FoundryLib_isEditorHint",
    "FoundryLib_isProjectManagerHint",
    "editor/settings/editor_settings.h",
)
forbid_text(
    "platform/android/export/export_plugin.cpp",
    "AndroidEditorGradleRunner",
    "android_editor_gradle_runner",
    "OS_Android::get_singleton()->sign_apk",
    "OS_Android::get_singleton()->verify_apk",
    "_copy_keystore_to_temp",
)
forbid_text("platform/android/os_android.cpp", "#ifdef TOOLS_ENABLED", "#else // TOOLS_ENABLED")

# CI, release, docs, and desktop editor surfaces must not revive Android editor builds.
forbid_text(
    ".github/workflows/android_builds.yml",
    "android-editor",
    "target: editor",
    "Generate Foundry editor",
    "generateFoundryEditor",
    "generateFoundryHorizonOSEditor",
    "generateFoundryPicoOSEditor",
    "android_editor_builds",
)
forbid_text_in_section(
    ".github/workflows/release.yml",
    "\n  build-android-native:\n",
    "\n  build-ios:\n",
    "release-android-editor",
    "target: editor",
    "ToolsRelease",
    "Generate Foundry editor",
    "generateFoundryEditor",
    "android_editor_builds",
)
forbid_text("doc/classes/EditorSettings.xml", "Android editor")
forbid_text(
    "editor/settings/editor_settings.cpp",
    "is_android_editor",
    "run/window_placement/android_window",
    "interface/touchscreen/touch_actions_panel",
    'has_feature("xr_editor")',
)
forbid_text("editor/editor_node.cpp", "TouchActionsPanel", "_touch_actions_panel_mode_changed")

# Preserved Foundry-owned Android producer, app template, and IDE configuration.
for path in (
    "platform/android/java/app",
    "platform/android/java/nativeSrcsConfigs",
    "platform/android/java/app/src/main/AndroidManifest.xml",
    "platform/android/export/export_plugin.cpp",
):
    require_path(path)
for path in (
    "platform/android/foundry_android_runtime.json",
    "platform/android/android_runtime_build.py",
    "platform/android/android_native_bundle.py",
):
    forbid_path(path)
require_path("platform/android/android_native_contract.py")
require_path("platform/android/android_jni_contract.py")

require_text(
    "platform/android/java/settings.gradle",
    "include ':app'",
    "include ':lib'",
    "include ':nativeSrcsConfigs'",
)
require_text(
    "platform/android/java/build.gradle",
    'dependsOn ":lib:assembleTemplate${capitalizedTarget}"',
    'dependsOn ":app:assemble${capitalizedEdition}${capitalizedTarget}"',
    "foundry-${target}.aar",
    'into("libs/${target}")',
    'dependsOn ":app:assemble${capitalizedEdition}${capitalizedTarget}"',
    "task generateFoundryTemplates",
    "task generateFoundryMonoTemplates",
)
forbid_text(
    "platform/android/java/build.gradle",
    "foundryAndroidSource",
    "foundryAndroidFetch",
    "foundryNativeBundle",
    "foundryRuntimeScratch",
    "WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE",
)
require_text(
    "platform/android/java/lib/build.gradle",
    "id 'com.android.library'",
    "id 'org.jetbrains.kotlin.android'",
    "namespace = 'games.cafecito.foundry'",
    "compileSdkVersion versions.compileSdk",
    "targetSdkVersion versions.targetSdk",
    "testInstrumentationRunner",
    "aidl = true",
    "template {}",
    "abortOnError true",
    "FOUNDRY_BINDINGS_VERSION",
    "FOUNDRY_ENGINE_VERSION",
    "FOUNDRY_ENGINE_REVISION",
    "FOUNDRY_JNI_CONTRACT_VERSION",
)
require_text(
    "platform/android/java/app/build.gradle",
    'implementation project(":lib")',
    "fileTree(dir: \"$addonsDirectory\", include: ['*.jar', '*.aar'])",
    "libopenxr_loader.so",
)
forbid_text(
    "platform/android/java/app/build.gradle",
    'implementation project(":godot:lib")',
    "foundryRuntimeAarRoot",
    "prepareFoundryAndroidRuntime",
)
require_text(
    "platform/android/java/lib/src/main/AndroidManifest.xml",
    "games.cafecito.foundry.library.version",
    "games.cafecito.foundry.engine.version",
    "games.cafecito.foundry.engine.revision",
    "games.cafecito.foundry.jni.contract",
    ".FoundryDownloaderAlarmReceiver",
)
require_text(
    "platform/android/java/lib/src/main/java/com/google/android/vending/expansion/downloader/impl/DownloaderService.java",
    "PendingIntent.FLAG_ONE_SHOT | PendingIntent.FLAG_IMMUTABLE",
)
require_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/service/FoundryService.kt",
    "Build.VERSION_CODES.BAKLAVA",
    "hostInputTransferToken != null",
)
require_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/utils/AndroidRuntimeCompat.kt",
    "@RequiresApi(Build.VERSION_CODES.R)",
    '@SuppressLint("MissingPermission")',
)
require_text(
    "platform/android/export/export_plugin.cpp",
    "get_apksigner_path",
    'args.push_back("sign")',
    'args.push_back("verify")',
    "OS::get_singleton()->execute(apksigner",
    "execute_and_show_output",
    "--xr_mode_openxr",
)


if failures:
    print("Android runtime-only source contract failed:", file=sys.stderr)
    for failure in failures:
        print(f"- {failure}", file=sys.stderr)
    sys.exit(1)

print("Android runtime-only source contract passed.")
