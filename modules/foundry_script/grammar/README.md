# FoundryScript TextMate grammar

`tmlanguage_builder.py` generates the TextMate grammar editors use to highlight `.fs`
files. The artifact is **not** committed: it is generated on demand and published as
`foundryscript-tmlanguage-<version>.json` alongside every release.

```sh
python3 modules/foundry_script/grammar/tmlanguage_builder.py --output /tmp/foundryscript.tmLanguage.json
```

The document declares scope name `source.foundryscript` and file type `fs`. Generation is
byte-deterministic: identical inputs always produce an identical file.

## Inputs

| Input | Owns |
| --- | --- |
| `../fs_tokenizer.cpp` | The reserved words and the numeric keyword constants, derived from the `KEYWORDS` table. |
| `patterns/keyword_scopes.json` | Which TextMate scope each derived keyword carries, plus the boolean/null literals the tokenizer deliberately does not reserve. |
| `patterns/lexical.json` | Comments, strings, numbers, annotations, node references, operators, and punctuation. |
| `patterns/constructs.json` | Declaration names, calls, type positions, and the contextual keywords. |

The tokenizer keeps three keyword sets apart and so does the generator. Reserved words and
numeric keyword constants get their own rules. Contextual keywords (`annotation`, `extend`,
`async`, `targets`, `get`, `set`) are ordinary identifiers, so they only ever reach the
grammar through the narrowly anchored rules in `patterns/constructs.json`; they are never
emitted into a keyword alternation.

Reserved words are never scoped in `patterns/constructs.json`. Declaration rules match
through a lookbehind on the already-consumed keyword, so a keyword's scope is stated in
exactly one place.

## Keeping it honest

`misc/checks/check_foundryscript_tmlanguage.py` fails when the tokenizer table,
`../GRAMMAR.md` section 2.5, and the generated grammar disagree about any of the three
sets. `tests/python_build/test_foundryscript_tmlanguage.py` generates the grammar and
tokenizes `fixtures/highlighting_sample.fs` with a real Oniguruma engine.

Adding a keyword to the tokenizer therefore requires, in the same change: the section 2.5
entry in `../GRAMMAR.md`, and a scope in `patterns/keyword_scopes.json`.
