#!/usr/bin/env python3
"""Run Foundry Android source-template and exported-APK acceptance checks."""

from __future__ import annotations

import importlib.util
import re
import shutil
import stat
import sys
import zipfile
from pathlib import Path
from types import ModuleType


class AcceptanceError(Exception):
    """Android device acceptance failed."""


APPLICATION_ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+$")
DEFAULT_APPLICATION_ID = "games.cafecito.foundry.game"
CUSTOM_APPLICATION_ID = "dev.example.foundryacceptance"
SUPPORTED_ABIS = frozenset({"arm64-v8a", "armeabi-v7a", "x86", "x86_64"})
INSTRUMENTATION_TEST = "games.cafecito.foundry.game.FoundryAppTest#runtimeBootsWithCanonicalPluginProtocol"
RUNTIME_FAILURE_PATTERNS = (
    "UnsatisfiedLinkError",
    "NoClassDefFoundError",
    "ClassNotFoundException",
    "FATAL EXCEPTION",
    'couldn\'t find "libfoundry_android.so"',
)
SOURCE_TEMPLATE_TOOL_PATH = Path(__file__).resolve().with_name("android_source_template.py")
_source_template_tool: ModuleType | None = None


def validate_application_id(value: str) -> str:
    """Return one valid lowercase reverse-DNS Android application ID."""
    if APPLICATION_ID_PATTERN.fullmatch(value) is None:
        raise AcceptanceError(f"invalid Android application ID: {value!r}")
    return value


def select_device(output: str, requested_serial: str | None) -> str:
    """Resolve exactly one ready adb device, or one explicitly requested device."""
    ready = [
        fields[0] for line in output.splitlines()[1:] if len(fields := line.split()) >= 2 and fields[1] == "device"
    ]
    if requested_serial is not None:
        if requested_serial not in ready:
            raise AcceptanceError(f"requested Android device is not ready: {requested_serial}")
        return requested_serial
    if len(ready) != 1:
        raise AcceptanceError(f"expected exactly one ready Android device, found {len(ready)}")
    return ready[0]


def runtime_log_failures(contents: str) -> list[str]:
    """Return fatal runtime signatures present in one captured Android log."""
    return [signature for signature in RUNTIME_FAILURE_PATTERNS if signature in contents]


def _load_source_template_tool() -> ModuleType:
    global _source_template_tool
    if _source_template_tool is not None:
        return _source_template_tool
    spec = importlib.util.spec_from_file_location(
        "_foundry_android_source_template",
        SOURCE_TEMPLATE_TOOL_PATH,
    )
    if spec is None or spec.loader is None:
        raise AcceptanceError(f"unable to load source-template inspector: {SOURCE_TEMPLATE_TOOL_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    _source_template_tool = module
    return module


def stage_scenario(source_template: Path, destination: Path) -> Path:
    """Validate and extract one source template, then add the smoke assets."""
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise AcceptanceError(f"Android acceptance scenario already exists: {destination}")

    inspector = _load_source_template_tool()
    try:
        names = inspector.inspect_source_template(source_template)
    except inspector.SourceTemplateError as error:
        raise AcceptanceError(f"invalid Android source template: {error}") from error

    try:
        destination.mkdir(parents=True)
        with zipfile.ZipFile(source_template) as archive:
            for name in names:
                target = destination / name
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(name) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)

        instrumented_assets = destination / "src/instrumented/assets"
        if not instrumented_assets.is_dir():
            raise AcceptanceError(
                "Android source template is missing instrumented smoke assets: src/instrumented/assets"
            )
        shutil.copytree(
            instrumented_assets,
            destination / "src/main/assets",
            dirs_exist_ok=True,
        )
        gradlew = destination / "gradlew"
        gradlew.chmod(gradlew.stat().st_mode | stat.S_IXUSR)
    except AcceptanceError:
        raise
    except (OSError, zipfile.BadZipFile) as error:
        raise AcceptanceError(f"unable to stage Android acceptance scenario: {error}") from error
    return destination


def gradle_acceptance_command(
    gradlew: Path,
    abi: str,
    application_id: str,
) -> list[str]:
    """Build the standard APK and run the focused instrumented smoke."""
    if abi not in SUPPORTED_ABIS:
        raise AcceptanceError(f"unsupported Android device ABI: {abi!r}")
    validate_application_id(application_id)
    command = [
        str(gradlew),
        "--no-daemon",
        "assembleStandardDebug",
        "connectedInstrumentedDebugAndroidTest",
        f"-Pexport_enabled_abis={abi}|",
        "-Pperform_signing=true",
        "-Pperform_zipalign=true",
        f"-Pandroid.testInstrumentationRunnerArguments.class={INSTRUMENTATION_TEST}",
    ]
    if application_id != DEFAULT_APPLICATION_ID:
        command.append(f"-Pexport_package_name={application_id}")
    return command
