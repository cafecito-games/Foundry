# Fixed-width integers — release gate record

Closeout record for the fixed-width integer epic. It states what the shipped surface is, records the
repository-wide unsigned-carrier audit, and lists the gaps that are tracked rather than closed.

## Shipped contract

- The public integer surface is exactly `int` (i32), `uint` (u32), `long` (i64), and `ulong` (u64).
- Literal suffixes are exactly `U`, `L`, and `UL`, uppercase-only; lowercase, mixed-case, and `LU`
  are rejected with the canonical spelling.
- `Variant::UINT` is appended at 39, immediately before `VARIANT_MAX` (40). No pre-existing
  `Variant::Type` value moved.
- Arithmetic, shifts, division, and conversions are checked. Constant folding and the virtual machine
  answer through one implementation (`modules/foundry_script/fs_numeric_ops.{h,cpp}`), so they agree
  by construction, and a failed compound assignment does not modify its destination.
- Implicit conversion is value-preserving: `int` -> `long`, `uint` -> `ulong`, and `int` -> `float`.
  Everything else needs `as`.
- Width erases at an untyped Variant boundary; signedness does not.

User-facing documentation is `docs/foundry_script/fixed_width_integers.md` and
`docs/foundry_script/int_to_long_migration.md`. The normative syntax is
`modules/foundry_script/GRAMMAR.md` section 2.6.1 and section 7.

## Repository-wide unsigned-carrier audit

Every `switch` in `core/`, `editor/`, `modules/`, `scene/`, `servers/`, `tests/`, `main/`, `drivers/`,
and `platform/` that has a `case Variant::INT:` label was checked for a matching `case
Variant::UINT:` label. Forty-four switches have no UINT label. Each is classified below.

### Required, changed in this issue

- `modules/foundry_script/editor/fs_refactoring.cpp` `default_return_literal()` — a generated
  override/implementation stub for a `uint` or `ulong` return emitted `pass`, producing a stub that
  fails "not all code paths return a value". It now emits `0U` or `0UL`. Covered by
  `modules/foundry_script/tests/test_refactor.h`, "Override method returns an unsigned zero for
  unsigned return types".

### Correct without a UINT label

- `modules/foundry_script/fs_analyzer.cpp` (dictionary index compatibility) — the `default:` arm
  requires an exact carrier match, which is the intended rule: a signed index does not silently key
  an unsigned-keyed dictionary.
- `modules/foundry_script/fs_byte_codegen.cpp` (validated-evaluator avoidance for `/` and `%`) —
  unreachable for integer carriers. `_checked_binary_type()` claims every operation the checked model
  handles, including both carriers, and emits `OPCODE_NUMERIC_BINARY` before this switch is reached.
- `modules/foundry_script/fs_byte_codegen.cpp` (`for` loop opcode specialization) — an unspecialized
  carrier falls through to the generic iterator, which is correct, only unspecialized.
- `modules/foundry_script/fs_parser.cpp` (`@export` annotation error text) — enumerates the packed
  array spellings for annotation-expected types. No annotation expects an unsigned carrier.

### Domain-typed, not arbitrary-Variant sites

These switches dispatch on a value whose type the surrounding subsystem has already fixed, so an
unsigned carrier cannot reach them: `scene/resources/visual_shader.cpp` and
`editor/shader/visual_shader_editor_plugin.cpp` (shader port types),
`scene/resources/animation.cpp` and `editor/animation/animation_track_editor.cpp` (track value
types), `modules/gltf/structures/gltf_accessor.cpp` (glTF component types),
`editor/scene/gui/font_config_plugin.cpp` (font configuration properties),
`servers/rendering/rendering_device.cpp` (shader uniform types), `core/io/plist.cpp` (property-list
node types), `editor/scene/connections_dialog.cpp` (the signal-binding type picker's offered types),
`editor/automation/editor_automation_workflow.cpp` (workflow step arguments), and
`modules/mono/editor/bindings_generator.cpp` (the C# module, which is not part of this release).

### Tracked gaps, not changed here

- `core/variant/variant_utility.cpp` — `floor`, `ceil`, `round`, `abs`, `sign`, `snapped`, `lerp`,
  and `wrap` reject an unsigned argument with an explicit invalid-argument error rather than
  operating on it. No silent misbehavior, but a usability gap; adding unsigned support to the global
  numeric utilities is feature work beyond this issue.
- `modules/foundry_script/fs_json_marshal.cpp` — `JsonNode.of()` refuses an unsigned value. The JSON
  boundary is explicitly lossy by design, and refusing is safer than approximating, but the subset of
  unsigned values a JSON integer represents exactly could be accepted.
- `platform/web/javascript_bridge_singleton.cpp` and `platform/android/jni_utils.cpp` — the platform
  Variant bridges do not marshal the unsigned carrier. Neither platform is validated in this release,
  and both were unverifiable from a macOS host.

## Verified epic acceptance items

Each item is backed by an existing test rather than re-implemented here.

| Epic acceptance item | Coverage |
|---|---|
| `Variant::UINT` appended, existing numeric values unchanged | `tests/core/variant/test_variant_unsigned_carrier.h`, `doc/classes/@GlobalScope.xml` (`TYPE_UINT` = 39, `TYPE_MAX` = 40) |
| Public surface is exactly the four names | `modules/foundry_script/tests/scripts/analyzer/features/fixed_width_integer_types.fs` |
| Suffixes are exactly `U`, `L`, `UL` | `modules/foundry_script/tests/test_integer_literal_suffixes.h`; `modules/foundry_script/GRAMMAR.md` section 2.6.1 |
| Overflow, division by zero, invalid shifts, invalid casts produce stable errors | `.../analyzer/errors/fixed_width_integer_constant_overflow.fs`, `.../fixed_width_integer_checked_operations.fs`, `.../fixed_width_integer_checked_casts.fs`, `modules/foundry_script/tests/test_checked_numeric_runtime.h` |
| Failed compound operations do not mutate their destination | `modules/foundry_script/tests/test_checked_numeric_runtime.h` |
| `UINT64_MAX` round-trips through exact text/binary/resource/debug/RPC paths | `tests/core/variant/test_variant_unsigned_carrier.h`, `tests/core/io/test_marshalls.h`, `tests/core/io/test_resource.h` |
| Native 8/16/32/64-bit metadata maps to the approved constraints | `modules/foundry_script/tests/test_native_integer_metadata.h` |
| Typed containers distinguish widths sharing a carrier | `modules/foundry_script/tests/test_typed_container_numeric_width.h` |
| Promotion and rejection matrix | `.../analyzer/features/fixed_width_integer_promotions.fs`, `.../analyzer/errors/fixed_width_integer_unsafe_mix.fs` |
| Cross-script width retention | `.../analyzer/features/fixed_width_integer_cross_script.fs`, `.../analyzer/errors/fixed_width_integer_cross_script_narrowing.fs` |

## Tracked gaps at close

The epic ships with these open, documented in
`docs/foundry_script/fixed_width_integers.md#known-limitations`:

- #1684 — `int` still applies no width constraint. `var value: int = 4000000000L` is accepted, and
  `int` arithmetic is checked at 64 bits.
- #1759 — an unsuffixed positive constant does not cross to the unsigned carrier.
- #1757 — builtin Variant method binds carry no integer metadata.
- #1758 — `ADD_PROPERTY(Variant::INT)` with unsigned accessors disagrees with generic reflection.
- #1763 — compound assignment on a flow-narrowed value, missing `int()`/`uint()`/`long()`/`ulong()`
  conversion-call syntax, and `TypedArray` hint strings erasing element widths.
- `uint` does not reach `long` implicitly, and `uint` does not reach `float` implicitly, although
  both conversions are value-preserving.
- Global numeric utilities, `JsonNode.of()`, and the web/Android Variant bridges do not accept the
  unsigned carrier.
