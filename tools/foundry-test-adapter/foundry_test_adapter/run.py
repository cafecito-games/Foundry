"""Cross-artifact conformance checks between discovery, selection, and a report."""

from __future__ import annotations

from typing import Optional, Sequence

from .discovery import DiscoveryItem, DiscoveryResult
from .tap import TapReport
from .violations import Violation, ViolationCode


def is_leaf(discovery: DiscoveryResult, item: DiscoveryItem) -> bool:
    """True when no other discovered item declares `item` as its parent."""

    return not discovery.children_of(item.id)


def expected_leaf_ids(
    discovery: DiscoveryResult, selection: Optional[Sequence[str]] = None
) -> tuple[list[str], list[Violation]]:
    """Resolves a selection into the deduplicated leaf identifiers a run must report.

    An empty or omitted selection resolves to every runnable leaf. Selecting a
    suite selects its descendants, and repeated or overlapping selections
    collapse so each leaf appears once, in discovery order.
    """

    violations: list[Violation] = []
    runnable_leaves = [item for item in discovery.items if is_leaf(discovery, item) and item.runnable]
    if not selection:
        return ([item.id for item in runnable_leaves], violations)

    selected: set[str] = set()
    for identifier in selection:
        item = discovery.item_by_id(identifier)
        if item is None:
            violations.append(
                Violation(
                    ViolationCode.UNKNOWN_SELECTION_ID,
                    "Selected identifier '{}' was never discovered".format(identifier),
                )
            )
            continue
        if not item.runnable:
            violations.append(
                Violation(
                    ViolationCode.SELECTION_NOT_RUNNABLE,
                    "Selected identifier '{}' is not runnable".format(identifier),
                )
            )
            continue
        for descendant in _closure(discovery, item):
            selected.add(descendant.id)

    ordered = [item.id for item in runnable_leaves if item.id in selected]
    return (ordered, violations)


def validate_run(
    discovery: DiscoveryResult,
    report: TapReport,
    selection: Optional[Sequence[str]] = None,
) -> list[Violation]:
    """Checks that a report answers exactly the selection implied by a discovery stream."""

    expected, violations = expected_leaf_ids(discovery, selection)
    actual = report.point_ids()

    # The plan is written before any point, so bailing out later never excuses a plan
    # that disagrees with the selection.
    if report.plan is not None and report.plan != len(expected):
        violations.append(
            Violation(
                ViolationCode.INVALID_PLAN,
                "Plan declares {} tests but the deduplicated selection has {}".format(report.plan, len(expected)),
            )
        )

    expected_set = set(expected)
    for identifier in actual:
        if identifier not in expected_set:
            violations.append(
                Violation(
                    ViolationCode.UNEXPECTED_RESULT_ID,
                    "Report contains unselected identifier '{}'".format(identifier),
                )
            )

    actual_set = set(actual)
    if report.complete:
        for identifier in expected:
            if identifier not in actual_set:
                violations.append(
                    Violation(
                        ViolationCode.MISSING_RESULT_ID,
                        "Report omits selected identifier '{}'".format(identifier),
                    )
                )

    violations.extend(_validate_skip_state(discovery, report))

    reported_in_order = [identifier for identifier in actual if identifier in expected_set]
    expected_in_report = [identifier for identifier in expected if identifier in actual_set]
    if reported_in_order != expected_in_report:
        violations.append(
            Violation(
                ViolationCode.RESULT_ORDER_MISMATCH,
                "Report does not follow deterministic discovery order",
            )
        )

    return violations


def _closure(discovery: DiscoveryResult, item: DiscoveryItem) -> list[DiscoveryItem]:
    # Identifiers are tracked while walking because a non-conforming stream may repeat
    # an identifier, which makes an item its own descendant. Traversal must still
    # terminate so the caller gets its collected violations back.
    collected = [item]
    visited = {item.id}
    index = 0
    while index < len(collected):
        current = collected[index]
        index += 1
        for child in discovery.children_of(current.id):
            if child.id in visited:
                continue
            visited.add(child.id)
            collected.append(child)
    return collected


def _validate_skip_state(discovery: DiscoveryResult, report: TapReport) -> list[Violation]:
    """Checks that each point's skip directive matches the discovered skip state.

    Skip state is discovery-owned, so a report may neither silently run a test the
    framework declared skipped nor skip one it did not.
    """

    violations: list[Violation] = []
    for point in report.points:
        if point.id is None:
            continue
        item = discovery.item_by_id(point.id)
        if item is None:
            continue
        if item.skipped and not point.skipped:
            violations.append(
                Violation(
                    ViolationCode.SKIP_STATE_MISMATCH,
                    "Test '{}' was discovered as skipped but its point carries no '# SKIP'".format(point.id),
                )
            )
            continue
        if point.skipped and not item.skipped:
            violations.append(
                Violation(
                    ViolationCode.SKIP_STATE_MISMATCH,
                    "Test '{}' was not discovered as skipped but its point is skipped".format(point.id),
                )
            )
            continue
        if item.skipped and point.skip_reason != item.skip_reason:
            violations.append(
                Violation(
                    ViolationCode.SKIP_STATE_MISMATCH,
                    "Test '{}' reports skip reason '{}' instead of the discovered '{}'".format(
                        point.id, point.skip_reason, item.skip_reason
                    ),
                )
            )
    return violations
