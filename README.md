# Foundry

> **Foundry is an experimental fork of Godot Engine 4.6.3 (MIT).** See
> [NOTICE](NOTICE) and [LICENSE.txt](LICENSE.txt) for attribution and license
> details.

<p align="center">
  <a href="https://godotengine.org">
    <img src="logo_outlined.svg" width="400" alt="Foundry logo">
  </a>
</p>

Foundry is an experimental game engine built on top of the
[Godot Engine](https://godotengine.org). It keeps everything that makes Godot a
great cross-platform 2D and 3D engine — the scene system, the renderer, the
one-click exports — and rebuilds the scripting experience around two ideas:

- **Foundry Script**, a new statically-richer scripting language derived from
  GDScript, with generics, traits, tuples, tagged unions, nullable types,
  namespaces, and checked fixed-width integers.
- **Editor tooling as a first-class product**: an analyzer-backed refactoring
  suite, a canonical formatter, a language server for external IDEs, a script
  test runner, and an editor automation layer (MCP) that lets AI agents drive
  the real editor GUI.

## ⚠️ Experimental

This is an experiment to see how far the Godot Engine can be pushed in a few
specific directions: a more type-safe scripting language, better editor tooling
for scripting, and language features that aren't present in stock GDScript.

A large amount of the implementation is being done with AI assistance (primarily
Claude and Codex). This is primarily a personal project built around my own
tastes for what the language and tooling should look like. It may be useful to
others, or it may not be. **Support, stability, and backwards compatibility are
not guaranteed** — treat it as experimental and don't ship production games on
it without understanding that caveat. Published builds are available from
[Foundry releases](https://github.com/cafecito-games/Foundry/releases) for
anyone who wants to try them out.

## Foundry Script

Foundry Script is the engine's scripting language, derived from GDScript and
implemented in [`modules/foundry_script/`](modules/foundry_script/). Scripts use
the `.fs` extension, projects use a `project.foundry` config file, and the
grammar is specified normatively in
[`modules/foundry_script/GRAMMAR.md`](modules/foundry_script/GRAMMAR.md).

The language remains gradually typed — untyped `var` is still legal — but every
addition below pushes toward catching errors at compile time. What follows is a
tour of everything that differs from GDScript.

### Nullable types

Types can be marked nullable with a `?` suffix: `Node?`, `Array[int]?`. The `?`
is only a type suffix — there is no `?:`, `?.`, or `??` operator. With the
opt-in `strict_null_checks` project setting, a plain `T` is treated as
non-nullable and the analyzer enforces null-safety at compile time.

```foundry
var target: Node? = null

func describe(node: Node?) -> String:
	if node == null:
		return "nothing"
	return node.name
```

### Fixed-width integers

Where GDScript has a single 64-bit `int`, Foundry Script has four integer
types: `int` (32-bit signed), `uint` (32-bit unsigned), `long` (64-bit signed),
and `ulong` (64-bit unsigned), with literal suffixes `U`, `L`, and `UL`.

- Arithmetic is **checked**: overflow, divide-by-zero, and invalid shifts are
  errors instead of silent wraparound.
- Only safe implicit conversions exist (`int → long`, `uint → ulong`,
  `int → float`); everything else requires an explicit `as` cast.
- Mixing signedness in one expression (`uint + long`) is an error, as is unary
  `-` on an unsigned value.

```foundry
var count: uint = 42U
var big: long = 1L << 40
```

### Generics

Classes, inner classes, traits, functions, and tagged unions can take type
parameters, with optional upper bounds that may be a class or a trait.

```foundry
class_name Box[T: Damageable]

var value: T

func swap[T](a: T, b: T) -> Array[T]:
	return [b, a]
```

Type arguments are supplied at use sites (`Box[int].new()`,
`swap[float](1, 2)`) or inferred. Class generics are **reified** at runtime —
assigning a `String` into a `Box[int]` is a runtime error — while method
generics are checked statically and erased. `is` and `as` observe reified
arguments, so `crate is Box[int]` asks a real question about the value.

### Traits

Traits are nominal, composable units of state and behavior — closer to Rust
traits or Swift protocols with default implementations than to duck typing. A
trait can carry variables, constants, signals, concrete methods, and
`abstract func` requirements, and traits can inherit from other traits.

```foundry
trait_name Damageable

signal died
var health: int = 100

abstract func take_damage(amount: int) -> void

func is_alive() -> bool:
	return health > 0
```

Classes opt in with `uses`:

```foundry
class_name Player
extends CharacterBody2D
uses Damageable, Movable
```

Traits participate fully in the type system: they work as parameter and return
types, in typed containers, with `is`/`as`, and as generic bounds
(`Box[T: Damageable]`). Traits themselves can be generic
(`trait Container[T]`).

**Retroactive conformance** lets you implement a trait for a type you don't
own — including native engine classes and builtin value types:

```foundry
extend int uses Printable:
	func to_label() -> String:
		return str(self)
```

Conformances are checked for coherence (no duplicate implementations, no
method-name collisions on the same target) and are visible along explicit load
edges, so an unreached conformance is an analysis error rather than a silent
runtime miss.

### Tuples

Foundry Script has structural tuples (GDScript has none): tuple literals,
tuple types, named tuple declarations, index access, and destructuring.

```foundry
tuple Size(width: int, height: int)

var pair := (1, "two")            # tuple literal
var typed: (int, String) = pair   # unnamed tuple type
print(pair.0, pair.1)             # index access

var (quotient, remainder) = divmod(7, 3)   # destructuring
```

Tuples always have arity ≥ 2 (`()` and `(a,)` are parse errors), destructuring
supports `_` to discard fields, and tuple patterns work in `match`.

### Enums and tagged unions

Enum bodies use indentation instead of braces, members are newline-separated,
and every plain-enum value must be explicit — no implicit ordinals:

```foundry
enum Direction:
	UP = 0
	DOWN = 1
```

Named enums can also declare functions after their values. More significantly,
enum cases can carry **typed payloads**, turning the enum into a tagged union:

```foundry
enum Message:
	Quit
	Move(x: int, y: int)
	Write(text: String)
```

Tagged unions can be **generic** and **recursive**, which enables types like
the builtin `Result[T, E]` (with `Ok`/`Err` cases) that ships with the engine:

```foundry
enum Tree[T]:
	Leaf(value: T)
	Branch(children: Array[Tree[T]])
```

Wherever the expected type is known, cases can be written with the contextual
`.Case` shorthand:

```foundry
func parse(text: String) -> Result[int, String]:
	if not text.is_valid_int():
		return .Err("not a number")
	return .Ok(text.to_int())
```

### Pattern matching and flow analysis

`match` understands case patterns with payload binds, generic case patterns,
and tuple patterns — and exhaustiveness is normative for flow analysis: a
`match` over a tagged union, enum, or `bool` that covers every case counts as
terminating, so no unreachable trailing `return` is needed.

```foundry
match message:
	Message.Quit:
		queue_free()
	Message.Move(x, y):
		position += Vector2(x, y)
	Message.Write(text):
		print(text)
```

`is` supports payload binds directly in conditions:

```foundry
if message is Message.Move(x, y):
	position += Vector2(x, y)
```

### Namespaces and imports

Files can declare a dotted namespace and import others, giving projects
hierarchical organization and conflict-free naming:

```foundry
namespace game.characters
import game.combat

class_name Player
```

Fully-qualified names (`game.combat.Attack`) always work without an import.
Whole files can also declare a single global artifact with `trait_name`,
`enum_name`, or `tuple_name` in place of `class_name`.

Namespaces extend to **native engine classes**: engine APIs can register under
a namespace (for example `foundry.http.server.HTTPServer`) and are reached via
`import` or a fully-qualified name rather than occupying the global scope.

### Coroutines and typed callables

Coroutine-ness is part of the type system rather than a runtime surprise:

- `async func` declares a coroutine contract, reflected in method metadata and
  enforced across overrides.
- Calling an async function yields a typed `Coroutine[T]` handle; `await`
  consumes it to produce `T`. A `Coroutine[T]` is deliberately not
  assignment-compatible with `T`, so a forgotten `await` is a compile-time
  error in expression position.
- `Callable` and `Signal` take full signatures: `Callable[[int, String], bool]`,
  `AsyncCallable[[Node], void]`, `Signal[[int]]`, including variadic tails
  (`Callable[[String, ...Array[int]], int]`).

### Declarations and modifiers

- **`final`** — a true keyword, usable on classes (no subclassing), methods (no
  overriding), and variables. `final var` members and locals are write-once
  with definite-assignment analysis, like Java finals.
- **`abstract`** — a keyword (not an annotation) for classes, traits, and
  methods. An `abstract func` has no body and the modifier is required for any
  body-less function.
- **Typed rest parameters** — `func tally(...values: Array[int])` collects
  surplus arguments into a genuinely typed array, checked at every call site.
- **Named call arguments** — `spawn(position = Vector2.ZERO, count = 3)`,
  resolved entirely at compile time with source-order evaluation.
- **Custom annotations** — declare your own with typed parameters and target
  lists (`annotation benchmark(iterations: int = 1) targets METHOD, CLASS`),
  namespace-scoped and importable.
- **`@autoload`** — register autoloads from the script itself
  (`@autoload(depends_on = [...])`) instead of editing project settings, backed
  by an analyzer-maintained autoload index.
- **`Self` and `Type[T]`** — a contextual self type that always means the
  runtime receiver, preserved across inheritance and `super`, and a metatype
  for passing class handles to generic factories
  (`func make[T](t: Type[T]) -> T`).
- **Property accessors** — inline `get:`/`set(value):` blocks or pointer-style
  `get = _getter, set = _setter`.

### Strict typing, opt in

Two project settings tighten the gradual defaults: `strict_null_checks`
(non-nullable-by-default `T`) and `strict_dynamic_checks` (no implicit
`Variant` → static-type assignment). A migration driver
(`foundry script migrate --strict null,dynamic --apply`) and an in-editor
wizard help move existing code over incrementally.

### Removed from GDScript

Foundry Script is a clean break, not a superset. Notable removals:

- Brace-delimited enums, comma-separated enum members, and implicit enum
  ordinals.
- The `@abstract` annotation (replaced by the `abstract` keyword).
- Embedded/built-in scripts inside scene files — scripts are always real files.
- Silent integer wraparound and implicit narrowing conversions.
- `?` as an operator, 1-tuples, and empty tuples.
- By default, export templates ship without the script front-end: exported
  games load only compiled bytecode, with name-mangled symbols and a
  `@keep_name` escape hatch.

## Script tooling

The toolchain around the language is part of the product:

- **Canonical formatter** — `foundry script format` rewrites source to one
  canonical style (gofmt-style, zero configuration), preserving comments and
  refusing to format files with parse errors.
- **Linter** — `foundry script lint` surfaces the analyzer's full warning set
  from the command line, suitable for CI.
- **Refactoring suite** — analyzer-backed rename (cross-file, including tuple
  fields and enum cases), extract variable/method, inline variable, add type
  annotation, implement abstract methods, insert explicit cast, widen to
  nullable, and sort members by the style guide.
- **Language server** — completion, go-to-definition, find-references, hover,
  and semantic tokens over LSP, plus a debug adapter, served together with
  `foundry tooling serve --project <dir> --lsp-port 0 --dap-port 0` for
  external IDE integration.
- **Test runner** — `foundry project test --runner res://tests/runner.fs` runs
  project-owned test runners under an engine-side execution guard with
  timeouts and structured (JSONL/TAP) reporting; `foundry test run` executes
  the engine's own suites with machine-readable progress events.
- **Inline eval** — `foundry script eval 'print("ok")'` for quick scripting
  from the shell.

## Editor automation (MCP)

The Foundry editor embeds a local automation server that speaks the
[Model Context Protocol](https://modelcontextprotocol.io), so AI agents can
drive the real editor GUI — no screenshots, no pixel coordinates:

```sh
foundry editor open --project <project> --automation
```

The editor prints a `FOUNDRY_AUTOMATION` line with a local HTTP MCP endpoint
and bearer token. Agents (or the bundled stdio bridge,
[`scripts/foundry_mcp_server.py`](scripts/foundry_mcp_server.py)) can then:

- **Observe** the semantic UI tree — windows, roles, names, focus, and the
  modal stack — instead of guessing from pixels.
- **Find** controls with semantic selectors
  (`{role: "button", name: "Add Child Node"}`).
- **Act** on them: click, type, select, drive menus, edit inspector values.
- **Wait** on real conditions (a selector appearing, the modal stack settling,
  imports going idle, a log line) instead of sleeping.
- **Read** editor state, logs, and resources; run command-palette commands;
  poll the event stream; capture screenshots.

This is how editor features get tested end-to-end in this repository: an agent
opens a dialog, clicks through it, and asserts the scene tree changed — the
same path a human takes. Registering the bridge in an MCP-capable client is
one line:

```sh
claude mcp add foundry-editor -- python3 scripts/foundry_mcp_server.py
```

See [`docs/editor_automation_mcp_client.md`](docs/editor_automation_mcp_client.md)
for client configuration examples.

## Getting the engine

### Compiling from source

Foundry builds the same way as Godot. For example, on macOS:

```sh
scons platform=macos target=editor dev_build=yes tests=yes
```

Use `platform=linuxbsd` on Linux or `platform=windows` on Windows. See the
[official Godot docs](https://docs.godotengine.org/en/latest/engine_details/development/compiling)
for platform-specific prerequisites and options, and [CONTRIBUTING.md](CONTRIBUTING.md)
for repository conventions.

### Binary downloads

Published editor binaries and export templates are available from the
[Foundry releases](https://github.com/cafecito-games/Foundry/releases).

### Headless container

Containers begin publishing with the first non-draft container-enabled release
after this feature lands. They use `ghcr.io/cafecito-games/foundry`, a minimal
`linux/amd64` image for Foundry Script tooling and project-owned test runners.
Before using a command, choose an actually published exact or channel tag and
set `FOUNDRY_IMAGE` to it:

```sh
FOUNDRY_IMAGE='ghcr.io/cafecito-games/foundry:<published-exact-or-channel-tag>'
```

Every image has an exact tag matching its Git release tag, including the leading
`v`. Moving tags are channel-specific: `latest-alpha`, `latest-beta`, and
`latest-rc` track prereleases, while `latest` tracks stable releases only.

The image automatically passes `--headless` to Foundry and uses `/workspace` as
its working directory:

```sh
docker run --rm \
  --tmpfs /workspace/.foundry:rw,uid=10001,gid=10001,mode=0700 \
  -v "$PWD:/workspace:ro" \
  "$FOUNDRY_IMAGE" \
  script lint .

docker run --rm \
  --tmpfs /workspace/.foundry:rw,uid=10001,gid=10001,mode=0700 \
  -v "$PWD:/workspace:ro" \
  "$FOUNDRY_IMAGE" \
  project test --project . --runner res://tests/runner.fs

docker run --rm \
  "$FOUNDRY_IMAGE" \
  script eval 'print("ok")'
```

The container runs as the non-root UID/GID `10001:10001`. Read-only commands
work with readable bind mounts. The lint and project-test examples keep the
project read-only and make only Foundry's `.foundry` metadata ephemeral and
writable. Commands that rewrite files require a writable bind mount. On a host
whose checkout has a different owner, run mutating commands with the host
UID/GID and temporary writable home and XDG directories:

```sh
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp/foundry-home \
  -e XDG_CONFIG_HOME=/tmp/foundry-home/.config \
  -e XDG_CACHE_HOME=/tmp/foundry-home/.cache \
  -e XDG_DATA_HOME=/tmp/foundry-home/.local/share \
  -v "$PWD:/workspace" \
  "$FOUNDRY_IMAGE" \
  script format --write .
```

The initial image is `linux/amd64` only and contains Foundry plus its runtime
libraries, not export templates, compilers, Git, or the internal engine test
suite. The GHCR package is intended to be public. After its first publication,
verify anonymous access from a shell that is not authenticated to GHCR. Set
`PUBLISHED_EXACT_TAG` to an actually published exact tag, including its leading
`v`:

```sh
docker pull "ghcr.io/cafecito-games/foundry:${PUBLISHED_EXACT_TAG:?Set this to a published exact v tag}"
```

If the pull fails, an organization administrator must switch the package
visibility to Public in GitHub Packages.

## About Godot

Foundry is built on **[Godot Engine](https://godotengine.org)**, a feature-packed,
cross-platform engine for creating 2D and 3D games from a unified interface. It
provides a comprehensive set of [common tools](https://godotengine.org/features)
and one-click export to desktop (Linux, macOS, Windows), mobile (Android, iOS),
Web, and [consoles](https://godotengine.org/consoles).

Godot is free and open source under the permissive
[MIT license](https://godotengine.org/license) — no strings attached, no
royalties. Its development is independent and community-driven, supported by the
[Godot Foundation](https://godot.foundation/). Before being open sourced in
[February 2014](https://github.com/godotengine/godot/commit/0b806ee0fc9097fa7bda7ac0109191c9c5e0a1ac),
Godot was developed by [Juan Linietsky](https://github.com/reduz) and
[Ariel Manzur](https://github.com/punto-) as an in-house engine.

## Godot documentation and resources

The upstream Godot resources remain the best reference for engine features that
Foundry inherits:

- The official documentation is hosted on [Read the Docs](https://docs.godotengine.org).
  The [class reference](https://docs.godotengine.org/en/latest/classes/) is also
  accessible from within the editor.
- Official demos live in their own [GitHub repository](https://github.com/godotengine/godot-demo-projects),
  alongside a list of [awesome Godot community resources](https://github.com/godotengine/awesome-godot).
- Additional community [learning resources](https://docs.godotengine.org/en/latest/community/tutorials.html)
  (text and video tutorials, demos, etc.) are available through the
  [community channels](https://godotengine.org/community).
