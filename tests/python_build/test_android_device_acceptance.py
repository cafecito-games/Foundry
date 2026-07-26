from __future__ import annotations

import importlib.util
import json
import stat
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


class FakeRunner:
    def __init__(self, tool: ModuleType, failure: str | None = None) -> None:
        self.tool = tool
        self.failure = failure
        self.commands: list[tuple[str, ...]] = []
        self.environments: list[dict[str, str] | None] = []
        self.apk_ids: dict[Path, str] = {}
        self.devices_calls = 0

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
                test_name = (
                    "anotherTest" if self.failure == "missing-junit" else "runtimeBootsWithoutLegacyPluginMetadata"
                )
                failure = "<failure>failed</failure>" if self.failure == "failed-junit" else ""
                report.write_text(
                    (
                        '<testsuite tests="1" failures="0">'
                        '<testcase classname="games.cafecito.foundry.game.FoundryAppTest" '
                        f'name="{test_name}">{failure}</testcase></testsuite>'
                    ),
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
            if self.failure == "process-timeout":
                returncode = 1
            else:
                stdout = "4242\n"
        elif "logcat" in arguments and "-d" in arguments:
            if self.failure is not None and self.failure.startswith("runtime-log:"):
                stdout = f"{self.failure.partition(':')[2]}\n"
            elif (
                self.failure is not None
                and self.failure.startswith("system-runtime-log:")
                and not any(argument.startswith("--pid=") for argument in arguments)
            ):
                stdout = f"{self.failure.partition(':')[2]}\n"
            else:
                stdout = "Foundry ready\n"
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
        self.source_entries = {
            "build.gradle": b"plugins { id 'com.android.application' }\n",
            "config.gradle": b"ext.versions = [:]\n",
            "gradlew": b"#!/bin/sh\n",
            "gradle/wrapper/gradle-wrapper.jar": b"wrapper",
            "gradle/wrapper/gradle-wrapper.properties": b"distributionUrl=gradle\n",
            "settings.gradle": b'rootProject.name = "FoundryAcceptance"\n',
            "src/main/AndroidManifest.xml": b"<manifest />\n",
            "src/main/assets/.gitignore": b"*\n",
            "src/instrumented/assets/project.foundry": b"foundry project",
            "src/instrumented/assets/scenes/main.tscn": b"foundry scene",
            "libs/debug/foundry-debug.aar": b"debug aar",
            "libs/dev/foundry-dev.aar": b"dev aar",
            "libs/release/foundry-release.aar": b"release aar",
        }
        self.write_source_template(self.source_entries)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_source_template(self, entries: dict[str, bytes]) -> None:
        with zipfile.ZipFile(self.source_template, "w") as archive:
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
            self.workspace / "canonical",
        )
        self.assertTrue((scenario / "libs/debug/foundry-debug.aar").is_file())
        self.assertEqual(
            b"foundry project",
            (scenario / "src/main/assets/project.foundry").read_bytes(),
        )
        self.assertEqual(
            b"foundry scene",
            (scenario / "src/main/assets/scenes/main.tscn").read_bytes(),
        )
        self.assertTrue((scenario / "gradlew").stat().st_mode & stat.S_IXUSR)

    def test_stage_scenario_rejects_unsafe_or_symbolic_link_entries(self) -> None:
        unsafe_entries = {**self.source_entries, "../escaped": b"unsafe"}
        self.write_source_template(unsafe_entries)
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.stage_scenario(
                self.source_template,
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
            (
                "-Pandroid.testInstrumentationRunnerArguments.class="
                "games.cafecito.foundry.game.FoundryAppTest"
                "#runtimeBootsWithoutLegacyPluginMetadata"
            ),
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
    ) -> tuple[dict[str, Any], FakeRunner, Path]:
        self.acceptance_run_index += 1
        evidence_dir = self.workspace / f"evidence-{self.acceptance_run_index}"
        runner = FakeRunner(self.tool, failure)
        report = self.tool.run_source_template_acceptance(
            source_template=self.source_template,
            work_dir=self.workspace / f"work-{self.acceptance_run_index}",
            evidence_dir=evidence_dir,
            adb=Path("/sdk/platform-tools/adb"),
            apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
            requested_serial="emulator-5554",
            runner=runner,
            boot_timeout=boot_timeout,
            process_timeout=0.0,
            poll_interval=0.0,
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
            ("missing-junit", "did not report exactly one"),
            ("failed-junit", "did not pass"),
        ):
            with self.subTest(failure=failure):
                with self.assertRaisesRegex(self.tool.AcceptanceError, message):
                    self.run_acceptance(failure)

    def test_acceptance_rejects_boot_manifest_install_start_process_and_log_failures(self) -> None:
        for failure, message in (
            ("boot-timeout", "finish booting"),
            ("wrong-application-id", "application ID mismatch"),
            ("install", "INSTALL_FAILED"),
            ("start", "did not start successfully"),
            ("process-timeout", "waiting for Android process"),
        ):
            with self.subTest(failure=failure):
                with self.assertRaisesRegex(self.tool.AcceptanceError, message):
                    self.run_acceptance(failure)

        for signature in self.tool.RUNTIME_FAILURE_PATTERNS:
            with self.subTest(signature=signature):
                with self.assertRaisesRegex(self.tool.AcceptanceError, "forbidden runtime failures"):
                    self.run_acceptance(f"runtime-log:{signature}")

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
        canonical = self.workspace / "canonical.apk"
        custom = self.workspace / "custom.apk"
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
            evidence_dir=self.workspace / "apk-evidence",
            adb=Path("/sdk/platform-tools/adb"),
            apkanalyzer=Path("/sdk/cmdline-tools/latest/bin/apkanalyzer"),
            requested_serial="emulator-5554",
            runner=runner,
            boot_timeout=0.0,
            process_timeout=0.0,
            poll_interval=0.0,
        )
        self.assertEqual("verify-apks", report["mode"])
        self.assertEqual(
            [self.tool.DEFAULT_APPLICATION_ID, self.tool.CUSTOM_APPLICATION_ID],
            [scenario["application_id"] for scenario in report["scenarios"]],
        )
        self.assertEqual(
            report,
            json.loads((self.workspace / "apk-evidence/report.json").read_text(encoding="utf-8")),
        )

    def test_primary_failure_still_uninstalls_target_and_test_packages(self) -> None:
        runner = FakeRunner(self.tool, "start")
        with self.assertRaises(self.tool.AcceptanceError):
            self.tool.run_source_template_acceptance(
                source_template=self.source_template,
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
        with self.assertRaisesRegex(self.tool.AcceptanceError, "Gradle acceptance failed"):
            self.run_acceptance("gradle")


if __name__ == "__main__":
    unittest.main()
