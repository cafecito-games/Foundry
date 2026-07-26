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
        failures.append(f"required runtime path is missing: {path}")


def forbid_path(path: str) -> None:
    if (ROOT / path).exists():
        failures.append(f"removed Android editor path still exists: {path}")


def require_text(path: str, *fragments: str) -> None:
    contents = source(path)
    for fragment in fragments:
        if fragment not in contents:
            failures.append(f"{path} must preserve {fragment!r}")


def forbid_text(path: str, *fragments: str) -> None:
    contents = source(path)
    for fragment in fragments:
        if fragment in contents:
            failures.append(f"{path} still contains Android editor surface {fragment!r}")


def forbid_text_in_section(path: str, start: str, end: str, *fragments: str) -> None:
    contents = source(path)
    if start not in contents or end not in contents:
        failures.append(f"{path} must contain section boundaries {start!r} and {end!r}")
        return
    section = contents.split(start, 1)[1].split(end, 1)[0]
    for fragment in fragments:
        if fragment in section:
            failures.append(f"{path} section {start.strip()!r} still contains Android editor surface {fragment!r}")


# Removed application, library flavor, publication, and generated artifacts.
forbid_path("platform/android/java/editor")
forbid_path("platform/android/java/lib/src/main/java/games/cafecito/foundry/BuildProvider.java")
forbid_path("platform/android/java/lib/src/main/java/games/cafecito/foundry/editor")
forbid_path("platform/android/java/lib/src/main/java/games/cafecito/foundry/utils/BenchmarkUtils.kt")
forbid_path("platform/android/editor")
forbid_path("platform/android/export/android_editor_gradle_runner.cpp")
forbid_path("platform/android/export/android_editor_gradle_runner.h")
forbid_path("editor/gui/touch_actions_panel.cpp")
forbid_path("editor/gui/touch_actions_panel.h")

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
    "lib/libs/tools",
)
forbid_text(
    "platform/android/java/lib/build.gradle",
    "TOOLS_PUBLISH_ARTIFACT_ID",
    "foundry-tools",
    "editor {}",
    "editorRelease",
    "editorDebug",
    "editorDev",
    "libs/tools",
)
forbid_text(
    "platform/android/java/scripts/publish-module.gradle",
    "toolsRelease",
    "components.editorRelease",
    "Editor Build",
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

# Removed editor-only Java/Kotlin and JNI host hooks.
forbid_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt",
    "isEditorHint",
    "isProjectManagerHint",
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
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryHost.java",
    "signApk",
    "verifyApk",
    "onEditorWorkspaceSelected",
    "getBuildProvider",
)
forbid_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryFragment.java",
    "signApk",
    "verifyApk",
    "onEditorWorkspaceSelected",
    "getBuildProvider",
)
forbid_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryLib.java",
    "getEditorSetting",
    "setEditorSetting",
    "getEditorProjectMetadata",
    "setEditorProjectMetadata",
    "isEditorHint",
    "isProjectManagerHint",
)
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
)
forbid_text(
    "platform/android/export/export_plugin.cpp",
    "AndroidEditorGradleRunner",
    "android_editor_gradle_runner",
    "OS_Android::get_singleton()->sign_apk",
    "OS_Android::get_singleton()->verify_apk",
    "_copy_keystore_to_temp",
)

# Removed CI, release, publication, and active support documentation.
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
    "\n  build-android:\n",
    "\n  build-ios:\n",
    "release-android-editor",
    "target: editor",
    "ToolsRelease",
    "Generate Foundry editor",
    "generateFoundryEditor",
    "android_editor_builds",
)
forbid_text("platform/android/java/PUBLISHING.md", "foundry-tools", "Editor / tools", "editor/tools build")
forbid_text("doc/classes/EditorSettings.xml", "Android editor")
forbid_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt",
    "EDITOR_FLAVOR",
    "isEditorBuild()",
)
forbid_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/io/directory/DirectoryAccessHandler.kt",
    "Foundry.isEditorBuild()",
    "If this is an editor build",
)
forbid_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryActivity.kt",
    "BaseFoundryEditor",
)
forbid_text("platform/android/java_foundry_lib_jni.cpp", "editor/settings/editor_settings.h")
forbid_text("platform/android/os_android.cpp", "#ifdef TOOLS_ENABLED", "#else // TOOLS_ENABLED")
forbid_text(
    "editor/settings/editor_settings.cpp",
    "is_android_editor",
    "run/window_placement/android_window",
    "interface/touchscreen/touch_actions_panel",
    'has_feature("xr_editor")',
)
forbid_text("editor/editor_node.cpp", "TouchActionsPanel", "_touch_actions_panel_mode_changed")

# Preserved Gradle project, runtime library, export template, and publications.
for path in (
    "platform/android/java/app",
    "platform/android/java/lib",
    "platform/android/java/nativeSrcsConfigs",
    "platform/android/java/app/src/main/AndroidManifest.xml",
    "platform/android/java/lib/src/main/AndroidManifest.xml",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryHost.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryActivity.kt",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/service/FoundryService.kt",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/service/RemoteFoundryFragment.kt",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/FoundryPlugin.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/FoundryPluginRegistry.java",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/plugin/AndroidRuntimePlugin.kt",
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/xr/XRMode.java",
    "platform/android/export/export_plugin.cpp",
):
    require_path(path)

require_text(
    "platform/android/java/settings.gradle",
    "include ':app'",
    "include ':lib'",
    "include ':nativeSrcsConfigs'",
)
require_text(
    "platform/android/java/build.gradle",
    'supportedFlavors = ["template"]',
    'dependsOn ":lib:assembleTemplate${capitalizedTarget}"',
    'dependsOn ":app:assemble${capitalizedEdition}${capitalizedTarget}"',
    "task generateFoundryTemplates",
    "task generateFoundryMonoTemplates",
)
require_text(
    "platform/android/java/lib/build.gradle",
    "template {}",
    'singleVariant("templateDebug")',
    'singleVariant("templateRelease")',
    '"target=${sconsTarget}"',
)
require_text(
    "platform/android/java/scripts/publish-module.gradle",
    "templateDebug(MavenPublication)",
    "from components.templateDebug",
    "templateRelease(MavenPublication)",
    "from components.templateRelease",
)
require_text(
    "platform/android/java/app/build.gradle",
    'implementation project(":lib")',
    "getFoundryPluginsMavenRepos",
    "getFoundryPluginsRemoteBinaries",
    "getFoundryPluginsLocalBinaries",
    "libopenxr_loader.so",
)

# Preserved runtime plugin, XR, service, and desktop exporter behavior.
require_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/Foundry.kt",
    "FoundryPluginRegistry.initializePluginRegistry",
    "AndroidRuntimePlugin(this)",
    "plugin.supportsFeature(feature)",
    'hasFeature("xr_runtime")',
    "XRMode.OPENXR",
    "createNewFoundryInstance",
)
require_text(
    "platform/android/java/lib/src/main/java/games/cafecito/foundry/FoundryHost.java",
    "getHostPlugins",
    "supportsFeature",
    "onNewFoundryInstanceRequested",
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
