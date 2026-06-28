# GDScript Language Primer

This primer covers fork-specific GDScript declarations that create globally named
types. These declarations live at the top of a script file, after any file-level
`namespace` or `import` declarations.

## Global Class Names

`class_name` registers a script class as a project-global type. The registered
name can be used by other scripts in annotations, constructors, inheritance, and
member access according to the usual class rules.

```gdscript
namespace game.characters

class_name Player
extends CharacterBody2D
```

Within `namespace game.characters`, the global type is
`game.characters.Player`. Scripts in the same namespace, or scripts that import
that namespace, can use the short name `Player`; other scripts can use the
fully qualified name.

## Global Trait Names

`trait_name` registers a globally named trait. A trait file follows the same
namespace rules as `class_name`, but the declaration describes a trait contract
instead of a script class.

```gdscript
namespace game.combat

trait_name Damageable

func take_damage(amount: int) -> void
```

## Global Enum Names

`enum_name` declares a project-global enum type. It uses the normal GDScript enum
body syntax, but the declaration is a file-level artifact rather than a member
inside a class.

```gdscript
namespace game.enums

enum_name CharacterState {
	IDLE,
	RUNNING,
	JUMPING,
}
```

The enum above registers the global type `game.enums.CharacterState`. Other
scripts can use the enum as a type annotation and can read members from the
global enum name:

```gdscript
import game.enums

var state: CharacterState = CharacterState.IDLE

func set_state(next_state: CharacterState) -> void:
	state = next_state
```

The enum name also evaluates to a read-only dictionary of its members, matching
regular GDScript enum behavior:

```gdscript
print(CharacterState.RUNNING)
print(CharacterState.keys())
```

### Namespace and Import Rules

An `enum_name` declared inside a namespace is registered with its fully qualified
name. Code in the same namespace can use the short name. Code in another
namespace can either import the declaring namespace or use the fully qualified
name:

```gdscript
namespace game.ui
import game.enums

var local_state: CharacterState = CharacterState.IDLE
var qualified_state: game.enums.CharacterState = game.enums.CharacterState.RUNNING
```

If two imported namespaces provide the same short enum name, use the fully
qualified name to disambiguate.

### One Enum Per File

An `enum_name` file contains exactly one top-level enum declaration. The only
file-level declarations allowed before it are `namespace` and `import`.

Do not combine `enum_name` with `class_name`, `trait_name`, `extends`, `uses`,
top-level functions, variables, constants, signals, annotations, nested classes,
or another top-level enum declaration in the same file.

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
