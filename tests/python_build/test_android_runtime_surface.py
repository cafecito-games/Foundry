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
        failures.append(f"required Android path is missing: {path}")


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


# Foundry owns the native producer and app template only. The Java/Kotlin/AIDL
# runtime and its Maven publication are owned by the pinned Foundry-Android repo.
for path in (
    "platform/android/java/lib",
    "platform/android/java/scripts/publish-module.gradle",
    "platform/android/java/scripts/publish-root.gradle",
    "platform/android/java/PUBLISHING.md",
):
    forbid_path(path)

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

forbid_text("platform/android/java/settings.gradle", "include ':editor'", "include ':lib'")
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
    "lib/libs",
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
    "platform/android/foundry_android_runtime.json",
):
    require_path(path)

require_text(
    "platform/android/java/settings.gradle",
    "include ':app'",
    "include ':nativeSrcsConfigs'",
)
require_text(
    "platform/android/java/build.gradle",
    "prepareFoundryAndroidRuntime",
    "foundryRuntimeAarRoot",
    'dependsOn ":app:assemble${capitalizedEdition}${capitalizedTarget}"',
    "task generateFoundryTemplates",
    "task generateFoundryMonoTemplates",
)
require_text(
    "platform/android/java/app/build.gradle",
    "debugImplementation",
    "devImplementation",
    "releaseImplementation",
    "foundryRuntimeAarRoot",
    "getFoundryPluginsMavenRepos",
    "getFoundryPluginsRemoteBinaries",
    "getFoundryPluginsLocalBinaries",
    "libopenxr_loader.so",
)
forbid_text(
    "platform/android/java/app/build.gradle",
    'implementation project(":lib")',
    'implementation project(":godot:lib")',
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
