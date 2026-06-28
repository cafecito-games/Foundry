# Cafecito Foundry Script Language Primer

This primer describes the Foundry Script dialect in this Godot fork. It is written as a compact reference for people and
code-generating models that need to produce valid, idiomatic `.fs` files for this repository.

## Paradigm

Foundry Script is an imperative, object-oriented, event-driven scripting language integrated with Godot's scene tree and
resource system. Every `.fs` file defines a script class, scripts usually extend engine classes such as `Node`,
`Resource`, or `RefCounted`, and behavior is commonly driven by callbacks, signals, and per-frame methods.

It is gradually typed: type annotations are optional, but typed code gets stronger static analysis and better bytecode.
This fork adds stricter typing surfaces, including nullable object types, typed `Callable` and `Signal` signatures,
nested typed containers, generics, traits, namespaces/imports, async function declarations, final declarations, and
custom annotation declarations. It is not a pure functional language, not stack-based, and not primarily data-driven,
although it supports first-class functions, lambdas, dictionaries, arrays, and declarative annotations.

## Grammar And Syntax Cheat Sheet

### File Shape And Declaration Order

```gdscript
# Script-level annotations such as @tool must stay at the top if used.
@tool

# This fork supports one optional file-level namespace, then zero or more imports.
namespace game.characters
import game.combat
import game.ui.widgets

# Class-level annotations and head modifiers belong after namespace/import declarations.
abstract class_name PlayerController[T: Node]
extends CharacterBody2D
uses Damageable, Serializable

# Body declarations start here.
```

- Script-level annotations such as `@tool`, `@icon`, and `@static_unload` appear before `namespace` and `import`.
- `namespace a.b.c` applies to the root script class or global artifact declared by the file.
- `import a.b` makes classes, traits, enums, and custom annotations from that namespace visible in this file.
- `class_name Name` registers a global script class. With `namespace game.characters`, the canonical name is
  `game.characters.Name`.
- `trait_name Name` registers a global trait file. A root script cannot use both `class_name` and `trait_name`.
- `enum_name Name { ... }` registers a global enum file. It cannot be combined with `class_name`, `trait_name`,
  `extends`, `uses`, annotations, or body declarations.
- `class_name` and `trait_name` may declare type parameters: `class_name Box[T]`, `trait_name Store[T: Resource]`.
- `abstract` may prefix `class_name`, `trait_name`, or `extends`. `final` may prefix `class_name` or `extends`;
  traits cannot be final.
- `extends` must appear before `uses`.
- `uses` must appear before body declarations and can list multiple traits: `uses Damageable, Trackable`.
- Namespace aliases, wildcards, relative imports, and namespace blocks are not supported.

### Global Named Artifacts

Files can declare one globally named artifact. Global classes and traits still have normal script bodies. Global enums
are enum-only files.

#### Global Class Names

`class_name` registers a script class as a project-global type. The registered name can be used by other scripts in
annotations, constructors, inheritance, and member access according to the usual class rules.

```gdscript
namespace game.characters

class_name Player
extends CharacterBody2D
```

Within `namespace game.characters`, the global type is `game.characters.Player`. Scripts in the same namespace, or
scripts that import that namespace, can use the short name `Player`; other scripts can use the fully qualified name.

#### Global Trait Names

`trait_name` registers a globally named trait. A trait file follows the same namespace and import rules as
`class_name`, but the declaration describes a trait contract instead of a script class.

```gdscript
namespace game.combat

trait_name Damageable

abstract func take_damage(amount: int) -> void
```

#### Global Enum Names

`enum_name` declares a project-global enum type. It uses the normal Foundry Script enum body syntax, but the declaration is a
file-level artifact rather than a member inside a class.

```gdscript
namespace game.enums

enum_name CharacterState {
	IDLE,
	RUNNING,
	JUMPING,
}
```

The enum above registers the global type `game.enums.CharacterState`. Other scripts can use the enum as a type
annotation and can read members from the global enum name:

```gdscript
import game.enums

var state: CharacterState = CharacterState.IDLE

func set_state(next_state: CharacterState) -> void:
	state = next_state
```

The enum name also evaluates to a read-only dictionary of its members, matching regular Foundry Script enum behavior:

```gdscript
print(CharacterState.RUNNING)
print(CharacterState.keys())
```

An `enum_name` declared inside a namespace is registered with its fully qualified name. Code in the same namespace can
use the short name. Code in another namespace can either import the declaring namespace or use the fully qualified name:

```gdscript
namespace game.ui
import game.enums

var local_state: CharacterState = CharacterState.IDLE
var qualified_state: game.enums.CharacterState = game.enums.CharacterState.RUNNING
```

If two imported namespaces provide the same short enum name, use the fully qualified name to disambiguate.

An `enum_name` file contains exactly one top-level enum declaration. The only file-level declarations allowed before it
are `namespace` and `import`.

Valid:

```gdscript
namespace game.enums
import game.shared

enum_name ItemKind {
	WEAPON,
	ARMOR,
}
```

Invalid:

```gdscript
enum_name ItemKind {
	WEAPON,
}

const DEFAULT_KIND = ItemKind.WEAPON
```

### Blocks, Comments, And Statements

```gdscript
# Single-line comment.
## Documentation comment for the next declaration.

func example(flag: bool) -> void:
	if flag:
		print("indented block")
	else:
		pass # Empty blocks need pass.
```

- Blocks start with `:` and are delimited by indentation, like Python.
- Use newlines to end statements. Do not emit semicolon-terminated Foundry Script.
- Use `#` for normal comments and `##` for generated documentation comments.
- Use `pass` for an intentionally empty function, class, branch, or loop body.

### Variables, Constants, Properties, And Finality

```gdscript
var dynamic_value = 10                 # Inferred as dynamic/Variant-like in many contexts.
var hit_points: int = 100              # Explicit type.
var title := "Cafecito"                # Inferred static type from initializer.
const MAX_SPEED: float = 360.0
static var cache: Dictionary[String, Resource] = {}
static final var BUILD_ID := "debug"

@export_range(0.0, 1.0, 0.01)
var volume: float = 0.8

@onready var label: Label = %HealthLabel
```

- `var name: Type = value` declares a typed variable.
- `var name := value` asks the analyzer to infer a static type from the initializer.
- `const` values must be compile-time constants.
- `final var` values are write-once variables. Use `const` when the value is a compile-time constant.
- `static var` and `static func` are class-level members.
- `@export` and related annotations expose properties to the Inspector.
- `@onready` delays node-path lookups until the node is ready.

### Functions, Lambdas, And Async

```gdscript
func move(delta: float) -> void:
	pass

static func make_node() -> Node:
	return Node.new()

async func next_frame_name() -> String:
	await get_tree().process_frame
	return name

static async func warm_cache() -> void:
	pass

func collect(first: int, ...rest: Array) -> Array:
	return rest

var double := func(value: int) -> int:
	return value * 2

abstract func take_damage(amount: int) -> void

final func stable_id() -> int:
	return 1
```

- Function form is `func name(param: Type = default, ...rest: Array) -> ReturnType:`.
- Variadic rest parameters use `...name: Array` and must be the final parameter.
- Async functions use `async func`. Prefer the formatter's `static async func` order for static async methods.
- Await async functions and signals with `await expression` when the result or suspension matters.
- Abstract functions are declared with no body and no colon. `abstract async func` declares an async requirement.
- `final func` can override a non-final parent method, but subclasses cannot override the final method.

### Control Flow And Loops

```gdscript
if health <= 0:
	die()
elif health < 10:
	retreat()
else:
	advance()

for i in range(5):
	print(i)

for item in inventory:
	if item == "":
		continue
	print(item)

while cooldown > 0.0:
	cooldown -= get_process_delta_time()
	if cooldown < 0.0:
		break

match state:
	"idle":
		start_patrol()
	"combat" when target != null:
		attack(target)
	_:
		pass
```

- `for name in iterable:` works with arrays, dictionaries, strings, packed arrays, and `range(...)`.
- `range(end)`, `range(start, end)`, and `range(start, end, step)` stop before `end`.
- `match` supports patterns, `_` as a catch-all, and `when` guards.

### Types

```gdscript
var maybe_target: Node? = null
var target: Node
var scores: Array[int] = [10, 20, 30]
var payloads: Dictionary[String, Array[int]] = {"wave_1": [3, 4]}
var callback: Callable[[Node?], String]
var renamed: Signal[[String]]
```

- `T?` marks nullable object types. With strict null checks enabled, plain `T` is treated as non-nullable.
- `Array[T]` and `Dictionary[K, V]` are typed containers. This fork supports nested typed containers.
- `Callable[[ArgType, ...], ReturnType]` uses double brackets for the argument list.
- `Signal[[ArgType, ...]]` uses double brackets for emitted arguments.
- `Variant` is the dynamic boundary. With strict dynamic checks enabled, avoid assigning a `Variant` directly to a
  narrower static type without validation or conversion.

### Classes, Traits, Generics, And Annotations

```gdscript
trait Damageable:
	abstract func take_damage(amount: int) -> void

class Box[T: Damageable]:
	var value: T

	func hit() -> void:
		value.take_damage(1)

class Actor uses Damageable:
	var health: int = 10

	func take_damage(amount: int) -> void:
		health = max(health - amount, 0)

func identity[T](value: T) -> T:
	return value

annotation benchmark(iterations: int = 1) targets METHOD

@benchmark(100)
func measured() -> void:
	pass
```

- Generic classes and methods declare type parameters with brackets: `class Box[T]`, `func id[T](value: T) -> T`.
- Bounds use `T: Bound`, where `Bound` may be a class or trait.
- Generic class use sites provide type arguments: `Box[Actor].new()`.
- Generic method calls infer type arguments when possible: `identity(10)`. Use explicit arguments when inference is
  ambiguous: `identity[int](10)`.
- Generic class instances are reified at runtime. Generic methods are checked statically and erased at runtime.
- Traits are nominal contracts. A class satisfies a trait by declaring `uses TraitName`; matching method names alone is
  not enough.
- Custom annotation declarations are root-only and use `annotation name(...) targets METHOD, CLASS, VARIABLE`.

## Few-Shot Examples

### 1. Typed Node Script With Variables, Functions, And Loops

```gdscript
extends CharacterBody2D

@export var speed: float = 240.0
@export var hit_points: int = 3

# The % shorthand resolves a uniquely named child node when the scene is ready.
@onready var health_label: Label = %HealthLabel

var inventory: Array[String] = []
var cooldown: float = 0.0

func _physics_process(delta: float) -> void:
	var direction := Input.get_vector("move_left", "move_right", "move_up", "move_down")
	velocity = direction * speed
	move_and_slide()

	if cooldown > 0.0:
		cooldown = max(cooldown - delta, 0.0)

func add_item(item_name: String) -> void:
	if item_name.is_empty():
		return

	inventory.append(item_name)

	# A for loop iterates directly over arrays and typed arrays.
	for item in inventory:
		print(item)

func damage(amount: int) -> bool:
	hit_points = max(hit_points - amount, 0)
	health_label.text = str(hit_points)
	return hit_points == 0
```

### 2. Fibonacci Generator

```gdscript
extends RefCounted

func fibonacci(count: int) -> Array[int]:
	var result: Array[int] = []

	if count <= 0:
		return result

	var previous := 0
	var current := 1

	for _i in range(count):
		result.append(previous)

		var next := previous + current
		previous = current
		current = next

	return result

func test() -> void:
	print(fibonacci(8)) # Prints [0, 1, 1, 2, 3, 5, 8, 13].
```

### 3. String Parser Into A Typed Dictionary

```gdscript
extends RefCounted

# Parses strings such as "potion:3, arrows:12, key:1".
func parse_counts(line: String) -> Dictionary[String, int]:
	var counts: Dictionary[String, int] = {}

	for raw_piece in line.split(","):
		var piece := raw_piece.strip_edges()
		if piece.is_empty():
			continue

		var separator := piece.find(":")
		if separator < 0:
			push_warning("Skipping malformed entry: " + piece)
			continue

		var name := piece.substr(0, separator).strip_edges()
		var amount_text := piece.substr(separator + 1).strip_edges()

		if name.is_empty() or not amount_text.is_valid_int():
			push_warning("Skipping malformed entry: " + piece)
			continue

		counts[name] = amount_text.to_int()

	return counts

func test() -> void:
	var inventory := parse_counts("potion:3, arrows:12, key:1")
	print(inventory["arrows"]) # Prints 12.
```

### 4. Fork-Specific Traits And Generics

```gdscript
namespace primer.combat

trait Damageable:
	abstract func take_damage(amount: int) -> void

class HitBox[T: Damageable]:
	var target: T

	func _init(p_target: T) -> void:
		target = p_target

	func trigger(amount: int) -> void:
		# Because T is bounded by Damageable, trait members are statically visible.
		target.take_damage(amount)

class Player uses Damageable:
	var health: int = 10

	func take_damage(amount: int) -> void:
		health = max(health - amount, 0)

func test_hit_box() -> void:
	var player := Player.new()
	var hit_box := HitBox[Player].new(player)

	hit_box.trigger(3)
	print(player.health) # Prints 7.
```

### 5. Async HTTP Request

```gdscript
extends Node

async func fetch_json(url: String) -> Dictionary[String, Variant]:
	var result: Dictionary[String, Variant] = {}
	var request := HTTPRequest.new()
	add_child(request)

	var start_error := request.request(url)
	if start_error != OK:
		request.queue_free()
		push_error("HTTP request failed to start: " + error_string(start_error))
		return result

	# request_completed emits result, response_code, headers, and body.
	var completed: Array = await request.request_completed
	request.queue_free()

	var response_code := int(completed[1])
	if response_code < 200 or response_code >= 300:
		push_warning("HTTP request returned status " + str(response_code))
		return result

	# Signal and JSON data cross a dynamic boundary, so validate before returning typed data.
	var body = completed[3]
	var parsed = JSON.parse_string(body.get_string_from_utf8())

	if parsed is Dictionary:
		for key in parsed:
			if key is String:
				result[key] = parsed[key]

	return result

async func load_payload() -> void:
	var payload := await fetch_json("https://example.com/data.json")
	print(payload)
```

## Common Pitfalls

- Do not write C-style or JavaScript-style blocks. Foundry Script uses `:` plus indentation, not braces.
- Do not generate semicolon-terminated statements. Use one statement per line.
- Do not put `namespace` or `import` after `class_name`, `trait_name`, `extends`, `uses`, or body declarations.
- Do not put class-level annotations before `namespace` or `import`.
- Do not use `@abstract`; this fork uses the `abstract` keyword.
- Do not combine `abstract` and `final` on the same class or function.
- Do not apply `final` to traits, constants, signals, enums, or annotation declarations.
- Do not use `class_name` and `trait_name` in the same root script.
- Do not combine `enum_name` with any root script declaration except preceding `namespace` and `import`.
- Do not place `uses` after functions, variables, constants, signals, inner classes, or other body declarations.
- Do not call an async function or wait for a signal without `await` when the result or suspension matters.
- Do not assume structural trait conformance. A class must explicitly declare `uses TraitName`.
- Do not use namespace aliases, wildcard imports, relative imports, or `namespace { ... }` blocks.
- Do not use Godot 3 `export var` syntax. Use annotations such as `@export var value: int`.
- Do not use `$Node` or `get_node()` as a member initializer unless it is marked `@onready`.
- Do not assign `Node?` to `Node` without a null check when strict null checks are enabled.
- Do not treat `Callable` and `Signal` signatures like `Callable[String, int]`; use
  `Callable[[String], int]` and `Signal[[String]]`.
- Do not assume typed `Dictionary` methods always return narrow types. Validate or cast values that came from dynamic
  boundaries such as JSON, signals, untyped arrays, untyped dictionaries, and `Variant`.
- Do not declare custom annotations inside traits, inner classes, or functions. They are root script declarations only.
- Do not declare a rest parameter as `...args: Array[int]`; rest parameters use `Array` and must be final.
