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
    unclassified: bool

    @property
    def blocks_slice(self) -> bool:
        return self.state in (State.OVERDUE, State.CONFLICTING)

    @property
    def blocks_release(self) -> bool:
        """Release and epic closure wait for every finding to be classified, not only for overdue ones."""
        return self.blocks_slice or (self.unclassified and self.state is not State.RESOLVED)

    def to_dict(self) -> dict[str, Any]:
        return {
            "finding_id": self.finding_id,
            "state": self.state.value,
            "reason": self.reason,
            "blocks_capability_slice": self.blocks_slice,
            "blocks_release": self.blocks_release,
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
        authoritative = (
            merged_record
            if merged_record is not None
            else (provisional_record.payload if provisional_record is not None else None)
        )
        unclassified = authoritative is None or authoritative["classification"] == "unclassified"
        return Reconciliation(finding_id, state, reason, capability_slice, due_at, unclassified)

    if provisional_record is None and merged_record is None:
        return result(State.CONFLICTING, "no authority: neither a provisional record nor a merged ledger entry exists")

    if merged_record is not None:
        if provisional_record is not None:
            proposed = provisional_record.payload
            if ledger.identity_digest(merged_record) != ledger.identity_digest(proposed):
                return result(State.CONFLICTING, "merged ledger entry identity disagrees with the provisional payload")
            if ledger.payload_digest_without_classification(
                merged_record
            ) != ledger.payload_digest_without_classification(proposed):
                return result(State.CONFLICTING, "merged ledger entry payload disagrees with the provisional payload")
            if (
                proposed["classification"] != "unclassified"
                and merged_record["classification"] != proposed["classification"]
            ):
                return result(
                    State.CONFLICTING,
                    "merged ledger entry reclassifies a finding the provisional record already classified",
                )
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
    if pull_request is None:
        return result(State.CONFLICTING, "provisional record has no ledger pull request")
    if pull_request.state == "merged":
        return result(State.CONFLICTING, "pull request is merged but no ledger entry exists for the finding")
    if comparison_status == "resolved" and pull_request.state == "closed":
        return result(State.RESOLVED, "case passes on the branch and the ledger proposal was withdrawn unmerged")
    if pull_request.state != "open":
        return result(State.CONFLICTING, f"ledger pull request is {pull_request.state} without a merged entry")
    if due_at is not None and deadline.is_overdue(due_at, now):
        return result(State.OVERDUE, "classification deadline passed before the ledger entry merged")
    return result(State.PENDING_MERGE, "ledger pull request is open and awaiting review")


def blocked_slices(results: Iterable[Reconciliation]) -> set[tuple[str, ...]]:
    return {result.capability_slice for result in results if result.blocks_slice}


def blocks_release(results: Iterable[Reconciliation]) -> bool:
    return any(result.blocks_release for result in results)


def blocks_path(results: Iterable[Reconciliation], path: str) -> bool:
    return any(path in capability_slice for capability_slice in blocked_slices(results))
