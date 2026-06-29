# Foundry rename toolkit

A re-runnable, idempotent, context-aware toolkit that renames the Godot fork to
**Foundry** and its scripting language from **GDScript** to **Foundry Script**
(`.fs`). It has two parts:

1. `generate_map.py` — scans the frozen source tree and emits `naming_map.tsv`,
   a concrete `from -> to` table merged with the hand-curated `seed.tsv`.
2. `rename.py` — applies `naming_map.tsv` to file contents and file/dir names.

> Always dry-run first. These passes touch thousands of files; verify the plan
> before writing anything.

## The naming model

Every rule carries a **category** and a **context**:

| Category | Family | Example |
| --- | --- | --- |
| A | GDScript family | `GDScriptParser -> FSParser`, `GDScript -> FoundryScript` |
| B | GDExtension family | `GDExtensionManager -> FoundryExtensionManager` |
| C | GD* object macros | `GDCLASS -> FOUNDRY_CLASS`, `GDVIRTUAL0 -> FOUNDRY_VIRTUAL0` |
| D | GODOT_* / branding | `GODOT_VERSION -> FOUNDRY_VERSION`, `Godot Engine -> Foundry` |

| Context | Meaning |
| --- | --- |
| `code` | C++/Python identifiers. `GDScript -> FoundryScript`. |
| `prose` | user-facing text. `GDScript -> Foundry Script` (with a space). |
| `both` | file extensions, filenames, URLs — applied in either pass. |

The `prose` rule for `GDScript` is **additive**: it coexists with the `code`
rule, and the two are told apart by the `context` column. The content pass
selects rules for the requested context plus all `both` rules.

Rules are always applied **longest-source-first**, so `GDScriptParser` becomes
`FSParser` before the shorter `GDScript` rule can touch it.

## Token classification rules

`generate_map.py` discovers identifiers with these transforms:

- `GDScript` -> `FoundryScript`; `GDScript<Rest>` -> `FS<Rest>` (A)
- `GDExtension` -> `FoundryExtension`; `GDExtension<Rest>` -> `FoundryExtension<Rest>` (B)
- `GDCLASS` -> `FOUNDRY_CLASS`; `GDSOFTCLASS` -> `FOUNDRY_SOFTCLASS`;
  `GDVIRTUAL<suffix>` -> `FOUNDRY_VIRTUAL<suffix>`;
  `GDREGISTER_<NAME>` -> `FOUNDRY_REGISTER_<NAME>` (C)
- `GODOT_<NAME>` -> `FOUNDRY_<NAME>` (D)

Only source-code file types are scanned (see `SOURCE_EXTENSIONS`). Prose and
translation files (`.po`, `.md`, doc `.xml`, ...) are deliberately skipped: they
only reference tokens already present in code and would otherwise introduce
false tokens (e.g. the Czech declension `GDScriptu` from editor `.po` files).

## The seed (`seed.tsv`)

`seed.tsv` supplies, on top of the generated rows:

- the additive prose rule `GDScript -> Foundry Script`;
- file extension / filename rules (`.gd`, `.gdc`, `.gde`, `.gdignore`,
  `.gdextension`, `project.godot`);
- branding (`Godot Engine -> Foundry`, `godotengine.org -> cafecito.games`);
- overrides — on a `(from, context)` collision, the **seed wins**;
- exclusions — a seed row whose `to` is the literal `!EXCLUDE` removes every
  row (all contexts) with that `from` from the output.

Seed row contexts are inferred from shape: a space in `from`/`to` -> `prose`;
a leading `.` or an embedded `.` (extensions, filenames, URLs) -> `both`;
otherwise -> `code`.

## Regenerate the map

```sh
cd tools/foundry-rename
python generate_map.py --dry-run | head -40   # inspect
python generate_map.py --out naming_map.tsv   # write the committed map
```

`naming_map.tsv` is generated but committed so the rename passes are
reproducible. Regenerate it whenever `seed.tsv` or the source tree changes.

## Run the rename passes

Dry-run first, every time.

```sh
# Content (identifiers / macros):
python rename.py --context code  --content --dry-run
python rename.py --context code  --content

# Content (user-facing prose, e.g. docs/translations):
python rename.py --context prose --content --dry-run

# File/dir renames via git mv (deepest paths first; prose rules never move files):
python rename.py --moves --dry-run
python rename.py --moves
```

Restrict the content pass to specific files by passing paths:
`python rename.py --context code --content path/to/file.cpp ...`

`thirdparty/**` and `.git/**` are never read or modified. Running any pass a
second time is a no-op (the engine is idempotent). Extension rules are anchored
to a word boundary, so `.gd` rewrites `.gd` but never `.gdshader`.

## Tests

```sh
cd tools/foundry-rename
python -m pytest tests/ -v
```

Tests are hermetic: they use small fixture TSVs under `tests/fixtures/` rather
than the full generated map. `conftest.py` puts this directory on `sys.path`, so
pytest can be run from anywhere.

> Note on interpreters: use a Python 3.11–3.13 interpreter. The toolkit is pure
> standard library, but `pytest` must be importable (e.g. in a virtualenv:
> `python3.12 -m venv venv && ./venv/bin/pip install pytest`).

## License headers

Every Python file here begins with the Foundry MIT header. The canonical text
(both the `#` form and the C/C++ `/* */` box form) lives in
`header_template.txt`.
