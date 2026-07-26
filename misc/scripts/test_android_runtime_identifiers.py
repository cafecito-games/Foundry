#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import re
import sys
from pathlib import Path
from types import ModuleType

REPO_ROOT = Path(__file__).resolve().parents[2]
ANDROID_ROOT = REPO_ROOT / "platform/android"
JAVA_ROOT = ANDROID_ROOT / "java"
APP_ROOT = JAVA_ROOT / "app"
RUNTIME_PIN = ANDROID_ROOT / "foundry_android_runtime.json"
RUNTIME_CONTRACT = ANDROID_ROOT / "android_runtime_contract.py"

APP_IMPLEMENTATION_PACKAGE = "games.cafecito.foundry.game"
PLUGIN_METADATA_PREFIX = "games.cafecito.foundry.plugin.v1."
STANDALONE_REPOSITORY = "https://github.com/cafecito-games/Foundry-Android.git"

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


def reject_contains(
    path: Path,
    forbidden: tuple[str, ...],
    failures: list[str],
    allowed_literals: tuple[str, ...] = (),
) -> None:
    text = require_file(path, failures)
    for literal in allowed_literals:
        text = text.replace(literal, "")
    hits = [identifier for identifier in forbidden if identifier in text]
    if hits:
        failures.append(f"{relative(path)} contains stale runtime identifiers: {hits}")


def load_runtime_contract(failures: list[str]) -> ModuleType | None:
    spec = importlib.util.spec_from_file_location("android_runtime_contract", RUNTIME_CONTRACT)
    if spec is None or spec.loader is None:
        failures.append(f"could not load canonical runtime contract at {relative(RUNTIME_CONTRACT)}")
        return None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception as error:  # noqa: BLE001 - contract import diagnostics belong in this check.
        failures.append(f"could not import canonical runtime contract: {error}")
        return None
    return module


def check_standalone_boundary(failures: list[str]) -> None:
    required = (
        JAVA_ROOT / "lib/build.gradle",
        JAVA_ROOT / "lib/src/main/AndroidManifest.xml",
        JAVA_ROOT / "lib/src/main/java/games/cafecito/foundry/Foundry.kt",
        JAVA_ROOT / "lib/src/main/aidl/com/android/vending/licensing/ILicensingService.aidl",
    )
    failures.extend(f"{relative(path)} must exist" for path in required if not path.is_file())

    removed = (
        JAVA_ROOT / "scripts/publish-module.gradle",
        JAVA_ROOT / "scripts/publish-root.gradle",
        JAVA_ROOT / "PUBLISHING.md",
    )
    failures.extend(f"{relative(path)} must not exist" for path in removed if path.exists())

    active_gradle = (
        JAVA_ROOT / "settings.gradle",
        JAVA_ROOT / "build.gradle",
        JAVA_ROOT / "app/build.gradle",
        JAVA_ROOT / "lib/build.gradle",
    )
    forbidden = (
        "maven-publish",
        "MavenPublication",
        "nexusPublishing",
        "prepareFoundryAndroidRuntime",
        "foundryAndroidSource",
        "foundryAndroidFetch",
        "foundryNativeBundle",
    )
    for path in active_gradle:
        reject_contains(path, forbidden, failures)


def check_app_source_surface(failures: list[str]) -> None:
    source_suffixes = {".java", ".kt", ".aidl", ".xml", ".gradle", ".fs"}
    legacy_protocol_fixtures = {
        APP_ROOT / "src/androidTestInstrumented/java/games/cafecito/foundry/game/FoundryAppTest.kt": (
            '"org.godotengine.plugin.v1.Legacy"',
            '"org.godotengine.plugin.v2.Legacy"',
        ),
        APP_ROOT / "src/instrumented/AndroidManifest.xml": (
            'android:name="org.godotengine.plugin.v1.Legacy"',
            'android:name="org.godotengine.plugin.v2.Legacy"',
        ),
    }
    for path in APP_ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in source_suffixes or "build" in path.parts:
            continue
        reject_contains(
            path,
            OLD_RUNTIME_IDENTIFIERS,
            failures,
            legacy_protocol_fixtures.get(path, ()),
        )

        if path.suffix in {".java", ".kt", ".aidl"}:
            for package_name in re.findall(r"(?m)^package\s+([A-Za-z0-9_.]+)\s*;?", path.read_text()):
                if not (
                    package_name == APP_IMPLEMENTATION_PACKAGE
                    or package_name.startswith(f"{APP_IMPLEMENTATION_PACKAGE}.")
                ):
                    failures.append(f"{relative(path)} declares unexpected app/test package {package_name!r}")

    stale_source_roots = (
        APP_ROOT / "src/main/java/com/godot",
        APP_ROOT / "src/androidTestInstrumented/java/com/godot",
        APP_ROOT / "src/instrumented/java/com/godot",
        APP_ROOT / "src/main/java/org/godotengine",
        APP_ROOT / "src/androidTestInstrumented/java/org/godotengine",
        APP_ROOT / "src/instrumented/java/org/godotengine",
    )
    failures.extend(f"{relative(path)} must not exist" for path in stale_source_roots if path.exists())


def check_exact_app_contract(failures: list[str]) -> None:
    app_build = APP_ROOT / "build.gradle"
    app_config = APP_ROOT / "config.gradle"
    app_manifest = APP_ROOT / "src/main/AndroidManifest.xml"
    instrumented_manifest = APP_ROOT / "src/instrumented/AndroidManifest.xml"
    app_source = APP_ROOT / "src/main/java/games/cafecito/foundry/game/FoundryApp.java"
    app_test = APP_ROOT / "src/androidTestInstrumented/java/games/cafecito/foundry/game/FoundryAppTest.kt"
    instrumented_scene = APP_ROOT / "src/instrumented/assets/main.tscn"
    exporter = ANDROID_ROOT / "export/export_plugin.cpp"

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
    require_contains(
        instrumented_manifest,
        'android:name="org.godotengine.plugin.v1.Legacy"',
        failures,
    )
    require_contains(
        instrumented_manifest,
        'android:name="org.godotengine.plugin.v2.Legacy"',
        failures,
    )
    require_contains(app_source, f"package {APP_IMPLEMENTATION_PACKAGE};", failures)
    require_contains(app_test, "runtimeBootsWithCanonicalPluginProtocol", failures)
    require_contains(
        app_test,
        '"games.cafecito.foundry.plugin.v1.FoundryAppInstrumentedTestPlugin"',
        failures,
    )
    require_contains(app_test, '"org.godotengine.plugin.v1.Legacy"', failures)
    require_contains(app_test, '"org.godotengine.plugin.v2.Legacy"', failures)
    require_contains(instrumented_scene, 'path="res://main.fs"', failures)
    require_contains(exporter, f'"/{APP_IMPLEMENTATION_PACKAGE}.FoundryAppLauncher"', failures)


def check_runtime_pin(failures: list[str]) -> None:
    contract = load_runtime_contract(failures)
    if contract is None:
        return
    try:
        pin = contract.load_pin(RUNTIME_PIN)
    except Exception as error:  # noqa: BLE001 - surface the canonical validator's exact diagnostic.
        failures.append(f"{relative(RUNTIME_PIN)} violates the canonical runtime contract: {error}")
        return
    if pin.repository != STANDALONE_REPOSITORY:
        failures.append(f"{relative(RUNTIME_PIN)} must identify {STANDALONE_REPOSITORY!r}, got {pin.repository!r}")
    reject_contains(RUNTIME_PIN, OLD_RUNTIME_IDENTIFIERS, failures)


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
    check_standalone_boundary(failures)
    check_app_source_surface(failures)
    check_exact_app_contract(failures)
    check_runtime_pin(failures)
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
