# GDScript Brace Blocks And Namespace Modules

## Status

Design approved for planning. This document assumes the top-level enum epic is complete: top-level `enum`
declarations are already a valid declaration kind and participate in namespace indexing.

## Goals

- Make brace-delimited blocks the only supported block syntax in this fork's GDScript dialect.
- Replace the implicit file script class plus `class_name`/`trait_name` model with explicit namespace module files.
- Let one `.gd` file contribute multiple public top-level namespace members.
- Keep scene script attachment and `load()`/`preload()` resource loading unambiguous with `default class`.
- Preserve current namespace/import behavior where top-level symbols are public by default.

## Non-Goals

- No compatibility mode for indentation blocks.
- No `class_name` or `trait_name` compatibility spelling.
- No top-level executable statements.
- No namespace privacy/export system in this design.
- Automatic migration tooling is out of scope for this spec.

## Source Model

A `.gd` file is a namespace module. Its root is a declaration container, not a script class. The root owns:

- one optional `namespace` declaration,
- zero or more `import` declarations,
- zero or more top-level declarations.

Top-level declarations are public namespace members by default. Valid top-level declaration kinds include classes,
traits, enums, annotations, functions, variables, constants, and any declaration kinds already admitted by the completed
top-level enum work. Top-level executable statements are parser errors.

Example:

```gdscript
namespace games.services

var retry_count: int = 3

func make_token() -> String {
	return "abc"
}

trait AuthProvider {
	func authenticate() -> bool
}

class LoginService extends RefCounted {
}

default class LoginNode extends Node {
}
```

Canonical names are formed from the file namespace plus the declaration name:

```text
games.services.retry_count
games.services.make_token
games.services.AuthProvider
games.services.LoginService
games.services.LoginNode
```

The top-level namespace symbol table is flat. A class, trait, enum, annotation, function, variable, or constant with the
same canonical name conflicts with any other namespace member of the same canonical name.

## Default Classes And Script Resources

`default class` marks the single top-level class that represents the file as a loadable or attachable `Script`
resource.

Rules:

- A file may contain at most one `default class`.
- `default class` is also a normal public namespace class and is addressable as `namespace.Name`.
- Non-default top-level classes are public, instantiable with `.new()`, and valid as type annotations once indexed.
- Files without a `default class` are valid namespace modules but cannot be attached to scenes or loaded as a
  `GDScript` resource.
- `load("res://path/file.gd")`, `preload(...)`, and scene script attachment succeed as script-resource operations only
  when the file declares exactly one `default class`.
- Files containing only traits, annotations, enums, functions, variables, constants, or non-default classes are indexed
  for symbols but rejected when a `Script` resource is required.

This separates symbol contribution from resource identity. Namespace module files can organize shared declarations
without pretending that the file itself is a class.

## Syntax

Brace blocks are canonical and required. The old colon-plus-indentation block grammar is removed.

```gdscript
func greet() -> void {
	print("Hello")
	print("World")
}

class LoginService extends RefCounted {
	var endpoint: String

	func login(name: String) -> bool {
		return name != ""
	}
}

if authenticated {
	start()
} else {
	stop()
}

var value: int {
	get { return _value }
	set(v) { _value = v }
}
```

Statements remain newline-terminated. Existing semicolon handling can continue as a separator or terminator where the
language already supports it, but semicolons are not required for ordinary statements inside brace blocks.

All block-producing forms use braces directly. This includes classes, traits, functions, lambdas, property accessors,
`if`/`elif`/`else`, `for`, `while`, `match`, and any other parser form that currently consumes an indented suite.

`class_name` and `trait_name` are removed. Use top-level `class`, `default class`, and `trait`.

## Namespace Resolution

Fully qualified access works wherever namespace-qualified values or types are valid:

```gdscript
var service: games.services.LoginService = games.services.LoginService.new()
var token := games.services.make_token()
```

Imports expose namespace members by short name. The existing ambiguity behavior is preserved: if multiple imported
namespaces provide the same short name, unqualified use is an error and callers must use a fully qualified name.

Top-level free functions and variables resolve as namespace members. Top-level variables are namespace-level static
storage and are initialized once per project/script load, not per instance of any class.

## Parser And AST Design

Introduce an explicit module/root AST node for `.gd` files. The module node owns namespace/import metadata and
top-level declarations. `ClassNode` continues to represent actual classes only, including `default class`, and no longer
doubles as the file root.

The tokenizer keeps the existing `{` and `}` tokens because dictionaries and enum bodies already use them. Parser
context decides whether braces delimit a block, enum body, dictionary literal, or dictionary pattern.

Block parsing should be centralized behind explicit helpers:

- declaration blocks for classes and traits,
- statement blocks for executable suites,
- property accessor blocks,
- lambda/function body blocks.

These helpers should own brace consumption, newline handling, empty-block validation, and common recovery. This keeps
the parser one-token-lookahead friendly and avoids scattering brace lifecycle rules across each statement parser.

Indentation tokens are no longer semantic for block structure. The tokenizer may still track newlines and whitespace
for comments, formatting, diagnostics, and editor services, but parser correctness must not depend on `INDENT`/`DEDENT`
for blocks.

## Indexing, Analysis, And Compilation

The global symbol index moves from "one global class per file via `class_name`" to "many namespace symbols per file".
For each top-level declaration the index records:

- canonical name,
- short name,
- namespace,
- declaration kind,
- source path,
- source range,
- for classes, whether it is `default`,
- for classes/traits, inheritance or trait-use metadata needed by existing editor and analyzer flows.

The analyzer resolves namespace members through this index, including top-level classes, traits, enums, annotations,
functions, variables, and constants. Imported short-name lookup uses the same ambiguity checks currently used for
namespaced classes and traits.

The compiler compiles a module as a set of namespace declarations plus optional default class resource metadata.
Namespace-level variables become static module storage initialized once per project/script load. A `default class`
compiles as a normal class and is also recorded as the file's script-resource entry point.

Resource loading asks the parsed module for its default class when a `GDScript` resource is requested. If none exists,
loading fails with a clear resource error rather than silently manufacturing an implicit root class.

## Editor, LSP, And Formatter

Editor filesystem scanning indexes every top-level namespace symbol in a file. Scene script assignment and resource
load paths validate that a target file has exactly one `default class`.

LSP and editor tooling should support:

- completion of namespace members by fully qualified path,
- completion of imported top-level functions, variables, classes, traits, enums, and annotations,
- go-to-definition for all top-level namespace members,
- diagnostics for duplicate canonical names and ambiguous imported short names,
- script-attachment diagnostics for files without a default class.

The formatter becomes brace-canonical. It emits brace blocks for every block form and never emits colon/indent blocks.
It should preserve comments and documentation comments around module headers, top-level declarations, and block bodies.

## Diagnostics

Diagnostics should be direct and migration-oriented:

```text
Expected "{" after function declaration.
Indentation blocks are no longer supported; use "{ ... }".
"class_name" is no longer supported; use a top-level "class".
"trait_name" is no longer supported; use a top-level "trait".
Only one "default class" is allowed per file.
This file has no "default class" and cannot be used as a Script resource.
Top-level statements are not allowed; only declarations are valid at file scope.
Namespace member "games.services.LoginService" is already declared as a class.
```

## Testing Strategy

Coverage should include:

- parser fixtures for every brace block form,
- parser failures for old indentation block syntax,
- parser failures for `class_name` and `trait_name`,
- parser failures for arbitrary top-level executable statements,
- top-level namespace indexing for classes, default classes, traits, enums, annotations, functions, variables, and
  constants,
- conflicts across declaration kinds in the same namespace,
- imports of top-level namespace members and ambiguity diagnostics,
- analyzer and compiler behavior for namespace-level static storage,
- resource loading and scene attachment success for files with one `default class`,
- resource loading and scene attachment rejection for namespace modules without a default class,
- formatter output for brace-canonical blocks,
- LSP completion and go-to-definition for namespace members,
- regression coverage that assumes top-level enums are already implemented.

## Implementation Staging

The language semantics are a hard cut, but the implementation should be planned as staged work:

1. Parser and AST module root.
2. Brace suite parsing across all block forms.
3. Namespace symbol index expansion.
4. Analyzer and compiler support for module-level symbols and static storage.
5. Resource loading and scene attachment validation for `default class`.
6. Editor filesystem, LSP, and refactoring updates.
7. Formatter brace-canonical output.
8. Test fixture migration and documentation updates.

Each stage should leave the tree buildable and should include focused tests for its new behavior.
