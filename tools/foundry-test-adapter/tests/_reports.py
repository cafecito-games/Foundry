"""Builders for strict TAP13 report text used by the report and CLI tests."""

from __future__ import annotations

PREAMBLE = "TAP version 13\n# foundry-test-adapter: 1\n"


def block(test_id="test-a", duration=1, status_detail="", message=None, location=None, extra=()):
    lines = ["  ---"]
    if message is not None:
        lines.append('  message: "{}"'.format(message))
    if location is not None:
        lines.append("  at:")
        lines.append('    fileName: "{}"'.format(location[0]))
        lines.append("    lineNumber: {}".format(location[1]))
        lines.append("    columnNumber: {}".format(location[2]))
    lines.append("  _foundry:")
    lines.append('    id: "{}"'.format(test_id))
    lines.append("    duration_ms: {}".format(duration))
    lines.append('    status_detail: "{}"'.format(status_detail))
    lines.extend(extra)
    lines.append("  ...")
    return "\n".join(lines) + "\n"


def point(number, ok=True, label="MathTests.adds numbers", test_id="test-a", skip_reason=None, **kwargs):
    line = "{} {} - {}".format("ok" if ok else "not ok", number, label)
    if skip_reason is not None:
        line += " # SKIP {}".format(skip_reason)
    return line + "\n" + block(test_id, **kwargs)


def report(plan, *points):
    return PREAMBLE + "1..{}\n".format(plan) + "".join(points)
