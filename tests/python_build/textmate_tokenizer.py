"""A minimal TextMate grammar interpreter backed by Oniguruma.

TextMate grammars are Oniguruma regular expressions driven by a scope stack, and Python's
`re` accepts a different language: it rejects `\\p{L}`, and it evaluates lookbehind against
a sliced string rather than the real preceding text. A grammar that "passes" under `re` is
therefore no evidence that VS Code will highlight anything correctly, so this interpreter
runs the actual Oniguruma engine through `onigurumacffi`.

It implements the subset of the TextMate rule model the FoundryScript grammar uses:
`match`, `begin`/`end` with a scope stack that survives line breaks, `captures`,
`beginCaptures`, `endCaptures`, `name`, `contentName`, nested `patterns`, and `include`
of a repository entry or `$self`. Rules compete by leftmost match, with declaration order
breaking ties, and an `end` match wins a tie against the rules inside its own block --
the same resolution order vscode-textmate uses.
"""

from __future__ import annotations

from typing import Any, Iterator, NamedTuple

import onigurumacffi

MAX_STEPS_PER_LINE = 10_000


class GrammarError(RuntimeError):
    """Raised when a grammar document cannot be interpreted."""


class Token(NamedTuple):
    """One run of characters that carries a single scope stack."""

    line: int
    start: int
    end: int
    text: str
    scopes: tuple[str, ...]


class _Frame(NamedTuple):
    scopes: tuple[str, ...]
    outer_scopes: tuple[str, ...]
    end_pattern: str | None
    end_captures: dict[str, Any] | None
    patterns: list[dict[str, Any]]


class TextMateGrammar:
    """A tokenizer for one parsed `.tmLanguage.json` document."""

    def __init__(self, document: dict[str, Any]) -> None:
        self.document = document
        self.scope_name = str(document["scopeName"])
        self.repository: dict[str, Any] = document.get("repository") or {}
        self._compiled: dict[str, Any] = {}
        self._root_patterns = self._expand(document.get("patterns") or [], frozenset())

    # -- rule resolution --------------------------------------------------

    def _entry(self, include: str) -> dict[str, Any]:
        if include == "$self":
            return {"patterns": self.document.get("patterns") or []}
        if not include.startswith("#"):
            raise GrammarError(f"unsupported include target: {include!r}")
        name = include[1:]
        if name not in self.repository:
            raise GrammarError(f"include references an undefined repository entry: {name!r}")
        entry = self.repository[name]
        if not isinstance(entry, dict):
            raise GrammarError(f"repository entry {name!r} is not a rule")
        return entry

    def _expand(self, patterns: list[Any], seen: frozenset[str]) -> list[dict[str, Any]]:
        """Flatten `include`s and bare `patterns` groups into a list of matchable rules."""

        expanded: list[dict[str, Any]] = []
        for rule in patterns:
            if not isinstance(rule, dict):
                raise GrammarError(f"rule is not an object: {rule!r}")
            include = rule.get("include")
            if include is not None:
                if include in seen:
                    continue
                entry = self._entry(str(include))
                if "match" in entry or "begin" in entry:
                    expanded.append(entry)
                else:
                    expanded.extend(self._expand(entry.get("patterns") or [], seen | {str(include)}))
                continue
            if "match" not in rule and "begin" not in rule:
                expanded.extend(self._expand(rule.get("patterns") or [], seen))
                continue
            expanded.append(rule)
        return expanded

    def _regex(self, pattern: str) -> Any:
        compiled = self._compiled.get(pattern)
        if compiled is None:
            try:
                compiled = onigurumacffi.compile(pattern)
            except Exception as error:  # onigurumacffi raises a bare error type
                raise GrammarError(f"Oniguruma rejected {pattern!r}: {error}") from error
            self._compiled[pattern] = compiled
        return compiled

    # -- tokenization -----------------------------------------------------

    def tokenize(self, text: str) -> list[list[Token]]:
        """Tokenize `text`, returning the tokens of each line in order."""

        stack = [
            _Frame(
                scopes=(self.scope_name,),
                outer_scopes=(self.scope_name,),
                end_pattern=None,
                end_captures=None,
                patterns=self._root_patterns,
            )
        ]
        return [self._tokenize_line(line, index, stack) for index, line in enumerate(text.splitlines(keepends=True))]

    def _tokenize_line(self, line: str, index: int, stack: list[_Frame]) -> list[Token]:
        char_scopes: list[tuple[str, ...]] = [stack[-1].scopes] * len(line)

        def assign(start: int, end: int, scopes: tuple[str, ...]) -> None:
            for offset in range(max(start, 0), min(end, len(line))):
                char_scopes[offset] = scopes

        def apply(match: Any, base: tuple[str, ...], captures: dict[str, Any] | None, name: str | None) -> None:
            whole = base + ((name,) if name else ())
            assign(match.start(), match.end(), whole)
            for key in sorted(captures or {}, key=int):
                group = int(key)
                try:
                    start, end = match.span(group)
                except Exception:
                    # A capture index the rule declares but the pattern never defines.
                    continue
                if start < 0:
                    continue
                capture_name = (captures or {})[key].get("name")
                if capture_name:
                    assign(start, end, whole + (capture_name,))

        position = 0
        for _ in range(MAX_STEPS_PER_LINE):
            if position > len(line):
                break
            frame = stack[-1]

            best: tuple[int, dict[str, Any], Any] | None = None
            for rule in frame.patterns:
                pattern = rule.get("match") or rule.get("begin")
                match = self._regex(str(pattern)).search(line, position)
                if match is not None and (best is None or match.start() < best[0]):
                    best = (match.start(), rule, match)

            end_match = None
            if frame.end_pattern is not None:
                end_match = self._regex(frame.end_pattern).search(line, position)

            if end_match is not None and (best is None or end_match.start() <= best[0]):
                assign(position, end_match.start(), frame.scopes)
                apply(end_match, frame.outer_scopes, frame.end_captures, None)
                stack.pop()
                # A zero-width `end` still makes progress: it pops a frame, and a block
                # that could immediately re-open at the same offset would have to match
                # its own `begin` where its `end` just matched, which `MAX_STEPS_PER_LINE`
                # reports rather than looping on.
                position = end_match.end()
                continue

            if best is None:
                assign(position, len(line), frame.scopes)
                break

            start, rule, match = best
            assign(position, start, frame.scopes)
            name = rule.get("name")
            if "match" in rule:
                apply(match, frame.scopes, rule.get("captures"), name)
                # A zero-width `match` would re-fire at the same offset forever.
                position = match.end() if match.end() > match.start() else match.start() + 1
            else:
                apply(match, frame.scopes, rule.get("beginCaptures"), name)
                block = frame.scopes + ((name,) if name else ())
                content = block + ((rule["contentName"],) if rule.get("contentName") else ())
                stack.append(
                    _Frame(
                        scopes=content,
                        outer_scopes=block,
                        end_pattern=str(rule["end"]),
                        end_captures=rule.get("endCaptures"),
                        patterns=self._expand(rule.get("patterns") or [], frozenset()),
                    )
                )
                # A zero-width `begin` is a legal TextMate anchor: it opens its block
                # without consuming input, and the block's own rules resume from here.
                position = match.end()
        else:
            raise GrammarError(f"line {index} did not terminate: {line!r}")

        return list(_merge(line, index, char_scopes))

    # -- convenience ------------------------------------------------------

    def tokens(self, text: str) -> list[Token]:
        """Return every token of `text`, flattened across lines."""

        return [token for line_tokens in self.tokenize(text) for token in line_tokens]

    def scopes_of(self, text: str, token_text: str) -> list[tuple[str, ...]]:
        """Return the scope stack of every token whose text is exactly `token_text`."""

        return [token.scopes for token in self.tokens(text) if token.text == token_text]

    def scope_map(self, text: str) -> list[tuple[str, ...]]:
        """Return the scope stack of every character of `text`, indexed by offset."""

        scopes: list[tuple[str, ...]] = []
        for line_tokens in self.tokenize(text):
            for token in line_tokens:
                scopes.extend([token.scopes] * (token.end - token.start))
        return scopes


def _merge(line: str, index: int, char_scopes: list[tuple[str, ...]]) -> Iterator[Token]:
    start = 0
    while start < len(line):
        end = start + 1
        while end < len(line) and char_scopes[end] == char_scopes[start]:
            end += 1
        yield Token(line=index, start=start, end=end, text=line[start:end], scopes=char_scopes[start])
        start = end
