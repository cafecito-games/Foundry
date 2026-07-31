"""UTF-16 helpers for the position encoding used by discovery ranges."""

from __future__ import annotations


def utf16_length(text: str) -> int:
    """Number of UTF-16 code units in `text`.

    Protocol ranges are zero-based, end-exclusive, and count UTF-16 code
    units, so a character outside the Basic Multilingual Plane advances a
    `character` offset by two rather than one.
    """

    return len(text.encode("utf-16-le")) // 2


def utf16_offset_to_index(text: str, offset: int) -> int:
    """Converts a UTF-16 `character` offset into a Python string index.

    Raises `ValueError` when the offset falls inside a surrogate pair, which
    is never a valid protocol position.
    """

    if offset < 0:
        raise ValueError("A UTF-16 offset must not be negative")
    consumed = 0
    for index, character in enumerate(text):
        if consumed == offset:
            return index
        consumed += 2 if ord(character) > 0xFFFF else 1
        if consumed > offset:
            raise ValueError("UTF-16 offset {} splits a surrogate pair".format(offset))
    if consumed != offset:
        raise ValueError("UTF-16 offset {} is past the end of the text".format(offset))
    return len(text)
