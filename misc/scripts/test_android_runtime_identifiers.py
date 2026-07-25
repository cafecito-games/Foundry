#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ANDROID_ROOT = REPO_ROOT / "platform/android"
JAVA_ROOT = ANDROID_ROOT / "java"
APP_ROOT = JAVA_ROOT / "app"
LIB_ROOT = JAVA_ROOT / "lib"

APP_IMPLEMENTATION_PACKAGE = "games.cafecito.foundry.game"
PLUGIN_METADATA_PREFIX = "games.cafecito.foundry.plugin.v1."
LIBRARY_VERSION_METADATA = "games.cafecito.foundry.library.version"
WAKE_LOCK_LABEL = "games.cafecito.foundry:wakelock"

OLD_RUNTIME_IDENTIFIERS = (
    "com.godot",
    "org.godotengine",
    "org.godot.game",
)


def relative(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def require_file(path: Path, failures: list[str]) -> str:
    if not path.is_file():
        failures.append(f"{relative(path)} is missing")
        return ""
    return path.read_text()


def require_contains(path: Path, expected: str, failures: list[str]) -> None:
    text = require_file(path, failures)
    if text and expected not in text:
        failures.append(f"{relative(path)} is missing {expected!r}")


def reject_contains(path: Path, forbidden: tuple[str, ...], failures: list[str]) -> None:
    text = require_file(path, failures)
    hits = [identifier for identifier in forbidden if identifier in text]
    if hits:
        failures.append(f"{relative(path)} contains stale runtime identifiers: {hits}")


def check_app_source_surface(failures: list[str]) -> None:
    source_suffixes = {".java", ".kt", ".aidl", ".xml", ".gradle", ".fs"}
    for path in APP_ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in source_suffixes or "build" in path.parts:
            continue
        reject_contains(path, OLD_RUNTIME_IDENTIFIERS, failures)

        if path.suffix in {".java", ".kt", ".aidl"}:
            for package_name in re.findall(r"(?m)^package\s+([A-Za-z0-9_.]+)\s*;?", path.read_text()):
                if not (
                    package_name == APP_IMPLEMENTATION_PACKAGE
                    or package_name.startswith(f"{APP_IMPLEMENTATION_PACKAGE}.")
                ):
                    failures.append(f"{relative(path)} declares unexpected app/test package {package_name!r}")

    stale_source_roots = [
        APP_ROOT / "src/main/java/com/godot",
        APP_ROOT / "src/androidTestInstrumented/java/com/godot",
        APP_ROOT / "src/instrumented/java/com/godot",
        APP_ROOT / "src/main/java/org/godotengine",
        APP_ROOT / "src/androidTestInstrumented/java/org/godotengine",
        APP_ROOT / "src/instrumented/java/org/godotengine",
    ]
    failures.extend(f"{relative(path)} must not exist" for path in stale_source_roots if path.exists())


def check_library_package_declarations(failures: list[str]) -> None:
    for path in LIB_ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in {".java", ".kt", ".aidl"} or "build" in path.parts:
            continue

        for package_name in re.findall(r"(?m)^package\s+([A-Za-z0-9_.]+)\s*;?", path.read_text()):
            if package_name.startswith(("com.godot", "org.godotengine")):
                failures.append(f"{relative(path)} declares stale runtime package {package_name!r}")


def check_exact_contract(failures: list[str]) -> None:
    app_build = APP_ROOT / "build.gradle"
    app_config = APP_ROOT / "config.gradle"
    app_manifest = APP_ROOT / "src/main/AndroidManifest.xml"
    instrumented_manifest = APP_ROOT / "src/instrumented/AndroidManifest.xml"
    app_source = APP_ROOT / "src/main/java/games/cafecito/foundry/game/FoundryApp.java"
    exporter = ANDROID_ROOT / "export/export_plugin.cpp"
    plugin_registry = LIB_ROOT / "src/main/java/games/cafecito/foundry/plugin/FoundryPluginRegistry.java"
    plugin_docs = LIB_ROOT / "src/main/java/games/cafecito/foundry/plugin/FoundryPlugin.java"
    library_manifest = LIB_ROOT / "src/main/AndroidManifest.xml"
    download_thread = (
        LIB_ROOT / "src/main/java/com/google/android/vending/expansion/downloader/impl/DownloadThread.java"
    )
    downloader_patch = LIB_ROOT / "patches/com.google.android.vending.expansion.downloader.patch"

    require_contains(app_build, f"namespace = '{APP_IMPLEMENTATION_PACKAGE}'", failures)
    require_contains(app_config, f'appId = "{APP_IMPLEMENTATION_PACKAGE}"', failures)
    require_contains(app_manifest, f'android:name="{APP_IMPLEMENTATION_PACKAGE}.FoundryApp"', failures)
    require_contains(
        app_manifest,
        f'android:name="{APP_IMPLEMENTATION_PACKAGE}.FoundryAppLauncher"',
        failures,
    )
    require_contains(
        app_manifest,
        f'android:targetActivity="{APP_IMPLEMENTATION_PACKAGE}.FoundryApp"',
        failures,
    )
    require_contains(
        instrumented_manifest,
        f'android:name="{PLUGIN_METADATA_PREFIX}FoundryAppInstrumentedTestPlugin"',
        failures,
    )
    require_contains(
        instrumented_manifest,
        f'android:value="{APP_IMPLEMENTATION_PACKAGE}.test.FoundryAppInstrumentedTestPlugin"',
        failures,
    )
    require_contains(app_source, f"package {APP_IMPLEMENTATION_PACKAGE};", failures)
    require_contains(
        exporter,
        f'"/{APP_IMPLEMENTATION_PACKAGE}.FoundryAppLauncher"',
        failures,
    )

    registry_text = require_file(plugin_registry, failures)
    if registry_text:
        if registry_text.count(f'"{PLUGIN_METADATA_PREFIX}"') != 1:
            failures.append(
                f"{relative(plugin_registry)} must define exactly one {PLUGIN_METADATA_PREFIX!r} protocol prefix"
            )
        reject_contains(plugin_registry, OLD_RUNTIME_IDENTIFIERS, failures)
        for stale_branch in ("FOUNDRY_PLUGIN_V1_NAME_PREFIX", "FOUNDRY_PLUGIN_V2_NAME_PREFIX"):
            if stale_branch in registry_text:
                failures.append(f"{relative(plugin_registry)} retains dual-protocol branch {stale_branch}")

    require_contains(plugin_docs, PLUGIN_METADATA_PREFIX, failures)
    reject_contains(plugin_docs, OLD_RUNTIME_IDENTIFIERS, failures)
    require_contains(library_manifest, f'android:name="{LIBRARY_VERSION_METADATA}"', failures)
    reject_contains(library_manifest, OLD_RUNTIME_IDENTIFIERS, failures)

    for path in (download_thread, downloader_patch):
        require_contains(path, WAKE_LOCK_LABEL, failures)
        reject_contains(path, ("org.godot.game:wakelock",), failures)
    require_contains(downloader_patch, "// -- FOUNDRY start --", failures)
    require_contains(downloader_patch, "// -- FOUNDRY end --", failures)


def check_jni_contract(failures: list[str]) -> None:
    symbol_pattern = re.compile(r"\bJava_[A-Za-z0-9_]+")
    symbols: set[str] = set()
    for suffix in ("*.c", "*.cc", "*.cpp", "*.h"):
        for path in ANDROID_ROOT.rglob(suffix):
            if "thirdparty" not in path.parts:
                symbols.update(symbol_pattern.findall(path.read_text(errors="replace")))

    if not symbols:
        failures.append("platform/android contains no JNI export symbols to verify")
        return

    stale = sorted(symbol for symbol in symbols if not symbol.startswith("Java_games_cafecito_foundry_"))
    if stale:
        failures.append(f"platform/android contains JNI exports outside games.cafecito.foundry: {stale}")


def main() -> int:
    failures: list[str] = []
    check_app_source_surface(failures)
    check_library_package_declarations(failures)
    check_exact_contract(failures)
    check_jni_contract(failures)

    if failures:
        print("Foundry Android runtime identifier contract failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("Foundry Android runtime identifier tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
