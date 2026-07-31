"""Reconstruction of the ordered runnable-leaf plan an adapter must execute."""

from __future__ import annotations

from typing import Optional, Sequence

from .json_artifacts import DiscoveryItem, DiscoveryModel


class SelectionError(Exception):
    """A selection that a conforming adapter must reject with exit code 2."""


def _runnable_leaves(model: DiscoveryModel) -> tuple[DiscoveryItem, ...]:
    return tuple(item for item in model.items if item.kind == "test" and item.runnable)


def _descendant_leaves(model: DiscoveryModel, suite_id: str) -> tuple[DiscoveryItem, ...]:
    by_id = model.by_id()
    selected: list[DiscoveryItem] = []
    for item in model.items:
        if item.kind != "test" or not item.runnable:
            continue
        ancestor: Optional[str] = item.parent_id
        # Opaque IDs are compared exactly; the walk terminates because every parent
        # link points at an already emitted suite and a rejected model never reaches
        # plan construction.
        visited: set[str] = set()
        while ancestor is not None and ancestor in by_id and ancestor not in visited:
            if ancestor == suite_id:
                selected.append(item)
                break
            visited.add(ancestor)
            ancestor = by_id[ancestor].parent_id
    return tuple(selected)


def build_leaf_plan(model: DiscoveryModel, selections: Sequence[str] = ()) -> tuple[DiscoveryItem, ...]:
    """Returns the ordered runnable-leaf plan for `selections`.

    With no selections the plan is every runnable test leaf in discovery order.
    Repeated and overlapping selections deduplicate by test ID while the final
    plan keeps deterministic discovery order.
    """

    if not selections:
        return _runnable_leaves(model)

    by_id = model.by_id()
    chosen: set[str] = set()
    for selection in selections:
        item = by_id.get(selection)
        if item is None:
            raise SelectionError("Unknown selection ID '{}'".format(selection))
        if item.kind == "error":
            raise SelectionError(
                "Selection ID '{}' identifies a discovery error, not a runnable item".format(selection)
            )
        if not item.runnable:
            raise SelectionError("Selection ID '{}' is not runnable".format(selection))
        if item.kind == "test":
            chosen.add(item.id)
            continue
        expansion = _descendant_leaves(model, item.id)
        if not expansion:
            raise SelectionError("Selected suite '{}' has no runnable descendant tests".format(selection))
        for leaf in expansion:
            chosen.add(leaf.id)

    return tuple(item for item in _runnable_leaves(model) if item.id in chosen)
