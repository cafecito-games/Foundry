"""Reconciliation of provisional records, ledger pull requests, and merged ledger entries."""

from __future__ import annotations

import enum
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Iterable, Optional

from . import deadline, ledger
from .provisional import ProvisionalRecord


class State(str, enum.Enum):
    PENDING_MERGE = "pending_merge"
    MERGED = "merged"
    RESOLVED = "resolved"
    CONFLICTING = "conflicting"
    OVERDUE = "overdue"


@dataclass(frozen=True)
class PullRequest:
    url: str
    state: str  # open | merged | closed


@dataclass(frozen=True)
class Reconciliation:
    finding_id: str
    state: State
    reason: str
    capability_slice: tuple[str, ...]
    due_at: Optional[datetime]

    @property
    def blocks_slice(self) -> bool:
        return self.state in (State.OVERDUE, State.CONFLICTING)

    def to_dict(self) -> dict[str, Any]:
        return {
            "finding_id": self.finding_id,
            "state": self.state.value,
            "reason": self.reason,
            "blocks_capability_slice": self.blocks_slice,
            "blocked_capability_slice": list(self.capability_slice) if self.blocks_slice else [],
            "due_at": None if self.due_at is None else deadline.format_timestamp(self.due_at),
        }


def reconcile_finding(
    finding_id: str,
    provisional_record: Optional[ProvisionalRecord],
    merged_record: Optional[dict[str, Any]],
    pull_request: Optional[PullRequest],
    now: datetime,
    comparison_status: Optional[str] = None,
) -> Reconciliation:
    capability_slice = provisional_record.capability_slice if provisional_record else ()
    due_at = provisional_record.due_at if provisional_record else None

    def result(state: State, reason: str) -> Reconciliation:
        return Reconciliation(finding_id, state, reason, capability_slice, due_at)

    if provisional_record is None and merged_record is None:
        return result(State.CONFLICTING, "no authority: neither a provisional record nor a merged ledger entry exists")

    if merged_record is not None:
        if provisional_record is not None and ledger.record_digest(merged_record) != provisional_record.payload_digest:
            return result(State.CONFLICTING, "merged ledger entry digest disagrees with the provisional payload")
        if pull_request is not None and pull_request.state != "merged":
            return result(State.CONFLICTING, f"ledger entry is merged but its pull request is {pull_request.state}")
        if (
            merged_record["classification"] == "unclassified"
            and due_at is not None
            and deadline.is_overdue(due_at, now)
        ):
            return result(State.OVERDUE, "merged ledger entry is still unclassified past its deadline")
        return result(State.MERGED, "merged ledger entry matches the provisional record")

    assert provisional_record is not None
    if comparison_status == "resolved":
        return result(State.RESOLVED, "case passes on the branch and no ledger entry remains")
    if pull_request is None:
        return result(State.CONFLICTING, "provisional record has no ledger pull request")
    if pull_request.state == "merged":
        return result(State.CONFLICTING, "pull request is merged but no ledger entry exists for the finding")
    if pull_request.state != "open":
        return result(State.CONFLICTING, f"ledger pull request is {pull_request.state} without a merged entry")
    if due_at is not None and deadline.is_overdue(due_at, now):
        return result(State.OVERDUE, "classification deadline passed before the ledger entry merged")
    return result(State.PENDING_MERGE, "ledger pull request is open and awaiting review")


def blocked_slices(results: Iterable[Reconciliation]) -> set[tuple[str, ...]]:
    return {result.capability_slice for result in results if result.blocks_slice}


def blocks_release(results: Iterable[Reconciliation]) -> bool:
    return any(result.blocks_slice for result in results)


def blocks_path(results: Iterable[Reconciliation], path: str) -> bool:
    return any(path in capability_slice for capability_slice in blocked_slices(results))
