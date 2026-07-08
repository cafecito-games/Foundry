#!/usr/bin/env python3
"""Unit tests for scripts/foundry_mcp/session.py."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))

from scripts.foundry_mcp import (
    FoundryAutomationStartupError,
    FoundryEditorAutomationSession,
    parse_automation_line,
)


class _FakeStdout:
    def __init__(self, lines: list[str]) -> None:
        self.lines = lines

    def readline(self) -> str:
        if self.lines:
            return self.lines.pop(0)
        return ""


class _FakeProcess:
    def __init__(self, lines: list[str], *, wait_timeout: bool = False) -> None:
        self.stdout = _FakeStdout(lines)
        self.wait_timeout = wait_timeout
        self.terminated = False
        self.killed = False
        self.returncode: int | None = None

    def poll(self) -> int | None:
        return self.returncode

    def terminate(self) -> None:
        self.terminated = True

    def kill(self) -> None:
        self.killed = True

    def wait(self, timeout: float | None = None) -> int:
        if self.wait_timeout and not self.killed:
            raise subprocess.TimeoutExpired("foundry", timeout)
        self.returncode = 0
        return 0


class FoundryMCPSessionTestCase(unittest.TestCase):
    def test_parse_automation_line_extracts_endpoint_and_token(self) -> None:
        line = 'noise FOUNDRY_AUTOMATION {"endpoint":"http://127.0.0.1:1/mcp","token":"abc"}'

        info = parse_automation_line(line)

        self.assertEqual(info["endpoint"], "http://127.0.0.1:1/mcp")
        self.assertEqual(info["token"], "abc")

    def test_parse_automation_line_rejects_error_marker(self) -> None:
        with self.assertRaises(FoundryAutomationStartupError):
            parse_automation_line("FOUNDRY_AUTOMATION_ERROR failed")

    def test_wait_for_automation_line_reports_captured_output_on_timeout(self) -> None:
        process = _FakeProcess(["first line\n"])

        with self.assertRaises(FoundryAutomationStartupError) as raised:
            FoundryEditorAutomationSession.wait_for_automation(process, timeout_s=0.01)

        self.assertIn("first line", str(raised.exception))

    def test_connect_creates_client_without_process(self) -> None:
        session = FoundryEditorAutomationSession.connect("http://127.0.0.1:9/mcp", "secret")

        self.assertEqual(session.client.endpoint, "http://127.0.0.1:9/mcp")
        self.assertIsNone(session.process)

    def test_launch_builds_command_first_invocation(self) -> None:
        calls: list[dict[str, Any]] = []
        line = 'FOUNDRY_AUTOMATION {"endpoint":"http://127.0.0.1:2/mcp","token":"tok"}\n'

        def fake_popen(command: list[str], **kwargs: Any) -> _FakeProcess:
            calls.append({"command": command, "kwargs": kwargs})
            return _FakeProcess([line])

        with tempfile.TemporaryDirectory() as tmp:
            session = FoundryEditorAutomationSession.launch(
                binary=Path("/tmp/foundry"),
                project=Path(tmp),
                display=":9",
                token="tok",
                port=0,
                popen=fake_popen,
            )

            self.assertEqual(calls[0]["command"][:5], ["/tmp/foundry", "editor", "open", "--project", tmp])

        self.assertIn("--automation", calls[0]["command"])
        self.assertIn("--automation-transport=mcp", calls[0]["command"])
        self.assertIn("--automation-port", calls[0]["command"])
        self.assertIn("--automation-token", calls[0]["command"])
        self.assertEqual(calls[0]["kwargs"]["env"]["DISPLAY"], ":9")
        self.assertEqual(session.client.endpoint, "http://127.0.0.1:2/mcp")
        self.assertEqual(session.client.token, "tok")

    def test_context_manager_kills_process_when_terminate_times_out(self) -> None:
        process = _FakeProcess([], wait_timeout=True)
        session = FoundryEditorAutomationSession.connect("http://127.0.0.1:9/mcp", "secret")
        session.process = process

        with session:
            pass

        self.assertTrue(process.terminated)
        self.assertTrue(process.killed)


if __name__ == "__main__":
    unittest.main()
