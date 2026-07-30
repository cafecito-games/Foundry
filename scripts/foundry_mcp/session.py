#!/usr/bin/env python3
"""Process/session helpers for Foundry editor automation MCP."""

from __future__ import annotations

import json
import os
import subprocess
import time
from pathlib import Path
from typing import Any, Callable

from scripts.foundry_mcp.client import FoundryMCPClient

AUTOMATION_MARKER = "FOUNDRY_AUTOMATION "
AUTOMATION_ERROR_MARKER = "FOUNDRY_AUTOMATION_ERROR"


class FoundryAutomationStartupError(RuntimeError):
    pass


def parse_automation_line(line: str) -> dict[str, Any]:
    if AUTOMATION_ERROR_MARKER in line:
        raise FoundryAutomationStartupError(line.strip())
    if AUTOMATION_MARKER not in line:
        raise FoundryAutomationStartupError("Line does not contain FOUNDRY_AUTOMATION marker.")
    raw = line[line.index(AUTOMATION_MARKER) + len(AUTOMATION_MARKER) :].strip()
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise FoundryAutomationStartupError(f"Malformed FOUNDRY_AUTOMATION payload: {raw}") from exc
    if not data.get("endpoint") or not data.get("token"):
        raise FoundryAutomationStartupError(f"FOUNDRY_AUTOMATION payload missing endpoint/token: {raw}")
    return data


class FoundryEditorAutomationSession:
    def __init__(self, client: FoundryMCPClient, process: subprocess.Popen[str] | None = None) -> None:
        self.client = client
        self.process = process

    @classmethod
    def connect(cls, endpoint: str, token: str, *, timeout: float = 60.0) -> FoundryEditorAutomationSession:
        return cls(FoundryMCPClient(endpoint, token, timeout=timeout))

    @classmethod
    def launch(
        cls,
        *,
        binary: Path,
        project: Path,
        display: str | None = ":1",
        port: int | None = 0,
        token: str | None = None,
        extra_args: list[str] | None = None,
        env: dict[str, str] | None = None,
        startup_timeout_s: float = 180.0,
        popen: Callable[..., subprocess.Popen[str]] = subprocess.Popen,
    ) -> FoundryEditorAutomationSession:
        command = [
            str(binary),
            "editor",
            "open",
            "--project",
            str(project),
            "--automation",
            "--automation-transport=mcp",
        ]
        if port is not None:
            command.extend(["--automation-port", str(port)])
        if token is not None:
            command.extend(["--automation-token", token])
        if extra_args:
            command.extend(extra_args)

        child_env = os.environ.copy()
        if env:
            child_env.update(env)
        if display is not None:
            child_env["DISPLAY"] = display

        process = popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=child_env)
        automation = cls.wait_for_automation(process, timeout_s=startup_timeout_s)
        return cls(FoundryMCPClient(automation["endpoint"], automation["token"]), process=process)

    @staticmethod
    def wait_for_automation(process: Any, *, timeout_s: float = 180.0) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_s
        output = ""
        while time.monotonic() < deadline:
            line = process.stdout.readline() if process.stdout else ""
            if line:
                output += line
                if AUTOMATION_MARKER in line or AUTOMATION_ERROR_MARKER in line:
                    try:
                        return parse_automation_line(line)
                    except FoundryAutomationStartupError as exc:
                        raise FoundryAutomationStartupError(f"{exc}\nCaptured output:\n{output}") from exc
            elif process.poll() is not None:
                break
            else:
                time.sleep(0.05)
        raise FoundryAutomationStartupError(
            f"Timed out waiting for FOUNDRY_AUTOMATION line.\nCaptured output:\n{output}"
        )

    def close(self, *, terminate_timeout_s: float = 10.0) -> None:
        if self.process is None:
            return
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=terminate_timeout_s)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=terminate_timeout_s)

    def __enter__(self) -> FoundryEditorAutomationSession:
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback) -> None:
        self.close()
