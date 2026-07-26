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


class AndroidDeviceAcceptanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tool = load_tool()

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.workspace = Path(self.temporary_directory.name)
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
                "#runtimeBootsWithCanonicalPluginProtocol"
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


if __name__ == "__main__":
    unittest.main()
