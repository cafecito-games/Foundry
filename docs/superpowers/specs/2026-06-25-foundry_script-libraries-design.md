# GDScript Libraries — Design

**Date:** 2026-06-25
**Status:** Approved (brainstorm) — pending implementation plan
**Fork:** CafecitoGames / Godot Engine

## 1. Concept & Scope

A **library** is a versioned, distributable bundle of **namespaced GDScript** containing
only `.gd` and `.gdshader` files. Libraries are a *sibling* to addons, not a replacement:

- **Addons** enhance the editor — `EditorPlugin`, `@tool` scripts, scenes, custom docks.
- **Libraries** are pure code — classes and traits consumed by a game at runtime.

A library's identity is its **namespace** (e.g. `games.cafecito.utilities`), which is already
a first-class concept in this fork (the `namespace` keyword, `namespace.ClassName` resolution
in `ScriptServer`, file-local `imports`).

The system is modeled after Go modules (direct sources, no central registry) with a
vendored, `node_modules`-style install layout.

### Requirements being satisfied
1. Libraries are **namespaced** (`games.cafecito.utilities`).
2. Libraries declare an **engine-version requirement**; unmet requirements are surfaced.
3. Libraries are **pure classes/traits** — they never modify the editor.

## 2. Distribution Model — Vendored

- **Direct sources** (Go-modules style): every dependency names its own source
  (`git` / `path` / `http`). There is **no central registry** in v1.
- A **global download cache** at `~/.cache/godot-libraries/` holds fetched and extracted
  versions, deduplicated across projects.
- Resolution **vendors** the resolved set into `res://libraries/<namespace>/…` using
  flat dotted folder names (e.g. `res://libraries/games.cafecito.utilities/`). This folder is
  **gitignored**. Because the vendored files are plain `res://` resources, existing class and
  namespace resolution, script caching, and the export pipeline work without modification.

The global cache is a *download* cache; `res://libraries/` is the *install* set.

## 3. Manifests & Lockfile

All files use **TOML**.

### `library.toml` — in a library repository's root
```toml
[library]
namespace = "games.cafecito.utilities"   # identity; MUST match the namespace its scripts declare
version = "1.2.0"                          # the library's own semver, aligned with its git tag
requires_engine = ">=4.6"                  # requirement #2
authors = ["..."]
license = "MIT"
src = "src"                                # folder holding the .gd/.gdshader files (default "src")

[dependencies]                             # transitive deps, same syntax as a consumer
games.cafecito.math = { git = "https://github.com/...", version = "^2.0" }
```

### `libraries.toml` — in a consuming project's root (checked into git)
```toml
[dependencies]
games.cafecito.utilities = { git = "git@github.com:cafecito/utilities.git", version = "^1.2" }
games.cafecito.math      = { path = "../local-math" }                  # local development
some.vendor.thing        = { http = "https://.../thing-1.0.0.zip", sha256 = "..." }
```

### `libraries.lock` — in the project root (checked into git)
- Fully **flattened**: one entry per namespace.
- Records resolved version, exact git **sha**, source, and an **integrity hash** of the
  vendored content for reproducible, verifiable installs.

## 4. Versioning & Resolution

- Manifests use **semver ranges** (`^1.2`, `>=1.2,<2.0`). The resolver lists git tags for a
  source and selects the highest version satisfying the constraints.
- **Single version per namespace** is a hard constraint: namespaces register global
  `class_name`s in `ScriptServer`, so two versions of the same namespace would collide.
- **Diamond resolution:** when multiple dependents constrain the same namespace, pick the
  highest version satisfying the **intersection** of all ranges. If no single version
  satisfies all constraints, fail with a clear **hard error** identifying the conflicting
  demands.
- The lockfile pins exact shas so a fresh `res://libraries/` install is identical across
  machines and CI.

## 5. Engine-Version Requirement (Requirement #2)

- A library declares `requires_engine` as a semver range against the fork's engine version
  string (there is no separate "GDScript language level"; the engine version is the gate).
- An unmet requirement is a **warning by default**. The Editor Setting
  `libraries/strict_engine_requirement` promotes it to a **hard error / refusal to install**.
- Enforced at **resolve/install time** (primary gate) and re-checked at **load time** in case
  the engine was downgraded.

## 6. Authentication & Fetching

- **Shell out to the system `git` binary.** Its path is configurable via an Editor Setting,
  falling back to `git` on `PATH`.
- **Private git (SSH/HTTPS):** inherit the user's `ssh-agent` and git **credential helpers**.
  The system stores no secrets of its own for git sources.
- **Private GitHub release assets / generic HTTP:** obtain a token from, in order, an env var
  (`GITHUB_TOKEN` / `GH_TOKEN`), a global `credentials.toml` (host → token), or
  `gh auth token` as a fallback.
- `http` archive sources **require** a `sha256` in the manifest; the download is verified
  against it.

## 7. Validation Gate (Requirement #3 — pure, safe libraries)

Run at vendor time by the libraries service, using the **real GDScript parser** so that
validation never drifts from the language:

1. Only `.gd` and `.gdshader` files are allowed. Reject scenes, `.tres`, binaries, and any
   other asset types.
2. Reject any script that extends `EditorPlugin` or is marked `@tool` — those belong in addons.
3. The `namespace` declared in `library.toml` **must match** the `namespace` keyword in the
   shipped scripts. A mismatch is an error.
4. Reject attempts to register autoloads or otherwise mutate project settings — a library must
   remain inert with respect to project configuration.

(The allowed file set is intentionally tight for v1 and may widen later as use cases arise.)

## 8. Libraries Service & Front-Ends

### Core libraries service (new engine module/component)
Implements resolver, fetcher, vendorer, validator, and lockfile reader/writer. It is the
single source of truth and is **headlessly testable** with the existing test infrastructure.

### `godotcli` — new lean SCons build target
A dedicated build target linking core + the GDScript module + the libraries service, **without
editor, renderer, or audio**. It shares 100% of the engine's logic (version string, GDScript
parser for validation, semver). Verbs:
- `install` — vendor exactly what `libraries.lock` specifies, or resolve + write the lockfile
  if none exists.
- `update [namespace]` — re-resolve within the declared ranges and rewrite the lockfile.
- `add` / `remove` — edit `libraries.toml` and re-resolve.

### Editor UI (later milestone)
A dock — sibling to the AssetLib/Plugins tab — that drives the same service: list declared
libraries, install/update, and surface conflict and engine-requirement warnings inline.

### Auto-install on project open
When the editor opens a project whose vendored `res://libraries/` does not match
`libraries.lock`, it **auto-installs to match the lockfile** (cache-first; network only for
missing artifacts). Re-resolving *ranges* — which can change versions — remains an explicit
`update`, so opening a project never silently changes which versions are in use. Network access
and auto-install are governable by an Editor Setting for offline/CI scenarios.

## 9. Treatment of Vendored Code

- `res://libraries/` is **third-party / read-only**. It is excluded from the fork's
  migration/strict-typing tooling and "your code" scans — it is compiled but never offered for
  refactor/migration. Reuse the existing `.gdignore` mechanism (or an equivalent marker) to
  achieve "compile but don't tool" behavior.
- Project scaffolding adds `res://libraries/` to `.gitignore` and ensures `libraries.toml`
  and `libraries.lock` are committed.

## 10. Out of Scope for v1 (YAGNI)

- Central registry, search, and a publishing service.
- Assets other than `.gd` / `.gdshader`.
- Multiple concurrent versions of a single namespace.
- Editor-modifying libraries (that is what addons are for).

## Key Engine Touch-Points (reference)

| Area | File(s) |
|---|---|
| Global class / namespace registry | `core/object/script_language.h` (`ScriptServer`, `GlobalScriptClass`) |
| Namespace resolution | `modules/gdscript/gdscript_analyzer.cpp` (`get_global_class_in_namespace`, ~L5294) |
| Script caching / invalidation | `modules/gdscript/gdscript_cache.h` |
| Traits | `modules/gdscript/gdscript_parser.h` (`TraitNode`), `gdscript_trait_utils.h` |
| Validation via parser | `modules/gdscript/gdscript_parser.*`, `gdscript_analyzer.*` |
| Addon conventions (reference sibling) | `editor/editor_node.cpp`, `editor/plugins/editor_plugin_settings.cpp` |
| Project settings / gitignore scaffolding | `core/config/project_settings.cpp` |
| Engine version string | `core/version.h` / `Engine::get_version_info()` |
