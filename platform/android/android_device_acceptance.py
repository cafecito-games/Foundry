#!/usr/bin/env python3
"""Run Foundry Android source-template and exported-APK acceptance checks."""

from __future__ import annotations

import re


class AcceptanceError(Exception):
    """Android device acceptance failed."""


APPLICATION_ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+$")
RUNTIME_FAILURE_PATTERNS = (
    "UnsatisfiedLinkError",
    "NoClassDefFoundError",
    "ClassNotFoundException",
    "FATAL EXCEPTION",
    'couldn\'t find "libfoundry_android.so"',
)


def validate_application_id(value: str) -> str:
    """Return one valid lowercase reverse-DNS Android application ID."""
    if APPLICATION_ID_PATTERN.fullmatch(value) is None:
        raise AcceptanceError(f"invalid Android application ID: {value!r}")
    return value


def select_device(output: str, requested_serial: str | None) -> str:
    """Resolve exactly one ready adb device, or one explicitly requested device."""
    ready = [
        fields[0]
        for line in output.splitlines()[1:]
        if len(fields := line.split()) >= 2 and fields[1] == "device"
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
