from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import ModuleType
from typing import Any, ClassVar, Sequence

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPO_ROOT / "platform/android/android_device_acceptance.py"


def load_tool() -> ModuleType:
    if not TOOL_PATH.is_file():
        raise AssertionError(f"missing Android device acceptance tool: {TOOL_PATH}")
    spec = importlib.util.spec_from_file_location("android_device_acceptance", TOOL_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load Android device acceptance tool: {TOOL_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def archive_bytes(entries: dict[str, bytes]) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        for name, contents in entries.items():
            archive.writestr(name, contents)
    return output.getvalue()


class FakeRunner:
    def __init__(self, tool: ModuleType, failure: str | None = None) -> None:
        self.tool = tool
        self.failure = failure
        self.commands: list[tuple[str, ...]] = []
        self.environments: list[dict[str, str] | None] = []
        self.apk_ids: dict[Path, str] = {}
        self.devices_calls = 0
        self.pid_calls: dict[str, int] = {}
        self.filtered_logcat_calls = 0
        self.runtime_marker = tool.STANDARD_SMOKE_READY_MARKER

    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path | None,
        timeout: float,
        description: str,
        check: bool = True,
        env: dict[str, str] | None = None,
    ) -> Any:
        del timeout
        arguments = tuple(str(argument) for argument in argv)
        self.commands.append(arguments)
        self.environments.append(env)
        returncode = 0
        stdout = ""
        stderr = ""

        if arguments[-2:] == ("devices", "-l"):
            self.devices_calls += 1
            stdout = "List of devices attached\n"
            if self.failure != "device-delayed" or self.devices_calls > 1:
                stdout += "emulator-5554 device product:sdk model:Virtual_Device\n"
        elif arguments[-3:] == ("shell", "getprop", "sys.boot_completed"):
            stdout = "" if self.failure == "boot-timeout" else "1\n"
        elif arguments[-3:] == ("shell", "getprop", "ro.product.cpu.abi"):
            stdout = "arm64-v8a\n"
        elif arguments and arguments[0].endswith("gradlew"):
            if self.failure == "gradle":
                returncode = 1
                stderr = "Gradle acceptance failed"
            else:
                assert cwd is not None
                application_id = self.tool.DEFAULT_APPLICATION_ID
                for argument in arguments:
                    if argument.startswith("-Pexport_package_name="):
                        application_id = argument.partition("=")[2]
                apk = cwd / "build/outputs/apk/standard/debug/android_debug.apk"
                apk.parent.mkdir(parents=True)
                apk.write_bytes(f"apk:{application_id}".encode())
                self.apk_ids[apk.resolve()] = application_id
                report = (
                    cwd
                    / "build/outputs/androidTest-results/connected/debug/flavors/instrumented"
                    / "TEST-foundry-acceptance.xml"
                )
                report.parent.mkdir(parents=True)
                test_names = list(self.tool.INSTRUMENTATION_METHODS)
                if self.failure == "missing-junit":
                    test_names.pop()
                test_cases = []
                for index, test_name in enumerate(test_names):
                    failure = "<failure>failed</failure>" if self.failure == "failed-junit" and index == 0 else ""
                    test_cases.append(
                        '<testcase classname="games.cafecito.foundry.game.FoundryAppTest" '
                        f'name="{test_name}">{failure}</testcase>'
                    )
                report.write_text(
                    f'<testsuite tests="{len(test_cases)}" failures="0">' + "".join(test_cases) + "</testsuite>",
                    encoding="utf-8",
                )
        elif len(arguments) >= 4 and arguments[1:3] == ("manifest", "application-id"):
            apk = Path(arguments[3]).resolve()
            stdout = (
                "wrong.example.application\n" if self.failure == "wrong-application-id" else f"{self.apk_ids[apk]}\n"
            )
        elif arguments[-3:-1] == ("install", "-r"):
            if self.failure == "install":
                returncode = 1
                stderr = "INSTALL_FAILED"
            else:
                stdout = "Success\n"
        elif "am" in arguments and "start" in arguments:
            stdout = "Status: timeout\n" if self.failure == "start" else "Status: ok\nActivity: FoundryAppLauncher\n"
        elif "pidof" in arguments:
            application_id = arguments[-1]
            self.pid_calls[application_id] = self.pid_calls.get(application_id, 0) + 1
            if (
                self.failure == "process-timeout"
                or (self.failure == "process-exits" and self.pid_calls[application_id] > 1)
                or (
                    self.failure == "process-exits-while-ready"
                    and self.pid_calls[application_id] > self.tool.PROCESS_STABILITY_OBSERVATIONS
                )
            ):
                returncode = 1
            else:
                stdout = "4242\n"
        elif "logcat" in arguments and "-d" in arguments:
            filtered = any(argument.startswith("--pid=") for argument in arguments)
            if filtered:
                self.filtered_logcat_calls += 1
            if self.failure is not None and self.failure.startswith("runtime-log:"):
                stdout = f"{self.failure.partition(':')[2]}\n"
            elif (
                self.failure is not None
                and self.failure.startswith("system-runtime-log:")
                and not any(argument.startswith("--pid=") for argument in arguments)
            ):
                stdout = f"{self.failure.partition(':')[2]}\n"
            elif self.failure == "missing-ready-marker":
                stdout = "Foundry process running without project readiness\n"
            elif self.failure == "delayed-ready-marker" and filtered and self.filtered_logcat_calls == 1:
                stdout = "Foundry process starting\n"
            else:
                stdout = f"{self.runtime_marker}\n"
        elif "uninstall" in arguments:
            stdout = "Success\n"

        result = self.tool.CommandResult(
            argv=arguments,
            returncode=returncode,
            stdout=stdout,
            stderr=stderr,
        )
        if check and returncode != 0:
            raise self.tool.AcceptanceError(f"{description} failed with exit {returncode}:\n{stdout}{stderr}")
        return result


class AndroidDeviceAcceptanceTests(unittest.TestCase):
    tool: ClassVar[ModuleType]

    @classmethod
    def setUpClass(cls) -> None:
        cls.tool = load_tool()

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.workspace = Path(self.temporary_directory.name)
        self.acceptance_run_index = 0
        self.source_template = self.workspace / "android_source.zip"
        self.compiled_assets = self.workspace / "android-instrumented-assets.zip"
        host_aar = archive_bytes(
            {
                "AndroidManifest.xml": b"<manifest />\n",
                "classes.jar": archive_bytes({"games/cafecito/foundry/Host.class": b"host"}),
            }
        )
        self.source_entries = {
            "build.gradle": b"plugins { id 'com.android.application' }\n",
            "config.gradle": b"ext.versions = [:]\n",
            "gradlew": b"#!/bin/sh\n",
            "gradle/wrapper/gradle-wrapper.jar": archive_bytes(
                {"org/gradle/wrapper/GradleWrapperMain.class": b"wrapper"}
            ),
            "gradle/wrapper/gradle-wrapper.properties": b"distributionUrl=gradle\n",
            "settings.gradle": b'rootProject.name = "FoundryAcceptance"\n',
            "src/main/AndroidManifest.xml": b"<manifest />\n",
            "src/main/assets/.gitignore": b"*\n",
            "src/instrumented/assets/project.foundry": b"instrumented project",
            "src/instrumented/assets/main.fs": b"extends Node\n",
            "libs/debug/foundry-debug.aar": host_aar,
            "libs/dev/foundry-dev.aar": host_aar,
            "libs/release/foundry-release.aar": host_aar,
        }
        self.write_source_template(self.source_entries)
        self.write_compiled_assets(
            {
                "project.binary": b"compiled project",
                "main.fsb": b"compiled main",
                "main.fs.remap": b'path="res://main.fsb"\n',
                "main.tscn.remap": b'path="res://.foundry/exported/main.scn"\n',
                ".foundry/exported/main.scn": b"compiled scene",
                "test/base_test.fsb": b"compiled base test",
                "test/base_test.fs.remap": b'path="res://test/base_test.fsb"\n',
                "test/file_access/file_access_tests.fsb": b"compiled file tests",
                "test/file_access/file_access_tests.fs.remap": (
                    b'path="res://test/file_access/file_access_tests.fsb"\n'
                ),
                "test/javaclasswrapper/java_class_wrapper_tests.fsb": b"compiled wrapper tests",
                "test/javaclasswrapper/java_class_wrapper_tests.fs.remap": (
                    b'path="res://test/javaclasswrapper/java_class_wrapper_tests.fsb"\n'
                ),
            }
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_source_template(self, entries: dict[str, bytes]) -> None:
        with zipfile.ZipFile(self.source_template, "w") as archive:
            for name, contents in entries.items():
                archive.writestr(name, contents)

    def write_compiled_assets(self, entries: dict[str, bytes]) -> None:
        with zipfile.ZipFile(self.compiled_assets, "w") as archive:
            for name, contents in entries.items():
                archive.writestr(name, contents)

    def test_application_ids_require_lowercase_reverse_dns(self) -> None:
        self.assertEqual(
            "dev.example.foundryacceptance",
            self.tool.validate_application_id("dev.example.foundryacceptance"),
        )
        for invalid in ("", "Foundry", "dev..foundry", "9dev.example", "dev.Example"):
            with self.subTest(invalid=invalid):
                with self.assertRaises(self.tool.AcceptanceError):
                    self.tool.validate_application_id(invalid)

    def test_select_device_requires_one_ready_device_or_matching_serial(self) -> None:
        output = (
            "List of devices attached\nemulator-5554 device product:sdk model:Virtual_Device\noffline-5556 offline\n"
        )
        self.assertEqual("emulator-5554", self.tool.select_device(output, None))
        self.assertEqual(
            "emulator-5554",
            self.tool.select_device(output, "emulator-5554"),
        )
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.select_device("List of devices attached\n", None)
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.select_device(
                output + "emulator-5558 device product:sdk model:Other\n",
                None,
            )
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.select_device(output, "emulator-5558")

    def test_runtime_log_rejects_linkage_class_and_fatal_failures(self) -> None:
        self.assertEqual([], self.tool.runtime_log_failures("Foundry main loop started"))
        for signature in (
            "java.lang.UnsatisfiedLinkError",
            "java.lang.NoClassDefFoundError",
            "java.lang.ClassNotFoundException",
            "FATAL EXCEPTION: main",
            'couldn\'t find "libfoundry_android.so"',
        ):
            with self.subTest(signature=signature):
                self.assertTrue(self.tool.runtime_log_failures(signature))

    def test_stage_scenario_preserves_template_and_adds_smoke_assets(self) -> None:
        scenario = self.tool.stage_scenario(
            self.source_template,
            self.compiled_assets,
            self.workspace / "canonical",
        )
        self.assertTrue((scenario / "libs/debug/foundry-debug.aar").is_file())
        self.assertEqual(
            b"compiled project",
            (scenario / "src/main/assets/project.binary").read_bytes(),
        )
        self.assertEqual(
            b"compiled main",
            (scenario / "src/main/assets/main.fsb").read_bytes(),
        )
        self.assertEqual(
            b"compiled project",
            (scenario / "src/instrumented/assets/project.binary").read_bytes(),
        )
        self.assertFalse((scenario / "src/instrumented/assets/main.fs").exists())
        self.assertFalse((scenario / "src/main/assets/project.foundry").exists())
        self.assertTrue((scenario / "gradlew").stat().st_mode & stat.S_IXUSR)

    def test_stage_scenario_rejects_raw_or_incomplete_compiled_assets(self) -> None:
        self.write_compiled_assets({"main.fs": b"extends Node\n"})
        with self.assertRaisesRegex(self.tool.AcceptanceError, "raw Foundry Script"):
            self.tool.stage_scenario(
                self.source_template,
                self.compiled_assets,
                self.workspace / "raw-assets",
            )

        self.write_compiled_assets({"project.binary": b"compiled project"})
        with self.assertRaisesRegex(self.tool.AcceptanceError, "missing required"):
            self.tool.stage_scenario(
                self.source_template,
                self.compiled_assets,
                self.workspace / "incomplete-assets",
            )

    def test_prepare_compiled_assets_removes_caches_and_is_deterministic(self) -> None:
        raw = self.workspace / "raw-compiled-assets.zip"
        with zipfile.ZipFile(self.compiled_assets) as archive:
            entries = {name: archive.read(name) for name in archive.namelist()}
        entries.update(
            {
                ".godot/global_script_class_cache.cfg": b"stale",
                ".foundry/global_script_class_cache.cfg": b"generated",
                ".foundry/uid_cache.bin": b"generated",
            }
        )
        with zipfile.ZipFile(raw, "w") as archive:
            for name, contents in reversed(tuple(entries.items())):
                archive.writestr(name, contents)

        first = self.workspace / "prepared-first.zip"
        second = self.workspace / "prepared-second.zip"
        first_names = self.tool.prepare_compiled_assets(raw, first)
        second_names = self.tool.prepare_compiled_assets(raw, second)

        self.assertEqual(first_names, second_names)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertFalse(any(name.startswith(".godot/") for name in first_names))
        self.assertFalse(
            any(name.startswith(".foundry/") and not name.startswith(".foundry/exported/") for name in first_names)
        )

    def test_compiled_assets_require_exact_valid_remap_targets(self) -> None:
        with zipfile.ZipFile(self.compiled_assets) as archive:
            entries = {name: archive.read(name) for name in archive.namelist()}
        mutations = (
            (
                "substring-bypass",
                "main.fs.remap",
                b'# res://main.fsb\npath="res://wrong.fsb"\n',
                "does not target",
            ),
            (
                "invalid-utf8",
                "main.fs.remap",
                b"\xff\xfe",
                "not valid UTF-8",
            ),
            (
                "missing-scene",
                "main.tscn.remap",
                b'path="res://.foundry/exported/missing.scn"\n',
                "does not target an exported scene",
            ),
        )
        for label, path, contents, message in mutations:
            with self.subTest(label=label):
                self.write_compiled_assets({**entries, path: contents})
                with self.assertRaisesRegex(self.tool.AcceptanceError, message):
                    self.tool.stage_scenario(
                        self.source_template,
                        self.compiled_assets,
                        self.workspace / label,
                    )

    def test_stage_scenario_rejects_unsafe_or_symbolic_link_entries(self) -> None:
        unsafe_entries = {**self.source_entries, "../escaped": b"unsafe"}
        self.write_source_template(unsafe_entries)
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.stage_scenario(
                self.source_template,
                self.compiled_assets,
                self.workspace / "unsafe",
            )

        self.write_source_template(self.source_entries)
        with zipfile.ZipFile(self.source_template, "a") as archive:
            entry = zipfile.ZipInfo("linked-gradlew")
            entry.create_system = 3
            entry.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(entry, "gradlew")
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.stage_scenario(
                self.source_template,
                self.compiled_assets,
                self.workspace / "linked",
            )

    def test_gradle_command_omits_canonical_override_and_sets_custom_override(self) -> None:
        canonical = self.tool.gradle_acceptance_command(
            Path("/scenario/gradlew"),
            "arm64-v8a",
            self.tool.DEFAULT_APPLICATION_ID,
        )
        self.assertNotIn("-Pexport_package_name=", " ".join(canonical))
        custom = self.tool.gradle_acceptance_command(
            Path("/scenario/gradlew"),
            "arm64-v8a",
            self.tool.CUSTOM_APPLICATION_ID,
        )
        self.assertIn(
            f"-Pexport_package_name={self.tool.CUSTOM_APPLICATION_ID}",
            custom,
        )
        self.assertIn("assembleStandardDebug", custom)
        self.assertIn("connectedInstrumentedDebugAndroidTest", custom)
        self.assertIn("-Pexport_enabled_abis=arm64-v8a|", custom)
        self.assertIn(
            ("-Pandroid.testInstrumentationRunnerArguments.class=games.cafecito.foundry.game.FoundryAppTest"),
            custom,
        )

    def test_gradle_command_rejects_unsupported_device_abi(self) -> None:
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.gradle_acceptance_command(
                Path("/scenario/gradlew"),
                "riscv64",
                self.tool.DEFAULT_APPLICATION_ID,
            )

    def run_acceptance(
        self,
        failure: str | None = None,
        *,
        boot_timeout: float = 0.0,
        process_timeout: float = 0.0,
        poll_interval: float = 0.0,
    ) -> tuple[dict[str, Any], FakeRunner, Path]:
        self.acceptance_run_index += 1
        evidence_dir = self.workspace / f"evidence-{self.acceptance_run_index}"
        runner = FakeRunner(self.tool, failure)
        report = self.tool.run_source_template_acceptance(
            source_template=self.source_template,
            compiled_assets=self.compiled_assets,
            work_dir=self.workspace / f"work-{self.acceptance_run_index}",
            evidence_dir=evidence_dir,
            adb=Path("/sdk/platform-tools/adb"),
            apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
            requested_serial="emulator-5554",
            runner=runner,
            boot_timeout=boot_timeout,
            process_timeout=process_timeout,
            poll_interval=poll_interval,
        )
        return report, runner, evidence_dir

    def test_acceptance_runs_canonical_and_custom_scenarios(self) -> None:
        report, runner, evidence_dir = self.run_acceptance()
        self.assertEqual("emulator-5554", report["device"]["serial"])
        self.assertEqual("arm64-v8a", report["device"]["abi"])
        self.assertEqual(
            [
                self.tool.DEFAULT_APPLICATION_ID,
                self.tool.CUSTOM_APPLICATION_ID,
            ],
            [scenario["application_id"] for scenario in report["scenarios"]],
        )
        self.assertTrue(all(scenario["instrumentation_passed"] for scenario in report["scenarios"]))
        self.assertTrue(
            all(
                scenario["instrumentation_tests"] == list(self.tool.INSTRUMENTATION_METHODS)
                for scenario in report["scenarios"]
            )
        )
        self.assertTrue(all(scenario["start_status"] == "ok" for scenario in report["scenarios"]))
        self.assertTrue(all(scenario["pid"] == "4242" for scenario in report["scenarios"]))
        self.assertTrue(all(len(scenario["apk_sha256"]) == 64 for scenario in report["scenarios"]))

        report_path = evidence_dir / "report.json"
        self.assertEqual(report, json.loads(report_path.read_text(encoding="utf-8")))
        gradle_commands = [command for command in runner.commands if command[0].endswith("gradlew")]
        self.assertEqual(2, len(gradle_commands))
        self.assertFalse(any("-Pexport_package_name=" in argument for argument in gradle_commands[0]))
        self.assertIn(
            f"-Pexport_package_name={self.tool.CUSTOM_APPLICATION_ID}",
            gradle_commands[1],
        )
        self.assertTrue(
            all(
                environment == {"ANDROID_SERIAL": "emulator-5554"} for environment in runner.environments if environment
            )
        )

    def test_acceptance_requires_the_named_passing_junit_case(self) -> None:
        for failure, message in (
            ("missing-junit", "missing required test cases"),
            ("failed-junit", "did not pass"),
        ):
            with self.subTest(failure=failure):
                evidence_dir = self.workspace / f"evidence-{self.acceptance_run_index + 1}"
                with self.assertRaisesRegex(self.tool.AcceptanceError, message):
                    self.run_acceptance(failure)
                self.assertTrue((evidence_dir / "canonical-instrumentation-failure-logcat.txt").is_file())

    def test_acceptance_rejects_boot_manifest_install_start_process_and_log_failures(self) -> None:
        for failure, message in (
            ("boot-timeout", "finish booting"),
            ("wrong-application-id", "application ID mismatch"),
            ("install", "INSTALL_FAILED"),
            ("start", "did not start successfully"),
            ("process-timeout", "waiting for Android process"),
            ("process-exits", "did not remain stable"),
            ("process-exits-while-ready", "while waiting for required runtime marker"),
            ("missing-ready-marker", "required runtime marker"),
        ):
            with self.subTest(failure=failure):
                with self.assertRaisesRegex(self.tool.AcceptanceError, message):
                    self.run_acceptance(failure)

        for signature in self.tool.RUNTIME_FAILURE_PATTERNS:
            with self.subTest(signature=signature):
                with self.assertRaisesRegex(self.tool.AcceptanceError, "forbidden runtime failures"):
                    self.run_acceptance(f"runtime-log:{signature}")

    def test_acceptance_waits_for_delayed_runtime_readiness_and_reconfirms_the_pid(self) -> None:
        report, runner, _ = self.run_acceptance(
            "delayed-ready-marker",
            process_timeout=0.1,
        )
        self.assertTrue(all(scenario["start_status"] == "ok" for scenario in report["scenarios"]))
        self.assertGreaterEqual(runner.filtered_logcat_calls, 4)
        self.assertTrue(
            all(
                observations >= self.tool.PROCESS_STABILITY_OBSERVATIONS + 2
                for observations in runner.pid_calls.values()
            )
        )
        self.assertGreaterEqual(
            max(runner.pid_calls.values()),
            self.tool.PROCESS_STABILITY_OBSERVATIONS + 3,
        )

    def test_acceptance_ignores_unrelated_system_runtime_failures_after_process_starts(self) -> None:
        try:
            report, _, _ = self.run_acceptance("system-runtime-log:FATAL EXCEPTION: unrelated")
        except self.tool.AcceptanceError as error:
            self.fail(f"unrelated system logcat failed acceptance: {error}")

        self.assertTrue(all(not scenario["runtime_log_failures"] for scenario in report["scenarios"]))

    def test_acceptance_waits_for_requested_device_registration(self) -> None:
        report, runner, _ = self.run_acceptance("device-delayed", boot_timeout=0.1)
        self.assertEqual("emulator-5554", report["device"]["serial"])
        self.assertEqual(2, runner.devices_calls)

    def test_apk_only_acceptance_verifies_both_exported_application_ids(self) -> None:
        canonical = self.workspace / "canonical-no-marker.apk"
        custom = self.workspace / "custom-no-marker.apk"
        canonical.write_bytes(b"canonical")
        custom.write_bytes(b"custom")
        runner = FakeRunner(self.tool)
        runner.apk_ids = {
            canonical.resolve(): self.tool.DEFAULT_APPLICATION_ID,
            custom.resolve(): self.tool.CUSTOM_APPLICATION_ID,
        }
        report = self.tool.run_apk_acceptance(
            apks=(
                (self.tool.DEFAULT_APPLICATION_ID, canonical),
                (self.tool.CUSTOM_APPLICATION_ID, custom),
            ),
            evidence_dir=self.workspace / "apk-no-marker-evidence",
            adb=Path("/sdk/platform-tools/adb"),
            apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
            requested_serial="emulator-5554",
            runner=runner,
            boot_timeout=0.0,
            process_timeout=0.0,
            poll_interval=0.0,
        )
        self.assertEqual("verify-apks", report["mode"])
        self.assertIsNone(report["required_runtime_marker"])
        self.assertEqual(
            [self.tool.DEFAULT_APPLICATION_ID, self.tool.CUSTOM_APPLICATION_ID],
            [scenario["application_id"] for scenario in report["scenarios"]],
        )

    def test_apk_only_acceptance_normalizes_required_marker_for_both_application_ids(self) -> None:
        canonical = self.workspace / "canonical.apk"
        custom = self.workspace / "custom.apk"
        canonical.write_bytes(b"canonical")
        custom.write_bytes(b"custom")
        runner = FakeRunner(self.tool)
        marker = "FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY"
        runner.runtime_marker = marker
        runner.apk_ids = {
            canonical.resolve(): self.tool.DEFAULT_APPLICATION_ID,
            custom.resolve(): self.tool.CUSTOM_APPLICATION_ID,
        }
        report = self.tool.run_apk_acceptance(
            apks=(
                (self.tool.DEFAULT_APPLICATION_ID, canonical),
                (self.tool.CUSTOM_APPLICATION_ID, custom),
            ),
            evidence_dir=self.workspace / "apk-evidence",
            adb=Path("/sdk/platform-tools/adb"),
            apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
            requested_serial="emulator-5554",
            runner=runner,
            boot_timeout=0.0,
            process_timeout=0.0,
            poll_interval=0.0,
            required_runtime_marker=f" \t{marker}\n",
        )
        self.assertEqual("verify-apks", report["mode"])
        self.assertEqual(marker, report["required_runtime_marker"])
        self.assertEqual(
            [self.tool.DEFAULT_APPLICATION_ID, self.tool.CUSTOM_APPLICATION_ID],
            [scenario["application_id"] for scenario in report["scenarios"]],
        )
        self.assertEqual(
            report,
            json.loads((self.workspace / "apk-evidence/report.json").read_text(encoding="utf-8")),
        )

    def test_apk_only_acceptance_rejects_empty_or_whitespace_runtime_marker(self) -> None:
        for index, marker in enumerate(("", " \t ")):
            with self.subTest(marker=repr(marker)):
                apk = self.workspace / f"invalid-marker-{index}.apk"
                apk.write_bytes(b"apk")
                runner = FakeRunner(self.tool)
                runner.apk_ids = {apk.resolve(): self.tool.DEFAULT_APPLICATION_ID}

                with self.assertRaisesRegex(
                    self.tool.AcceptanceError,
                    "required runtime marker must contain non-whitespace text",
                ):
                    self.tool.run_apk_acceptance(
                        apks=((self.tool.DEFAULT_APPLICATION_ID, apk),),
                        evidence_dir=self.workspace / f"invalid-marker-evidence-{index}",
                        adb=Path("/sdk/platform-tools/adb"),
                        apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
                        requested_serial="emulator-5554",
                        runner=runner,
                        boot_timeout=0.0,
                        process_timeout=0.0,
                        poll_interval=0.0,
                        required_runtime_marker=marker,
                    )

    def test_verify_apks_cli_rejects_empty_or_whitespace_runtime_marker(self) -> None:
        parser = self.tool._parser()
        for marker in ("", " \t "):
            with self.subTest(marker=repr(marker)):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit) as error:
                    parser.parse_args(
                        [
                            "verify-apks",
                            "--apk",
                            f"{self.tool.DEFAULT_APPLICATION_ID}=/tmp/example.apk",
                            "--required-runtime-marker",
                            marker,
                            "--evidence-dir",
                            "/tmp/evidence",
                            "--adb",
                            "/tmp/adb",
                            "--apkanalyzer",
                            "/tmp/apkanalyzer",
                        ]
                    )
                self.assertEqual(2, error.exception.code)
                self.assertIn("required runtime marker must contain non-whitespace text", stderr.getvalue())

    def test_verify_apks_executable_rejects_invalid_runtime_marker_before_device_work(self) -> None:
        for index, marker in enumerate(("", " \t ")):
            with self.subTest(marker=repr(marker)):
                evidence_dir = self.workspace / f"invalid-cli-marker-evidence-{index}"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(TOOL_PATH),
                        "verify-apks",
                        "--apk",
                        f"{self.tool.DEFAULT_APPLICATION_ID}={self.workspace / 'example.apk'}",
                        "--required-runtime-marker",
                        marker,
                        "--evidence-dir",
                        str(evidence_dir),
                        "--adb",
                        str(self.workspace / "adb-must-not-run"),
                        "--apkanalyzer",
                        str(self.workspace / "apkanalyzer-must-not-run"),
                    ],
                    cwd=REPO_ROOT,
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=10,
                )

                self.assertEqual(2, result.returncode)
                self.assertIn(
                    "required runtime marker must contain non-whitespace text",
                    result.stderr,
                )
                self.assertNotIn("is not executable", result.stderr)
                self.assertFalse(evidence_dir.exists())

    def test_verify_apks_cli_normalizes_runtime_marker(self) -> None:
        marker = "FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY"

        arguments = self.tool._parser().parse_args(
            [
                "verify-apks",
                "--apk",
                f"{self.tool.DEFAULT_APPLICATION_ID}=/tmp/example.apk",
                "--required-runtime-marker",
                f" \t{marker}\n",
                "--evidence-dir",
                "/tmp/evidence",
                "--adb",
                "/tmp/adb",
                "--apkanalyzer",
                "/tmp/apkanalyzer",
            ]
        )

        self.assertEqual(marker, arguments.required_runtime_marker)

    def test_apk_only_acceptance_rejects_missing_required_runtime_marker(self) -> None:
        apk = self.workspace / "canonical.apk"
        apk.write_bytes(b"canonical")
        runner = FakeRunner(self.tool, "missing-ready-marker")
        runner.apk_ids = {apk.resolve(): self.tool.DEFAULT_APPLICATION_ID}
        marker = "FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY"
        with self.assertRaisesRegex(self.tool.AcceptanceError, "waiting for required runtime marker.*timed out"):
            self.tool.run_apk_acceptance(
                apks=((self.tool.DEFAULT_APPLICATION_ID, apk),),
                evidence_dir=self.workspace / "apk-marker-evidence",
                adb=Path("/sdk/platform-tools/adb"),
                apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
                requested_serial="emulator-5554",
                runner=runner,
                boot_timeout=0.0,
                process_timeout=0.0,
                poll_interval=0.0,
                required_runtime_marker=marker,
            )

    def test_apk_only_required_marker_does_not_mask_process_exit(self) -> None:
        apk = self.workspace / "canonical.apk"
        apk.write_bytes(b"canonical")
        runner = FakeRunner(self.tool, "process-exits-while-ready")
        runner.runtime_marker = "FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY"
        runner.apk_ids = {apk.resolve(): self.tool.DEFAULT_APPLICATION_ID}
        with self.assertRaisesRegex(self.tool.AcceptanceError, "did not remain stable"):
            self.tool.run_apk_acceptance(
                apks=((self.tool.DEFAULT_APPLICATION_ID, apk),),
                evidence_dir=self.workspace / "apk-exit-evidence",
                adb=Path("/sdk/platform-tools/adb"),
                apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
                requested_serial="emulator-5554",
                runner=runner,
                boot_timeout=0.0,
                process_timeout=0.0,
                poll_interval=0.0,
                required_runtime_marker=runner.runtime_marker,
            )

    def test_primary_failure_still_uninstalls_target_and_test_packages(self) -> None:
        runner = FakeRunner(self.tool, "start")
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.run_source_template_acceptance(
                source_template=self.source_template,
                compiled_assets=self.compiled_assets,
                work_dir=self.workspace / "work",
                evidence_dir=self.workspace / "evidence",
                adb=Path("/sdk/platform-tools/adb"),
                apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
                requested_serial="emulator-5554",
                runner=runner,
                boot_timeout=0.0,
                process_timeout=0.0,
                poll_interval=0.0,
            )
        uninstall_commands = [command for command in runner.commands if "uninstall" in command]
        self.assertEqual(
            {
                self.tool.DEFAULT_APPLICATION_ID,
                f"{self.tool.DEFAULT_APPLICATION_ID}.instrumented",
                f"{self.tool.DEFAULT_APPLICATION_ID}.instrumented.test",
            },
            {command[-1] for command in uninstall_commands},
        )

    def test_gradle_failure_is_reported_without_claiming_device_success(self) -> None:
        evidence_dir = self.workspace / "gradle-failure-evidence"
        runner = FakeRunner(self.tool, "gradle")
        with self.assertRaisesRegex(self.tool.AcceptanceError, "Gradle acceptance failed"):
            self.tool.run_source_template_acceptance(
                source_template=self.source_template,
                compiled_assets=self.compiled_assets,
                work_dir=self.workspace / "gradle-failure-work",
                evidence_dir=evidence_dir,
                adb=Path("/sdk/platform-tools/adb"),
                apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
                requested_serial="emulator-5554",
                runner=runner,
                boot_timeout=0.0,
                process_timeout=0.0,
                poll_interval=0.0,
            )
        logcat = evidence_dir / "canonical-instrumentation-failure-logcat.txt"
        self.assertEqual(
            "Foundry Android standard runtime smoke ready\n",
            logcat.read_text(encoding="utf-8"),
        )
        gradle_index = next(index for index, command in enumerate(runner.commands) if command[0].endswith("gradlew"))
        logcat_index = next(
            index for index, command in enumerate(runner.commands) if "logcat" in command and "-d" in command
        )
        cleanup_index = next(
            index
            for index, command in enumerate(runner.commands[gradle_index + 1 :], gradle_index + 1)
            if "uninstall" in command
        )
        self.assertLess(gradle_index, logcat_index)
        self.assertLess(logcat_index, cleanup_index)


if __name__ == "__main__":
    unittest.main()
