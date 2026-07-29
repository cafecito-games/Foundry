#!/usr/bin/env python3
"""Run Foundry Android source-template and exported-APK acceptance checks."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import time
import zipfile
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from types import ModuleType
from typing import Any, Protocol, cast
from xml.etree import ElementTree


class AcceptanceError(Exception):
    """Android device acceptance failed."""


APPLICATION_ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+$")
DEFAULT_APPLICATION_ID = "games.cafecito.foundry.game"
CUSTOM_APPLICATION_ID = "dev.example.foundryacceptance"
SUPPORTED_ABIS = frozenset({"arm64-v8a", "armeabi-v7a", "x86", "x86_64"})
INSTRUMENTATION_CLASS = "games.cafecito.foundry.game.FoundryAppTest"
INSTRUMENTATION_METHODS = (
    "runtimeBootsWithoutLegacyPluginMetadata",
    "runJavaClassWrapperTests",
    "runFileAccessTests",
    "testImplicitFoundryAppLauncherLaunch",
    "testExplicitFoundryAppLauncherLaunch",
    "testExplicitFoundryAppLaunch",
    "testGameNotQuittingOnBackPress",
    "testGameQuittingOnBackPress",
)
INSTRUMENTATION_TEST = INSTRUMENTATION_CLASS
STANDARD_APK = Path("build/outputs/apk/standard/debug/android_debug.apk")
JUNIT_REPORT_ROOT = Path("build/outputs/androidTest-results/connected")
# The single list of fatal runtime signatures. It gates the runtime-marker wait,
# the post-run log review, and the startup wait: observing one of these attributed
# to the target package while polling for a PID means the process already died, so
# the wait ends immediately with the real cause instead of burning the whole
# timeout on an empty probe.
RUNTIME_FAILURE_PATTERNS = (
    "UnsatisfiedLinkError",
    "NoClassDefFoundError",
    "ClassNotFoundException",
    "NoSuchMethodError",
    "NoSuchFieldError",
    "IncompatibleClassChangeError",
    "FATAL EXCEPTION",
    "Fatal signal",
    'couldn\'t find "libfoundry_android.so"',
)
STARTUP_ABORT_EXCERPT_LINES = 60
# A fatal signature only ends the startup wait when the surrounding log lines name
# the package. Android writes the owner next to the signature -- "Process: <id>"
# for a Java crash, ">>> <id> <<<" for a tombstone -- so an unrelated process
# crashing during our startup delay cannot be mistaken for the target aborting.
STARTUP_ABORT_ATTRIBUTION_LINES = 30
HOST_CONTRACT_REPORTED_MEMBERS = 12
STANDARD_SMOKE_READY_MARKER = "Foundry Android standard runtime smoke ready"
SOURCE_TEMPLATE_TOOL_PATH = Path(__file__).resolve().with_name("android_source_template.py")
HOST_CONTRACT_TOOL_PATH = Path(__file__).resolve().with_name("android_host_contract.py")
REPO_ROOT = Path(__file__).resolve().parents[2]
COMPILED_ASSET_REQUIRED_PATHS = frozenset(
    {
        "project.binary",
        "main.fsb",
        "main.fs.remap",
        "main.tscn.remap",
        "test/base_test.fsb",
        "test/base_test.fs.remap",
        "test/file_access/file_access_tests.fsb",
        "test/file_access/file_access_tests.fs.remap",
        "test/javaclasswrapper/java_class_wrapper_tests.fsb",
        "test/javaclasswrapper/java_class_wrapper_tests.fs.remap",
    }
)
MAX_COMPILED_ASSET_ENTRY_BYTES = 16 * 1024 * 1024
MAX_COMPILED_ASSET_TOTAL_BYTES = 64 * 1024 * 1024
PROCESS_STABILITY_OBSERVATIONS = 3
COMPILED_ASSET_REMAP_TARGETS = {
    "main.fs.remap": "res://main.fsb",
    "test/base_test.fs.remap": "res://test/base_test.fsb",
    "test/file_access/file_access_tests.fs.remap": "res://test/file_access/file_access_tests.fsb",
    "test/javaclasswrapper/java_class_wrapper_tests.fs.remap": (
        "res://test/javaclasswrapper/java_class_wrapper_tests.fsb"
    ),
}
_source_template_tool: ModuleType | None = None
_host_contract_tool: ModuleType | None = None


def _remap_target(contents: bytes, name: str) -> str:
    try:
        text_contents = contents.decode("utf-8", errors="strict")
    except UnicodeError as error:
        raise AcceptanceError(f"Android compiled acceptance remap {name} is not valid UTF-8") from error
    targets = re.findall(r'(?m)^path="([^"]+)"\s*$', text_contents)
    if len(targets) != 1:
        raise AcceptanceError(f"Android compiled acceptance remap {name} must contain exactly one path")
    return cast(str, targets[0])


@dataclass(frozen=True)
class CommandResult:
    """Captured subprocess output."""

    argv: tuple[str, ...]
    returncode: int
    stdout: str
    stderr: str


class Runner(Protocol):
    """Command execution boundary used by real runs and deterministic tests."""

    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path | None,
        timeout: float,
        description: str,
        check: bool = True,
        env: dict[str, str] | None = None,
    ) -> CommandResult: ...


class SubprocessRunner:
    """Run commands without a shell and retain one log per invocation.

    Passing no evidence directory runs commands without retaining logs, which the
    device-free inspection commands use.
    """

    def __init__(self, evidence_dir: Path | None = None) -> None:
        self.evidence_dir = evidence_dir
        self.command_dir = evidence_dir / "commands" if evidence_dir is not None else None
        if self.command_dir is not None:
            self.command_dir.mkdir(parents=True, exist_ok=True)
        self.command_index = 0

    def _write_log(
        self,
        description: str,
        result: CommandResult,
    ) -> None:
        if self.command_dir is None:
            return
        self.command_index += 1
        slug = re.sub(r"[^a-z0-9]+", "-", description.lower()).strip("-") or "command"
        path = self.command_dir / f"{self.command_index:03d}-{slug}.log"
        path.write_text(
            (
                f"argv: {json.dumps(list(result.argv))}\n"
                f"returncode: {result.returncode}\n"
                "\nstdout:\n"
                f"{result.stdout}"
                "\nstderr:\n"
                f"{result.stderr}"
            ),
            encoding="utf-8",
        )

    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path | None,
        timeout: float,
        description: str,
        check: bool = True,
        env: dict[str, str] | None = None,
    ) -> CommandResult:
        arguments = tuple(str(argument) for argument in argv)
        command_env = None
        if env is not None:
            command_env = os.environ.copy()
            command_env.update(env)
        try:
            completed = subprocess.run(
                list(arguments),
                cwd=cwd,
                env=command_env,
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
            )
            result = CommandResult(
                argv=arguments,
                returncode=completed.returncode,
                stdout=completed.stdout,
                stderr=completed.stderr,
            )
        except subprocess.TimeoutExpired as error:
            stdout = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
            stderr = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
            result = CommandResult(arguments, 124, stdout, stderr)
            self._write_log(description, result)
            raise AcceptanceError(f"{description} timed out after {timeout:g} seconds") from error
        except OSError as error:
            result = CommandResult(arguments, 127, "", str(error))
            self._write_log(description, result)
            raise AcceptanceError(f"{description} could not start: {error}") from error

        self._write_log(description, result)
        if check and result.returncode != 0:
            raise AcceptanceError(
                f"{description} failed with exit {result.returncode}:\n{result.stdout}{result.stderr}"
            )
        return result


def validate_application_id(value: str) -> str:
    """Return one valid lowercase reverse-DNS Android application ID."""
    if APPLICATION_ID_PATTERN.fullmatch(value) is None:
        raise AcceptanceError(f"invalid Android application ID: {value!r}")
    return value


def inspect_foundry_java_apk(
    apk: Path,
    *,
    requested_abis: Sequence[str],
    enabled: bool,
) -> dict[str, Any] | None:
    """Inspect the enabled-only Foundry-Java contract in one final APK or AAB."""
    if not enabled:
        return None

    artifact_kind = "AAB" if apk.suffix.lower() == ".aab" else "APK"
    root = "base/" if artifact_kind == "AAB" else ""
    requested = tuple(sorted(requested_abis))
    if not requested:
        raise AcceptanceError(f"Foundry-Java {artifact_kind} inspection requires at least one requested ABI")
    if len(requested) != len(set(requested)):
        raise AcceptanceError(f"Foundry-Java {artifact_kind} inspection received duplicate requested ABIs")
    unsupported = sorted(set(requested).difference(SUPPORTED_ABIS))
    if unsupported:
        raise AcceptanceError(
            f"Foundry-Java {artifact_kind} inspection has unsupported requested ABI: " + ", ".join(unsupported)
        )

    apk = apk.absolute()
    if not apk.is_file() or apk.is_symlink():
        raise AcceptanceError(f"Foundry-Java {artifact_kind} is not a regular file: {apk}")

    configuration = f"{root}assets/FoundryJava.foundryextension"
    registry_index = f"{root}assets/foundry_java/registry-index-v2.txt"
    try:
        with zipfile.ZipFile(apk) as archive:
            names = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
            for required in (configuration, registry_index):
                count = names.count(required)
                if count != 1:
                    raise AcceptanceError(
                        f"Foundry-Java {artifact_kind} must contain exactly one {required}; found {count}"
                    )
            bridge_entries = tuple(sorted(name for name in names if name.endswith("/libfoundry_java.so")))
            expected_bridges = tuple(f"{root}lib/{abi}/libfoundry_java.so" for abi in requested)
            if bridge_entries != expected_bridges:
                raise AcceptanceError(
                    f"Foundry-Java {artifact_kind} bridge entries differ from the requested ABI set: "
                    f"expected {list(expected_bridges)}, found {list(bridge_entries)}"
                )
            host_entries = tuple(sorted(name for name in names if name.endswith("/libfoundry_android.so")))
            return {
                "requested_abis": requested,
                "bridge_entries": bridge_entries,
                "host_entries": host_entries,
                "configuration_sha256": hashlib.sha256(archive.read(configuration)).hexdigest(),
                "registry_index_sha256": hashlib.sha256(archive.read(registry_index)).hexdigest(),
            }
    except AcceptanceError:
        raise
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise AcceptanceError(f"unable to inspect Foundry-Java {artifact_kind} {apk}: {error}") from error


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


def _package_owner_pattern(application_id: str) -> re.Pattern[str]:
    # A bare substring match would also claim sibling packages that extend the ID,
    # such as the ".instrumented" flavor, so require a package-name boundary.
    return re.compile(rf"(?<![\w.$]){re.escape(application_id)}(?![\w.$])")


def attributed_runtime_failures(contents: str, application_id: str) -> list[str]:
    """Return fatal signatures one captured log attributes to a specific package."""
    lines = contents.splitlines()
    owner = _package_owner_pattern(application_id)
    owned = [index for index, line in enumerate(lines) if owner.search(line)]
    if not owned:
        return []
    attributed: list[str] = []
    for signature in RUNTIME_FAILURE_PATTERNS:
        for index, line in enumerate(lines):
            if signature not in line:
                continue
            if any(abs(index - owner) <= STARTUP_ABORT_ATTRIBUTION_LINES for owner in owned):
                attributed.append(signature)
                break
    return attributed


def startup_abort_excerpt(contents: str, signatures: Sequence[str]) -> str:
    """Return the log window around the first startup abort signature."""
    lines = contents.splitlines()
    first = next(
        (index for index, line in enumerate(lines) if any(signature in line for signature in signatures)),
        None,
    )
    if first is None:
        return "\n".join(lines[-STARTUP_ABORT_EXCERPT_LINES:])
    start = max(0, first - STARTUP_ABORT_EXCERPT_LINES // 4)
    return "\n".join(lines[start : start + STARTUP_ABORT_EXCERPT_LINES])


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


def _load_host_contract_tool() -> ModuleType:
    global _host_contract_tool
    if _host_contract_tool is not None:
        return _host_contract_tool
    spec = importlib.util.spec_from_file_location(
        "_foundry_android_host_contract",
        HOST_CONTRACT_TOOL_PATH,
    )
    if spec is None or spec.loader is None:
        raise AcceptanceError(f"unable to load host JNI contract: {HOST_CONTRACT_TOOL_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    _host_contract_tool = module
    return module


def expected_host_dex_members(repo_root: Path) -> tuple[str, ...]:
    """Return the dex member listings every native-resolved Java member must produce."""
    contract = _load_host_contract_tool()
    try:
        members = contract.derive_host_members(repo_root)
    except contract.HostContractError as error:
        raise AcceptanceError(f"unable to derive the Android host JNI contract: {error}") from error

    listings: list[str] = []
    for member in members:
        if member.field:
            rendered = f"{contract.decode_field_descriptor(member.descriptor)} {member.member_name}"
        else:
            return_type, arguments = contract.decode_method_descriptor(member.descriptor)
            signature = f"{member.member_name}({','.join(arguments)})"
            # Initializers are listed without a return type.
            rendered = signature if member.member_name.startswith("<") else f"{return_type} {signature}"
        listings.append(f"{member.class_name} {rendered}")
    return tuple(sorted(listings))


def parse_dex_members(output: str) -> frozenset[str]:
    """Return every class member defined by an ``apkanalyzer dex packages`` listing."""
    members: set[str] = set()
    for line in output.splitlines():
        fields = line.split(None, 5)
        if len(fields) != 6 or fields[0] not in ("M", "F") or fields[1] != "d":
            continue
        members.add(" ".join(fields[5].split()))
    return frozenset(members)


def verify_host_contract_in_apk(
    *,
    apk: Path,
    apkanalyzer: Path,
    runner: Runner,
    repo_root: Path,
) -> dict[str, Any]:
    """Prove a built APK still exposes every Java member the native host resolves."""
    apk = apk.absolute()
    if not apk.is_file():
        raise AcceptanceError(f"Android host contract APK does not exist: {apk}")
    expected = expected_host_dex_members(repo_root)
    listing = runner.run(
        [str(apkanalyzer), "dex", "packages", "--defined-only", str(apk)],
        cwd=None,
        timeout=600,
        description=f"listing defined dex members of {apk.name}",
    )
    defined = parse_dex_members(listing.stdout)
    missing = tuple(member for member in expected if member not in defined)
    if missing:
        shown = list(missing[:HOST_CONTRACT_REPORTED_MEMBERS])
        remainder = len(missing) - len(shown)
        suffix = f" (and {remainder} more)" if remainder else ""
        raise AcceptanceError(
            f"Android APK {apk.name} lost {len(missing)} of {len(expected)} Java member(s) the native host "
            f"resolves through JNI; minification must keep them: {shown}{suffix}"
        )
    return {"apk": str(apk), "member_count": len(expected), "status": "verified"}


def _inspect_compiled_assets(compiled_assets: Path) -> tuple[str, ...]:
    compiled_assets = compiled_assets.absolute()
    if not compiled_assets.is_file() or compiled_assets.is_symlink():
        raise AcceptanceError(f"Android compiled acceptance assets are not a regular file: {compiled_assets}")

    names: list[str] = []
    seen: set[str] = set()
    total_size = 0
    try:
        with zipfile.ZipFile(compiled_assets) as archive:
            for entry in archive.infolist():
                name = entry.filename
                path = PurePosixPath(name)
                mode = entry.external_attr >> 16
                if (
                    not name
                    or "\\" in name
                    or path.is_absolute()
                    or any(part in ("", ".", "..") for part in path.parts)
                    or name in seen
                    or stat.S_ISLNK(mode)
                    or entry.flag_bits & 0x1
                ):
                    raise AcceptanceError(f"Android compiled acceptance assets contain an unsafe path: {name!r}")
                seen.add(name)
                if entry.is_dir():
                    continue
                if (
                    path.suffix in (".fs", ".fsc")
                    or "project.foundry" == name
                    or "export_presets.cfg" == name
                    or ".godot" in path.parts
                    or (path.parts[0] == ".foundry" and path.parts[:2] != (".foundry", "exported"))
                ):
                    raise AcceptanceError(f"Android compiled acceptance assets contain raw Foundry Script data: {name}")
                if entry.file_size > MAX_COMPILED_ASSET_ENTRY_BYTES:
                    raise AcceptanceError(f"Android compiled acceptance asset is too large: {name}")
                total_size += entry.file_size
                if total_size > MAX_COMPILED_ASSET_TOTAL_BYTES:
                    raise AcceptanceError("Android compiled acceptance assets exceed the size limit")
                names.append(name)

            for remap, target in COMPILED_ASSET_REMAP_TARGETS.items():
                if remap in seen:
                    actual_target = _remap_target(archive.read(remap), remap)
                    if actual_target != target:
                        raise AcceptanceError(f"Android compiled acceptance remap {remap} does not target {target}")
            if "main.tscn.remap" in seen:
                scene_target = _remap_target(archive.read("main.tscn.remap"), "main.tscn.remap")
                scene_path = scene_target[len("res://") :] if scene_target.startswith("res://") else scene_target
                if (
                    not scene_target.startswith("res://.foundry/exported/")
                    or not scene_target.endswith(".scn")
                    or scene_path not in seen
                ):
                    raise AcceptanceError(
                        "Android compiled acceptance main scene remap does not target an exported scene"
                    )
    except AcceptanceError:
        raise
    except (OSError, UnicodeError, zipfile.BadZipFile) as error:
        raise AcceptanceError(f"unable to inspect Android compiled acceptance assets: {error}") from error

    missing = sorted(COMPILED_ASSET_REQUIRED_PATHS.difference(names))
    if missing:
        raise AcceptanceError("Android compiled acceptance assets are missing required paths: " + ", ".join(missing))
    if not any(name.startswith(".foundry/exported/") and name.endswith(".scn") for name in names):
        raise AcceptanceError("Android compiled acceptance assets are missing the exported main scene")
    return tuple(names)


def prepare_compiled_assets(source: Path, output: Path) -> tuple[str, ...]:
    """Sanitize one raw exact-build export into a deterministic runtime-only ZIP."""
    source = source.absolute()
    output = output.absolute()
    if not source.is_file() or source.is_symlink():
        raise AcceptanceError(f"raw Android compiled acceptance export is not a regular file: {source}")
    if output.exists() or output.is_symlink():
        raise AcceptanceError(f"Android compiled acceptance output already exists: {output}")

    selected: dict[str, bytes] = {}
    total_size = 0
    try:
        with zipfile.ZipFile(source) as archive:
            for entry in archive.infolist():
                name = entry.filename
                path = PurePosixPath(name)
                mode = entry.external_attr >> 16
                if (
                    not name
                    or "\\" in name
                    or path.is_absolute()
                    or any(part in ("", ".", "..") for part in path.parts)
                    or name in selected
                    or stat.S_ISLNK(mode)
                    or entry.flag_bits & 0x1
                ):
                    raise AcceptanceError(f"raw Android compiled acceptance export contains an unsafe path: {name!r}")
                if entry.is_dir():
                    continue
                if path.suffix in (".fs", ".fsc") or name in ("project.foundry", "export_presets.cfg"):
                    raise AcceptanceError(f"raw Android compiled acceptance export contains source data: {name}")
                if ".godot" in path.parts:
                    continue
                if path.parts[0] == ".foundry" and path.parts[:2] != (".foundry", "exported"):
                    continue
                if entry.file_size > MAX_COMPILED_ASSET_ENTRY_BYTES:
                    raise AcceptanceError(f"raw Android compiled acceptance asset is too large: {name}")
                total_size += entry.file_size
                if total_size > MAX_COMPILED_ASSET_TOTAL_BYTES:
                    raise AcceptanceError("raw Android compiled acceptance assets exceed the size limit")
                selected[name] = archive.read(entry)
    except AcceptanceError:
        raise
    except (OSError, UnicodeError, zipfile.BadZipFile) as error:
        raise AcceptanceError(f"unable to prepare Android compiled acceptance assets: {error}") from error

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.tmp")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name in sorted(selected):
                entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = (stat.S_IFREG | 0o644) << 16
                archive.writestr(entry, selected[name])
        os.replace(temporary, output)
        return _inspect_compiled_assets(output)
    except AcceptanceError:
        output.unlink(missing_ok=True)
        raise
    except OSError as error:
        output.unlink(missing_ok=True)
        raise AcceptanceError(f"unable to write Android compiled acceptance assets: {error}") from error
    finally:
        temporary.unlink(missing_ok=True)


def _extract_compiled_assets(
    compiled_assets: Path,
    names: Sequence[str],
    destination: Path,
) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    try:
        with zipfile.ZipFile(compiled_assets) as archive:
            for name in names:
                target = destination / name
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(name) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
    except (OSError, zipfile.BadZipFile, KeyError) as error:
        raise AcceptanceError(f"unable to stage Android compiled acceptance assets: {error}") from error


def stage_scenario(
    source_template: Path,
    compiled_assets: Path,
    destination: Path,
) -> Path:
    """Validate and extract one source template, then overlay exact-build compiled smoke assets."""
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise AcceptanceError(f"Android acceptance scenario already exists: {destination}")

    inspector = _load_source_template_tool()
    try:
        names = inspector.inspect_source_template(source_template)
    except RuntimeError as error:
        raise AcceptanceError(f"invalid Android source template: {error}") from error
    compiled_names = _inspect_compiled_assets(compiled_assets)

    try:
        destination.mkdir(parents=True)
        with zipfile.ZipFile(source_template) as archive:
            for name in names:
                target = destination / name
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(name) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)

        for target in (
            destination / "src/main/assets",
            destination / "src/instrumented/assets",
        ):
            _extract_compiled_assets(compiled_assets, compiled_names, target)
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


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise AcceptanceError(f"unable to hash {path}: {error}") from error
    return digest.hexdigest()


def _create_owned_directory(path: Path, description: str) -> Path:
    path = path.absolute()
    if path.exists() or path.is_symlink():
        raise AcceptanceError(f"{description} already exists: {path}")
    try:
        path.mkdir(parents=True)
    except OSError as error:
        raise AcceptanceError(f"unable to create {description} {path}: {error}") from error
    return path


def _write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_name(f".{path.name}.tmp")
    try:
        temporary.write_text(
            json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, path)
    except OSError as error:
        raise AcceptanceError(f"unable to write Android acceptance report {path}: {error}") from error
    finally:
        temporary.unlink(missing_ok=True)


def _adb(adb: Path, serial: str, *arguments: str) -> list[str]:
    return [str(adb), "-s", serial, *arguments]


def _wait_for_probe(
    probe: Callable[[], CommandResult],
    ready: Callable[[CommandResult], bool],
    *,
    timeout: float,
    poll_interval: float,
    description: str,
    sleeper: Callable[[float], None] = time.sleep,
    monotonic: Callable[[], float] = time.monotonic,
) -> CommandResult:
    deadline = monotonic() + max(timeout, 0.0)
    last = CommandResult((), 1, "", "")
    while True:
        last = probe()
        if ready(last):
            return last
        if monotonic() >= deadline:
            raise AcceptanceError(
                f"{description} timed out after {timeout:g} seconds; "
                f"last output: {(last.stdout + last.stderr).strip()!r}"
            )
        sleeper(max(poll_interval, 0.0))


def _device_context(
    adb: Path,
    requested_serial: str | None,
    runner: Runner,
    *,
    boot_timeout: float,
    poll_interval: float,
) -> dict[str, str]:
    def device_ready(result: CommandResult) -> bool:
        if result.returncode != 0:
            return False
        try:
            select_device(result.stdout, requested_serial)
        except AcceptanceError:
            return False
        return True

    devices = _wait_for_probe(
        lambda: runner.run(
            [str(adb), "devices", "-l"],
            cwd=None,
            timeout=30,
            description="listing Android devices",
            check=False,
        ),
        device_ready,
        timeout=boot_timeout,
        poll_interval=poll_interval,
        description=(
            f"waiting for requested Android device {requested_serial} to register"
            if requested_serial is not None
            else "waiting for exactly one Android device to register"
        ),
    )
    serial = select_device(devices.stdout, requested_serial)
    _wait_for_probe(
        lambda: runner.run(
            _adb(adb, serial, "shell", "getprop", "sys.boot_completed"),
            cwd=None,
            timeout=15,
            description="reading Android boot status",
            check=False,
        ),
        lambda result: result.returncode == 0 and result.stdout.strip() == "1",
        timeout=boot_timeout,
        poll_interval=poll_interval,
        description=f"waiting for Android device {serial} to finish booting",
    )
    abi_result = runner.run(
        _adb(adb, serial, "shell", "getprop", "ro.product.cpu.abi"),
        cwd=None,
        timeout=15,
        description="reading Android device ABI",
    )
    abi = abi_result.stdout.strip()
    if abi not in SUPPORTED_ABIS:
        raise AcceptanceError(f"connected Android device reports unsupported ABI: {abi!r}")
    return {"serial": serial, "abi": abi}


def _require_instrumentation_results(scenario: Path) -> tuple[Path, ...]:
    report_root = scenario / JUNIT_REPORT_ROOT
    reports = sorted(report_root.rglob("*.xml")) if report_root.is_dir() else []
    matches: dict[str, list[tuple[Path, ElementTree.Element]]] = {method: [] for method in INSTRUMENTATION_METHODS}
    for report in reports:
        try:
            root = ElementTree.parse(report).getroot()
        except (OSError, ElementTree.ParseError) as error:
            raise AcceptanceError(f"unable to parse Android instrumentation report {report}: {error}") from error
        for test_case in root.iter():
            if test_case.tag.rsplit("}", 1)[-1] != "testcase":
                continue
            method = test_case.attrib.get("name")
            if test_case.attrib.get("classname") == INSTRUMENTATION_CLASS and method in matches:
                matches[method].append((report, test_case))
    missing = [method for method, cases in matches.items() if not cases]
    duplicates = [method for method, cases in matches.items() if len(cases) > 1]
    if missing:
        raise AcceptanceError("Android instrumentation is missing required test cases: " + ", ".join(missing))
    if duplicates:
        raise AcceptanceError(
            "Android instrumentation reported duplicate required test cases: " + ", ".join(duplicates)
        )
    matched_reports: set[Path] = set()
    for method in INSTRUMENTATION_METHODS:
        report, test_case = matches[method][0]
        matched_reports.add(report)
        outcomes = {child.tag.rsplit("}", 1)[-1] for child in test_case}
        failed = outcomes.intersection({"error", "failure", "skipped"})
        if failed:
            raise AcceptanceError(
                f"Android instrumentation {INSTRUMENTATION_CLASS}#{method} did not pass: {sorted(failed)}"
            )
    return tuple(sorted(matched_reports))


def _cleanup_packages(
    adb: Path,
    serial: str,
    application_id: str,
    runner: Runner,
) -> None:
    packages = (
        application_id,
        f"{application_id}.instrumented",
        f"{application_id}.instrumented.test",
    )
    for package in packages:
        runner.run(
            _adb(adb, serial, "uninstall", package),
            cwd=None,
            timeout=60,
            description=f"uninstalling Android package {package}",
            check=False,
        )


def _capture_logcat(
    adb: Path,
    serial: str,
    pid: str | None,
    runner: Runner,
) -> tuple[str, str | None]:
    full = runner.run(
        _adb(adb, serial, "logcat", "-d", "-v", "threadtime"),
        cwd=None,
        timeout=60,
        description="capturing full Android logcat",
        check=False,
    )
    full_contents = full.stdout + full.stderr
    if pid is None:
        return full_contents, None
    filtered = runner.run(
        _adb(adb, serial, "logcat", f"--pid={pid}", "-d", "-v", "threadtime"),
        cwd=None,
        timeout=60,
        description=f"capturing Android logcat for process {pid}",
        check=False,
    )
    return full_contents, filtered.stdout + filtered.stderr


def _wait_for_process(
    adb: Path,
    serial: str,
    application_id: str,
    runner: Runner,
    *,
    timeout: float,
    poll_interval: float,
) -> str:
    def process_probe() -> CommandResult:
        probe = runner.run(
            _adb(adb, serial, "shell", "pidof", application_id),
            cwd=None,
            timeout=15,
            description=f"reading process ID for {application_id}",
            check=False,
        )
        if probe.returncode == 0 and probe.stdout.strip():
            return probe
        # No live process yet. The launch may simply be slow, or it may already
        # have aborted; only the log distinguishes them, so fail fast on aborts
        # instead of polling an empty PID for the whole timeout.
        log = runner.run(
            _adb(adb, serial, "logcat", "-d", "-v", "threadtime"),
            cwd=None,
            timeout=60,
            description=f"reading Android startup log for {application_id}",
            check=False,
        )
        contents = log.stdout + log.stderr
        signatures = attributed_runtime_failures(contents, application_id)
        if signatures:
            raise AcceptanceError(
                f"Android package {application_id} aborted during startup with {signatures}; "
                f"logcat excerpt:\n{startup_abort_excerpt(contents, signatures)}"
            )
        return probe

    result = _wait_for_probe(
        process_probe,
        lambda probe: probe.returncode == 0 and bool(probe.stdout.strip()),
        timeout=timeout,
        poll_interval=poll_interval,
        description=f"waiting for Android process {application_id}",
    )
    pids = result.stdout.split()
    if len(pids) != 1 or not pids[0].isdigit():
        raise AcceptanceError(f"Android process {application_id} reported invalid PID output: {result.stdout!r}")
    pid = pids[0]
    for observation in range(2, PROCESS_STABILITY_OBSERVATIONS + 1):
        if poll_interval > 0:
            time.sleep(poll_interval)
        probe = runner.run(
            _adb(adb, serial, "shell", "pidof", application_id),
            cwd=None,
            timeout=15,
            description=f"confirming Android process stability for {application_id}",
            check=False,
        )
        observed_pids = probe.stdout.split() if probe.returncode == 0 else []
        if observed_pids != [pid]:
            raise AcceptanceError(
                f"Android process {application_id} did not remain stable "
                f"through observation {observation}/{PROCESS_STABILITY_OBSERVATIONS}; "
                f"expected PID {pid}, found {(probe.stdout + probe.stderr).strip()!r}"
            )
    return pid


def _wait_for_runtime_marker(
    *,
    adb: Path,
    serial: str,
    application_id: str,
    pid: str,
    marker: str,
    runner: Runner,
    timeout: float,
    poll_interval: float,
) -> None:
    def require_same_process(context: str) -> None:
        probe = runner.run(
            _adb(adb, serial, "shell", "pidof", application_id),
            cwd=None,
            timeout=15,
            description=f"confirming Android process {context} for {application_id}",
            check=False,
        )
        observed_pids = probe.stdout.split() if probe.returncode == 0 else []
        if observed_pids != [pid]:
            raise AcceptanceError(
                f"Android process {application_id} did not remain stable {context}; "
                f"expected PID {pid}, found {(probe.stdout + probe.stderr).strip()!r}"
            )

    def marker_probe() -> CommandResult:
        require_same_process("while waiting for required runtime marker")
        result = runner.run(
            _adb(adb, serial, "logcat", f"--pid={pid}", "-d", "-v", "threadtime"),
            cwd=None,
            timeout=60,
            description=f"reading Android runtime marker for {application_id}",
            check=False,
        )
        failures = runtime_log_failures(result.stdout + result.stderr)
        if failures:
            raise AcceptanceError(f"Android package {application_id} logged forbidden runtime failures: {failures}")
        return result

    _wait_for_probe(
        marker_probe,
        lambda result: marker in (result.stdout + result.stderr),
        timeout=timeout,
        poll_interval=poll_interval,
        description=f"waiting for required runtime marker {marker!r} from {application_id}",
    )
    require_same_process("after observing required runtime marker")


def _verify_apk_on_device(
    *,
    apk: Path,
    application_id: str,
    evidence_dir: Path,
    adb: Path,
    apkanalyzer: Path,
    serial: str,
    runner: Runner,
    process_timeout: float,
    poll_interval: float,
    required_runtime_marker: str | None,
) -> dict[str, Any]:
    application_id = validate_application_id(application_id)
    apk = apk.absolute()
    if not apk.is_file():
        raise AcceptanceError(f"Android acceptance APK does not exist: {apk}")

    manifest = runner.run(
        [str(apkanalyzer), "manifest", "application-id", str(apk)],
        cwd=None,
        timeout=60,
        description=f"reading application ID from {apk.name}",
    )
    manifest_application_id = manifest.stdout.strip()
    if manifest_application_id != application_id:
        raise AcceptanceError(
            f"Android APK application ID mismatch: expected {application_id!r}, found {manifest_application_id!r}"
        )

    host_contract = verify_host_contract_in_apk(
        apk=apk,
        apkanalyzer=apkanalyzer,
        runner=runner,
        repo_root=REPO_ROOT,
    )

    runner.run(
        _adb(adb, serial, "install", "-r", str(apk)),
        cwd=None,
        timeout=180,
        description=f"installing Android package {application_id}",
    )
    runner.run(
        _adb(adb, serial, "shell", "am", "force-stop", application_id),
        cwd=None,
        timeout=30,
        description=f"stopping Android package {application_id}",
        check=False,
    )
    runner.run(
        _adb(adb, serial, "logcat", "-c"),
        cwd=None,
        timeout=30,
        description="clearing Android logcat",
        check=False,
    )
    component = f"{application_id}/games.cafecito.foundry.game.FoundryAppLauncher"
    start = runner.run(
        _adb(adb, serial, "shell", "am", "start", "-W", "-n", component),
        cwd=None,
        timeout=60,
        description=f"starting Android package {application_id}",
    )
    start_output = start.stdout + start.stderr
    if re.search(r"(?m)^Status:\s*ok\s*$", start_output) is None:
        raise AcceptanceError(f"Android package {application_id} did not start successfully:\n{start_output}")

    try:
        pid = _wait_for_process(
            adb,
            serial,
            application_id,
            runner,
            timeout=process_timeout,
            poll_interval=poll_interval,
        )
    except AcceptanceError as error:
        full_logcat, _ = _capture_logcat(adb, serial, None, runner)
        # Preserve the log before propagating; a startup abort leaves no process to
        # filter on later, so this is the only capture of the actual cause.
        evidence_dir.mkdir(parents=True, exist_ok=True)
        (evidence_dir / f"{application_id}-logcat.txt").write_text(full_logcat, encoding="utf-8")
        failures = runtime_log_failures(full_logcat)
        suffix = f"; runtime failures: {failures}" if failures else ""
        raise AcceptanceError(f"{error}{suffix}") from error

    if required_runtime_marker is not None:
        _wait_for_runtime_marker(
            adb=adb,
            serial=serial,
            application_id=application_id,
            pid=pid,
            marker=required_runtime_marker,
            runner=runner,
            timeout=process_timeout,
            poll_interval=poll_interval,
        )

    full_logcat, process_logcat = _capture_logcat(adb, serial, pid, runner)
    if process_logcat is None:
        raise AcceptanceError(f"Android package {application_id} did not produce process-filtered logcat")
    (evidence_dir / f"{application_id}-logcat.txt").write_text(full_logcat, encoding="utf-8")
    (evidence_dir / f"{application_id}-process-logcat.txt").write_text(process_logcat, encoding="utf-8")
    failures = runtime_log_failures(process_logcat)
    if failures:
        raise AcceptanceError(f"Android package {application_id} logged forbidden runtime failures: {failures}")
    if required_runtime_marker is not None and required_runtime_marker not in process_logcat:
        raise AcceptanceError(
            f"Android package {application_id} did not log required runtime marker: {required_runtime_marker!r}"
        )
    return {
        "apk": str(apk),
        "apk_sha256": _sha256(apk),
        "application_id": application_id,
        "host_contract": host_contract,
        "manifest_application_id": manifest_application_id,
        "pid": pid,
        "runtime_log_failures": failures,
        "start_status": "ok",
    }


def run_source_template_acceptance(
    *,
    source_template: Path,
    compiled_assets: Path,
    work_dir: Path,
    evidence_dir: Path,
    adb: Path,
    apkanalyzer: Path,
    requested_serial: str | None,
    runner: Runner | None = None,
    boot_timeout: float = 300,
    process_timeout: float = 30,
    poll_interval: float = 2,
) -> dict[str, Any]:
    """Build, instrument, install, and start canonical and custom-ID APKs."""
    work_dir = _create_owned_directory(work_dir, "Android acceptance work directory")
    evidence_dir = _create_owned_directory(evidence_dir, "Android acceptance evidence directory")
    command_runner: Runner = runner if runner is not None else SubprocessRunner(evidence_dir)
    source_template = source_template.absolute()
    compiled_assets = compiled_assets.absolute()
    report: dict[str, Any] = {
        "mode": "source-template",
        "schema_version": 1,
        "source_template": str(source_template),
        "compiled_assets": str(compiled_assets),
    }
    try:
        _inspect_compiled_assets(compiled_assets)
        report["source_template_sha256"] = _sha256(source_template)
        report["compiled_assets_sha256"] = _sha256(compiled_assets)
        device = _device_context(
            adb,
            requested_serial,
            command_runner,
            boot_timeout=boot_timeout,
            poll_interval=poll_interval,
        )
        report["device"] = device
        scenarios: list[dict[str, Any]] = []
        for name, application_id in (
            ("canonical", DEFAULT_APPLICATION_ID),
            ("custom", CUSTOM_APPLICATION_ID),
        ):
            scenario = stage_scenario(source_template, compiled_assets, work_dir / name)
            _cleanup_packages(adb, device["serial"], application_id, command_runner)
            try:
                try:
                    command_runner.run(
                        gradle_acceptance_command(
                            scenario / "gradlew",
                            device["abi"],
                            application_id,
                        ),
                        cwd=scenario,
                        timeout=1800,
                        description=f"running {name} Android Gradle acceptance",
                        env={"ANDROID_SERIAL": device["serial"]},
                    )
                    junit_reports = _require_instrumentation_results(scenario)
                except AcceptanceError:
                    full_logcat, _ = _capture_logcat(
                        adb,
                        device["serial"],
                        None,
                        command_runner,
                    )
                    (evidence_dir / f"{name}-instrumentation-failure-logcat.txt").write_text(
                        full_logcat,
                        encoding="utf-8",
                    )
                    raise
                scenario_report = _verify_apk_on_device(
                    apk=scenario / STANDARD_APK,
                    application_id=application_id,
                    evidence_dir=evidence_dir,
                    adb=adb,
                    apkanalyzer=apkanalyzer,
                    serial=device["serial"],
                    runner=command_runner,
                    process_timeout=process_timeout,
                    poll_interval=poll_interval,
                    required_runtime_marker=STANDARD_SMOKE_READY_MARKER,
                )
                scenario_report.update(
                    {
                        "instrumentation_passed": True,
                        "instrumentation_reports": [str(report) for report in junit_reports],
                        "instrumentation_test": INSTRUMENTATION_CLASS,
                        "instrumentation_tests": list(INSTRUMENTATION_METHODS),
                        "scenario": name,
                    }
                )
                scenarios.append(scenario_report)
            finally:
                _cleanup_packages(adb, device["serial"], application_id, command_runner)
        report["scenarios"] = scenarios
        _write_json_atomic(evidence_dir / "report.json", report)
        return report
    except AcceptanceError as error:
        failure = {**report, "error": str(error), "status": "failed"}
        _write_json_atomic(evidence_dir / "failure.json", failure)
        raise


def _normalize_required_runtime_marker(marker: str) -> str:
    marker = marker.strip()
    if not marker:
        raise AcceptanceError("required runtime marker must contain non-whitespace text")
    return marker


def run_apk_acceptance(
    *,
    apks: Sequence[tuple[str, Path]],
    evidence_dir: Path,
    adb: Path,
    apkanalyzer: Path,
    requested_serial: str | None,
    runner: Runner | None = None,
    boot_timeout: float = 300,
    process_timeout: float = 30,
    poll_interval: float = 2,
    required_runtime_marker: str | None = None,
) -> dict[str, Any]:
    """Install and start already-exported, structurally validated APKs using runtime-only checks."""
    if required_runtime_marker is not None:
        required_runtime_marker = _normalize_required_runtime_marker(required_runtime_marker)
    evidence_dir = _create_owned_directory(evidence_dir, "Android acceptance evidence directory")
    command_runner: Runner = runner if runner is not None else SubprocessRunner(evidence_dir)
    report: dict[str, Any] = {
        "mode": "verify-apks",
        "required_runtime_marker": required_runtime_marker,
        "schema_version": 1,
    }
    try:
        device = _device_context(
            adb,
            requested_serial,
            command_runner,
            boot_timeout=boot_timeout,
            poll_interval=poll_interval,
        )
        report["device"] = device
        scenarios: list[dict[str, Any]] = []
        for application_id, apk in apks:
            application_id = validate_application_id(application_id)
            _cleanup_packages(adb, device["serial"], application_id, command_runner)
            try:
                scenarios.append(
                    _verify_apk_on_device(
                        apk=apk,
                        application_id=application_id,
                        evidence_dir=evidence_dir,
                        adb=adb,
                        apkanalyzer=apkanalyzer,
                        serial=device["serial"],
                        runner=command_runner,
                        process_timeout=process_timeout,
                        poll_interval=poll_interval,
                        required_runtime_marker=required_runtime_marker,
                    )
                )
            finally:
                _cleanup_packages(adb, device["serial"], application_id, command_runner)
        report["scenarios"] = scenarios
        _write_json_atomic(evidence_dir / "report.json", report)
        return report
    except AcceptanceError as error:
        failure = {**report, "error": str(error), "status": "failed"}
        _write_json_atomic(evidence_dir / "failure.json", failure)
        raise


def _parse_apk(value: str) -> tuple[str, Path]:
    application_id, separator, path = value.partition("=")
    if not separator or not path:
        raise argparse.ArgumentTypeError("--apk must use APPLICATION_ID=PATH")
    try:
        validate_application_id(application_id)
    except AcceptanceError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return application_id, Path(path)


def _parse_required_runtime_marker(value: str) -> str:
    try:
        return _normalize_required_runtime_marker(value)
    except AcceptanceError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def _add_device_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--evidence-dir", required=True, type=Path)
    parser.add_argument("--adb", required=True, type=Path)
    parser.add_argument("--apkanalyzer", required=True, type=Path)
    parser.add_argument("--serial")
    parser.add_argument("--boot-timeout", type=float, default=300)
    parser.add_argument("--process-timeout", type=float, default=30)
    parser.add_argument("--poll-interval", type=float, default=2)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    source = commands.add_parser(
        "source-template",
        help="build, instrument, install, and start the generated source template",
    )
    source.add_argument("--source-template", required=True, type=Path)
    source.add_argument("--compiled-assets", required=True, type=Path)
    source.add_argument("--work-dir", required=True, type=Path)
    _add_device_arguments(source)
    inspect_assets = commands.add_parser(
        "inspect-compiled-assets",
        help="validate one exact-build compiled Android acceptance asset archive",
    )
    inspect_assets.add_argument("--compiled-assets", required=True, type=Path)
    prepare_assets = commands.add_parser(
        "prepare-compiled-assets",
        help="sanitize one raw exact-build export into runtime-only compiled assets",
    )
    prepare_assets.add_argument("--source", required=True, type=Path)
    prepare_assets.add_argument("--output", required=True, type=Path)
    host_contract = commands.add_parser(
        "inspect-host-contract",
        help="prove built APKs keep every Java member the native host resolves through JNI",
    )
    host_contract.add_argument("--apk", required=True, action="append", type=Path)
    host_contract.add_argument("--apkanalyzer", required=True, type=Path)
    apks = commands.add_parser(
        "verify-apks",
        help="install and start already-exported, structurally validated APKs using runtime-only checks",
    )
    apks.add_argument("--apk", required=True, action="append", type=_parse_apk)
    apks.add_argument("--required-runtime-marker", type=_parse_required_runtime_marker)
    _add_device_arguments(apks)
    return parser


def _require_executable(path: Path, description: str) -> None:
    if not path.is_file() or not os.access(path, os.X_OK):
        raise AcceptanceError(f"{description} is not executable: {path}")


def main() -> int:
    arguments = _parser().parse_args()
    try:
        if arguments.command == "prepare-compiled-assets":
            paths = prepare_compiled_assets(arguments.source, arguments.output)
            report = {
                "compiled_assets": str(arguments.output.absolute()),
                "compiled_assets_sha256": _sha256(arguments.output.absolute()),
                "paths": paths,
                "schema_version": 1,
            }
            print(json.dumps(report, sort_keys=True))
            return 0
        if arguments.command == "inspect-compiled-assets":
            compiled_assets = arguments.compiled_assets.absolute()
            paths = _inspect_compiled_assets(compiled_assets)
            report = {
                "compiled_assets": str(compiled_assets),
                "compiled_assets_sha256": _sha256(compiled_assets),
                "paths": paths,
                "schema_version": 1,
            }
            print(json.dumps(report, sort_keys=True))
            return 0
        if arguments.command == "inspect-host-contract":
            _require_executable(arguments.apkanalyzer, "apkanalyzer")
            runner = SubprocessRunner()
            report = {
                "apks": [
                    verify_host_contract_in_apk(
                        apk=apk,
                        apkanalyzer=arguments.apkanalyzer,
                        runner=runner,
                        repo_root=REPO_ROOT,
                    )
                    for apk in arguments.apk
                ],
                "schema_version": 1,
            }
            print(json.dumps(report, sort_keys=True))
            return 0
        _require_executable(arguments.adb, "adb")
        _require_executable(arguments.apkanalyzer, "apkanalyzer")
        common = {
            "evidence_dir": arguments.evidence_dir,
            "adb": arguments.adb,
            "apkanalyzer": arguments.apkanalyzer,
            "requested_serial": arguments.serial,
            "boot_timeout": arguments.boot_timeout,
            "process_timeout": arguments.process_timeout,
            "poll_interval": arguments.poll_interval,
        }
        if arguments.command == "source-template":
            report = run_source_template_acceptance(
                source_template=arguments.source_template,
                compiled_assets=arguments.compiled_assets,
                work_dir=arguments.work_dir,
                **common,
            )
        else:
            report = run_apk_acceptance(
                apks=arguments.apk,
                required_runtime_marker=arguments.required_runtime_marker,
                **common,
            )
    except AcceptanceError as error:
        print(f"Android device acceptance failed: {error}", file=sys.stderr)
        return 2
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
