"""Strict JSON decoding for protocol artifacts."""

from __future__ import annotations

import json
from typing import Any, NoReturn


def _reject_constant(name: str) -> NoReturn:
    raise ValueError("{} is not valid JSON".format(name))


def loads(text: str) -> Any:
    """Decodes JSON, rejecting the non-standard `NaN`/`Infinity` literals.

    Python's default decoder accepts those literals, which would let a
    syntactically invalid artifact validate as conforming.
    """

    return json.loads(text, parse_constant=_reject_constant)
