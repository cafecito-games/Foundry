from __future__ import annotations

import importlib.util
import sys
import unittest
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
            "List of devices attached\n"
            "emulator-5554 device product:sdk model:Virtual_Device\n"
            "offline-5556 offline\n"
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


if __name__ == "__main__":
    unittest.main()
