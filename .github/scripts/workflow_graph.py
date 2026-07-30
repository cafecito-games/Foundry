"""Parsed view over GitHub Actions workflow and composite-action YAML.

Workflow contracts assert on the *graph* a workflow declares — which jobs exist,
what they depend on, which steps run in which order, what a matrix expands to —
never on how the YAML happens to be spelled. Reformatting a workflow must not
break a contract, and a broken workflow must not slip past one.

Every accessor raises `MissingWorkflowElement` when the thing it was asked for is
absent. Returning an empty mapping or list would let every downstream contract
pass while asserting nothing, which is the one failure mode these contracts exist
to prevent.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any, Iterator

import yaml

WHITESPACE = re.compile(r"\s+")
# Single- and double-quoted spans of a GitHub expression, whose interior whitespace
# is part of the compared value and must survive normalization.
QUOTED_SPAN = re.compile(r"'(?:''|[^'])*'|\"(?:\\.|[^\"\\])*\"")

# GitHub parses the unquoted key `on:` as the YAML boolean `True`. Workflows say
# `on:`, so the parsed document keys the trigger block under `True`.
YAML_TRUE_KEY = True


class MissingWorkflowElement(LookupError):
    """Raised when a workflow does not declare the requested job, step, or key."""


def normalize_expression(expression: str) -> str:
    """Collapse whitespace in a GitHub expression so line breaks do not matter.

    GitHub expressions may be wrapped across lines with folded or literal block
    scalars without changing meaning, so contracts compare them whitespace-normalized.
    Whitespace inside a quoted string literal is part of the compared value and is
    left alone, so `== 'a b'` never normalizes to the same text as `== 'a  b'`.
    Expressions are never evaluated.
    """

    text = str(expression)
    normalized: list[str] = []
    cursor = 0
    for quoted in QUOTED_SPAN.finditer(text):
        normalized.append(WHITESPACE.sub(" ", text[cursor : quoted.start()]))
        normalized.append(quoted.group())
        cursor = quoted.end()
    normalized.append(WHITESPACE.sub(" ", text[cursor:]))
    return "".join(normalized).strip()


def load(path: Path | str) -> Workflow:
    """Parse the workflow or composite action at `path`."""

    resolved = Path(path)
    document = yaml.safe_load(resolved.read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise MissingWorkflowElement(f"{resolved} does not parse to a YAML mapping.")
    return Workflow(resolved, document)


def iter_scalars(value: Any) -> Iterator[str]:
    """Yield every string scalar reachable from a parsed YAML value.

    This walks parsed values, not file text, so it is immune to indentation, key
    order, and line wrapping while still letting a contract assert that a forbidden
    identifier appears nowhere in a workflow.
    """

    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, child in value.items():
            if isinstance(key, str):
                yield key
            yield from iter_scalars(child)
    elif isinstance(value, list):
        for child in value:
            yield from iter_scalars(child)


class Workflow:
    """A parsed workflow or composite action, addressed by graph structure."""

    def __init__(self, path: Path, document: dict[Any, Any]) -> None:
        self.path = path
        self.document = document

    def __repr__(self) -> str:
        return f"Workflow({self.path!s})"

    def _require(self, container: Any, key: Any, description: str) -> Any:
        if not isinstance(container, dict) or key not in container:
            raise MissingWorkflowElement(f"{self.path}: {description} is missing.")
        value = container[key]
        if value is None:
            raise MissingWorkflowElement(f"{self.path}: {description} is empty.")
        return value

    def _require_mapping(self, container: Any, key: Any, description: str) -> dict[str, Any]:
        value = self._require(container, key, description)
        if not isinstance(value, dict):
            raise MissingWorkflowElement(f"{self.path}: {description} is not a mapping.")
        return value

    # -- document level ---------------------------------------------------

    def triggers(self) -> dict[str, Any]:
        """Return the `on:` block."""

        if YAML_TRUE_KEY in self.document:
            triggers = self.document[YAML_TRUE_KEY]
        else:
            triggers = self._require(self.document, "on", "the `on:` trigger block")
        if not isinstance(triggers, dict):
            raise MissingWorkflowElement(f"{self.path}: the `on:` trigger block is not a mapping.")
        return triggers

    def trigger(self, name: str) -> Any:
        return self._require(self.triggers(), name, f"trigger {name!r}")

    def workflow_call_inputs(self) -> dict[str, Any]:
        """Return the reusable-workflow `on.workflow_call.inputs` mapping."""

        workflow_call = self.trigger("workflow_call")
        return self._require_mapping(workflow_call, "inputs", "`workflow_call.inputs`")

    def action_inputs(self) -> dict[str, Any]:
        """Return a composite action's `inputs` mapping."""

        return self._require_mapping(self.document, "inputs", "the action `inputs` block")

    def jobs(self) -> dict[str, Any]:
        jobs = self._require_mapping(self.document, "jobs", "the `jobs:` block")
        if not jobs:
            raise MissingWorkflowElement(f"{self.path}: the `jobs:` block declares no jobs.")
        return jobs

    def job_names(self) -> list[str]:
        """Return every job name in declaration order."""

        return list(self.jobs())

    def job_index(self, name: str) -> int:
        """Return a job's position in declaration order."""

        names = self.job_names()
        if name not in names:
            raise MissingWorkflowElement(f"{self.path}: job {name!r} is not declared.")
        return names.index(name)

    def job(self, name: str) -> dict[str, Any]:
        jobs = self.jobs()
        if name not in jobs:
            raise MissingWorkflowElement(f"{self.path}: job {name!r} is not declared.")
        job = jobs[name]
        if not isinstance(job, dict):
            raise MissingWorkflowElement(f"{self.path}: job {name!r} does not parse to a mapping.")
        return job

    # -- job level --------------------------------------------------------

    def job_key(self, name: str, key: str) -> Any:
        return self._require(self.job(name), key, f"job {name!r} key {key!r}")

    def job_mapping(self, name: str, key: str) -> dict[str, Any]:
        return self._require_mapping(self.job(name), key, f"job {name!r} key {key!r}")

    def needs(self, name: str) -> tuple[str, ...]:
        """Return a job's dependencies, normalizing the scalar and list forms."""

        declared = self.job_key(name, "needs")
        if isinstance(declared, str):
            return (declared,)
        if isinstance(declared, list) and declared and all(isinstance(entry, str) for entry in declared):
            return tuple(declared)
        raise MissingWorkflowElement(f"{self.path}: job {name!r} declares an unreadable `needs:` value.")

    def has_needs(self, name: str) -> bool:
        return "needs" in self.job(name)

    def job_if(self, name: str) -> str:
        """Return a job's `if:` expression with whitespace collapsed."""

        return normalize_expression(self.job_key(name, "if"))

    def job_name(self, name: str) -> str:
        return str(self.job_key(name, "name"))

    def job_env(self, name: str) -> dict[str, Any]:
        return self.job_mapping(name, "env")

    def outputs(self, name: str) -> dict[str, Any]:
        return self.job_mapping(name, "outputs")

    def concurrency(self, name: str) -> dict[str, Any]:
        return self.job_mapping(name, "concurrency")

    def strategy(self, name: str) -> dict[str, Any]:
        return self.job_mapping(name, "strategy")

    def uses(self, name: str) -> str:
        """Return the reusable workflow a job calls."""

        return str(self.job_key(name, "uses"))

    def with_inputs(self, name: str) -> dict[str, Any]:
        """Return the `with:` inputs a job passes to the workflow it calls."""

        return self.job_mapping(name, "with")

    # -- matrix -----------------------------------------------------------

    def matrix(self, name: str) -> dict[str, list[Any]]:
        """Return every matrix dimension mapped to the values it declares.

        Both the cross-product form and the `include:` form contribute dimensions;
        `include` values are collected in declaration order without duplicates.
        """

        declared = self._require_mapping(self.strategy(name), "matrix", f"job {name!r} matrix")

        dimensions: dict[str, list[Any]] = {}
        for dimension, values in declared.items():
            if dimension in ("include", "exclude"):
                continue
            dimensions[dimension] = list(values) if isinstance(values, list) else [values]

        for cell in declared.get("include") or []:
            for dimension, value in cell.items():
                bucket = dimensions.setdefault(dimension, [])
                if value not in bucket:
                    bucket.append(value)

        if not dimensions:
            raise MissingWorkflowElement(f"{self.path}: job {name!r} declares an empty matrix.")
        return dimensions

    def matrix_cells(self, name: str) -> list[dict[str, Any]]:
        """Return the `include:` cells of a job's matrix in declaration order."""

        declared = self._require_mapping(self.strategy(name), "matrix", f"job {name!r} matrix")
        cells = self._require(declared, "include", f"job {name!r} matrix `include:`")
        if not isinstance(cells, list) or not all(isinstance(cell, dict) for cell in cells):
            raise MissingWorkflowElement(f"{self.path}: job {name!r} declares unreadable matrix cells.")
        return cells

    # -- step level -------------------------------------------------------

    def steps(self, name: str) -> list[dict[str, Any]]:
        declared = self.job_key(name, "steps")
        if not isinstance(declared, list) or not declared:
            raise MissingWorkflowElement(f"{self.path}: job {name!r} declares no steps.")
        return declared

    def action_steps(self) -> list[dict[str, Any]]:
        """Return a composite action's steps."""

        runs = self._require_mapping(self.document, "runs", "the action `runs` block")
        declared = self._require(runs, "steps", "the action `runs.steps` block")
        if not isinstance(declared, list) or not declared:
            raise MissingWorkflowElement(f"{self.path}: the action declares no steps.")
        return declared

    def step_names(self, name: str) -> list[str | None]:
        """Return step names in declaration order, with `None` for unnamed steps."""

        return [step.get("name") for step in self.steps(name)]

    def step_index(self, name: str, step_name: str) -> int:
        names = self.step_names(name)
        if step_name not in names:
            raise MissingWorkflowElement(f"{self.path}: job {name!r} has no step named {step_name!r}.")
        return names.index(step_name)

    def step(self, name: str, step_name: str) -> dict[str, Any]:
        return self.steps(name)[self.step_index(name, step_name)]

    def step_key(self, name: str, step_name: str, key: str) -> Any:
        step = self.step(name, step_name)
        return self._require(step, key, f"job {name!r} step {step_name!r} key {key!r}")

    def step_if(self, name: str, step_name: str) -> str:
        return normalize_expression(self.step_key(name, step_name, "if"))

    def step_with(self, name: str, step_name: str) -> dict[str, Any]:
        step = self.step(name, step_name)
        return self._require_mapping(step, "with", f"job {name!r} step {step_name!r} key 'with'")

    def step_run(self, name: str, step_name: str) -> str:
        return str(self.step_key(name, step_name, "run"))

    def all_steps(self) -> Iterator[tuple[str, dict[str, Any]]]:
        """Yield `(job_name, step)` for every step of every job that declares steps."""

        for job_name, job in self.jobs().items():
            if not isinstance(job, dict) or "steps" not in job:
                continue
            for step in job["steps"]:
                yield job_name, step

    # -- whole-document search -------------------------------------------

    def scalars(self) -> Iterator[str]:
        """Yield every string scalar in the parsed document."""

        return iter_scalars(self.document)
