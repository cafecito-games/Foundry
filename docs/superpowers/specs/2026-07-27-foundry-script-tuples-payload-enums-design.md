# FoundryScript Tuples & Payload Enums — Design

Status: approved design, epic pending implementation.
Scope: two coupled language features — fixed-size tuple values, and payload-carrying enums (tagged unions).

## 1. Motivation

FoundryScript has no fixed-size aggregate value type: multi-value returns and small records force a
class, an untyped Array, or a Dictionary. Enums are pure int-backed constants, so modeling "one of N
cases, each with different data" requires a class hierarchy or ad hoc Dictionaries. Tuples provide
lightweight immutable aggregates with static shape checking; payload enums provide a single closed
type whose cases carry heterogeneous data, with destructuring in `is` tests and `match`, plus
exhaustiveness checking.

## 2. Locked decisions

1. **Runtime representation is a read-only `Array`.** Tuple value = `[field0, field1, ...]`;
   tagged-union value = `[tag: int, payload...]`; payload-less case = a singleton constant `[tag]`.
   Deep `Array` equality gives value semantics; read-only Arrays hash by content, so tuple and case
   values are valid `Dictionary` keys. No new Variant type is added.
2. **Tuples are immutable.** Element assignment (`t.0 = 5`, `t.x = 5`, `t[0] = 5` on a hard tuple
   type) is an analyzer error; hard tuple types expose no Array methods. The read-only Array is the
   runtime backstop for Variant-typed escapes.
3. **Named tuples are nominal; unnamed tuples are structural.** A named tuple is assignable to the
   unnamed tuple of its element types (safe erasure); unnamed→named and named A→named B require
   explicit construction. Element types are invariant in v1.
4. **Payload enums reuse the existing enum type kind** (`DataType::Kind::ENUM`) with an
   `is_tagged_union` flag rather than a new kind, so name resolution, enum methods, and editor
   surfaces keep working. If any case declares a payload, the whole enum is a tagged union; its
   values have `builtin_type = ARRAY` (not `INT`), so int-context misuse fails type checks.
5. **Tags are ordinal by declaration order (0-based).** Explicit `= value` on any case of a tagged
   union is an error. Consequence: reordering cases is a serialization break; documented, with a
   future `@tag(n)` annotation as the escape hatch.
6. **Runtime type tests are structural.** `x is Vec2` and `x is (int, String)` test shape
   (Array + arity + element types); named identity is erased at runtime. Same class of erasure as
   int-backed enums today. No warning is emitted.
7. **`@export` of tuple or tagged-union typed properties is an analyzer error in v1.** Inspector
   support is a follow-up.
8. **No 1-tuples or empty tuples.** `(a)` remains expression grouping; `(a,)` is a hard error.
   Trailing commas are allowed at arity ≥ 2 in literals, types, declarations, and patterns.
9. **GRAMMAR.md is updated in the same change as any syntax PR** (§10 sync policy).

## 3. Tuples

### 3.1 Syntax

```
tuple Vec2(x: float, y: float)          # named declaration (class body or whole file)
tuple Player(name: String, int, bool)   # mixed named/positional fields

var p := (10, 20)                       # unnamed literal, arity >= 2
var pos: (int, int) = p                 # unnamed tuple type annotation
func get_data() -> (String, int): ...   # in return types, parameters, generics, anywhere a type appears

p.0                                     # index access, valid on ALL tuples
v.x                                     # named access, only where a name is declared
var (x, y) = pos                        # destructuring declaration
const (name, hp) = get_player()         # const destructuring
```

Grammar deltas (informal; exact EBNF goes into `modules/foundry_script/GRAMMAR.md`):

```
tuple_decl      = "tuple", identifier, "(", tuple_field, { ",", tuple_field }, [ "," ], ")" ;
tuple_field     = [ identifier, ":" ], type ;            (* positional field = bare type *)
type            = ... | "(", type, ",", type, { ",", type }, [ "," ], ")" ;
tuple_literal   = "(", expression, ",", expression, { ",", expression }, [ "," ], ")" ;
attribute       = postfix, ".", ( identifier | integer ) ;   (* integer form: tuple index *)
destructure     = ( "var" | "const" ), "(", binding, ",", binding, { ",", binding }, ")", "=", expression ;
binding         = identifier | "_" ;
```

New keyword: `tuple` (fits `MAX_KEYWORD_LENGTH 10`; `'t'` keyword group,
`fs_tokenizer.cpp:519-585`). Also added to `FSLanguage::get_reserved_words()`
(`foundry_script.cpp:3569`) and the highlighter.

### 3.2 Tokenizer: `.0` vs float literals

Today `fs_tokenizer.cpp:1542-1555` lexes `.<digit>` as a number unconditionally, so `t.0` is
`IDENTIFIER` + `LITERAL(0.0)`. Fix, mirroring the existing `+`/`-` sign-number disambiguation
(`fs_tokenizer.cpp:1560,1570` gated on `!last_token.can_precede_bin_op()`,
`can_precede_bin_op()` at `fs_tokenizer.cpp:183-199`):

- In the `'.'` case, take the number path only when `is_digit(_peek()) &&
  !last_token.can_precede_bin_op()`. After an identifier/`)`/`]`/literal, `.` is a `PERIOD`.
- In `number()`, when the previous token is `PERIOD`, lex a decimal **integer only**: no fractional
  part, no exponent, no non-decimal prefix, no type suffix. `x.0.1` lexes as
  `x . 0 . 1` (nested access works); `x.0e5` / `x.0x1` are errors ("expected tuple index").
- Unaffected: `.5`, `1.0`, `0.5e3`, `(f()).0` behaves as member access after `)`.
- New GRAMMAR.md §9 ambiguity entries: `.` after a value token is member access; `(a,)` and `(a)`.

### 3.3 Typing

New `DataType::Kind::TUPLE` (`fs_parser.h:107-351`):

- Element types reuse `container_element_types` — generic substitution (`substitute()`) recurses
  through them already, so `class Box[T]` holding `(T, int)` works with no extra code.
- New `Vector<StringName> tuple_field_names`, parallel to elements; empty `StringName` for
  positional fields. New `StringName tuple_name` (empty = unnamed/structural) plus script-path
  linkage for nominal identity.
- `builtin_type = ARRAY` (erasure target). The hand-written `DataType::operator=`
  (`fs_parser.h:309-342`) must copy every new field.
- `to_string`/`to_property_info`/`can_reference`/`substitute` updated in `fs_parser_data_type.cpp`;
  compatibility rules in `fs_type.cpp` (`FSTypeCompatibility::check`).

Rules:

- Compatibility: unnamed↔unnamed by arity + invariant element compatibility; named→unnamed allowed;
  unnamed→named and named A→named B errors ("construct explicitly").
- Construction: a named tuple name resolves to a meta-type (like `make_enum_type`,
  `fs_analyzer.cpp:1467`); `reduce_call` gains a branch: callee is a tuple meta-type → arity check,
  element-wise argument check with usual implicit conversions, result is the named tuple instance
  type. Positional arguments only in v1.
- Access: `.name` resolves at analyze time to its field index; `.0`-style indices are checked
  against arity. `.0` on a hard non-tuple, out-of-range index, or unknown field name → error.
  On `Variant`, `.0` lowers to a runtime indexed get; `.name` follows the normal dynamic attribute
  path (documented asymmetry).
- Immutability: assignment to a tuple element access or subscript on a hard tuple is an error;
  Array methods are not exposed on hard tuple types.
- Cyclic by-value tuple types (a named tuple containing itself as a field) are an error, detected
  during resolution like other `RESOLVING` cycles. Indirection via `Array`/objects is fine.
- `@export` on tuple-typed properties → error.

### 3.4 Lowering

- `_gdtype_from_datatype` (`fs_compiler.cpp:295`): TUPLE → plain `ARRAY` (elements are
  heterogeneous; no typed-array container).
- New opcode `OPCODE_CONSTRUCT_TUPLE(count)`: pops N operands, builds an Array, `make_read_only()`,
  pushes. Serves unnamed literals, named-tuple construction, and enum payload construction.
- `.0`/`.name` compile to constant-indexed Array gets. Parser marks `SubscriptNode` with
  `is_tuple_index` so the analyzer distinguishes `.0` from `[0]`.
- Destructuring: new `VariableDestructureNode` (new `Node::Type`) with `Vector<IdentifierNode *>`
  (`_` → null slot), const flag, and initializer. Hook: `parse_variable` (`fs_parser.cpp:2226`)
  branches on `(` after `var`/`const`; statement dispatch `fs_parser.cpp:3286/3291`. Analyzer: RHS
  must be a hard tuple of matching arity (Variant RHS is an error in v1); bindings become suite
  locals with element types. Compiler: RHS into a temp, then N constant-indexed gets into fresh
  locals. Not in v1: nested destructuring, per-binding type annotations, destructuring in `for`.
- Type tests: new `OPCODE_TYPE_TEST_TUPLE` (is Array + size == arity + per-element type test) fills
  the `write_type_test` branch (`fs_byte_codegen.cpp:593-645`). New runtime `FSDataType` kind
  `TUPLE` inserted **before** `TYPE_PARAMETER`, which must stay last for the bytecode loader bound
  check (`fs_function.h:57-71`) → bytecode format version bump.
- Equality/printing: Array deep `==` gives value equality (a named and unnamed tuple with equal
  elements are `==`). `str((1, 2))` prints as an Array (`[1, 2]`) — accepted limitation, documented.
- `Array[(int, int)]` erases to an array of Arrays at runtime; the analyzer keeps the precise
  element type.

### 3.5 Match patterns

New pattern kind `PT_TUPLE` in `PatternNode` (`fs_parser.h:1319-1353`): parenthesized sub-pattern
list, arity ≥ 2, reusing the `array` sub-pattern vector. Analyzer propagates tuple element types
into sub-patterns (like `PT_ARRAY` does with container element types,
`fs_analyzer.cpp:4347-4359`); compiler lowers exactly like the `PT_ARRAY` boolean tree
(`fs_compiler.cpp:2307-2403`) minus the rest-pattern support. Because of erasure, `[a, b]` array
patterns also match tuple values — documented artifact.

## 4. Payload enums (tagged unions)

### 4.1 Syntax

```
enum Message:
    Quit
    Move(x: int, y: int)
    Write(text: String)

func handle_message(msg: Message) -> void:
    if msg is Message.Move(x, y):
        prints("preview move", x, y)
    match msg:
        Message.Quit:
            print("quit")
        Message.Move(x, y):
            prints("move", x, y)
        Message.Write(_):
            print("write")
```

Grammar deltas: an enum case may declare a payload `(field: Type, ...)` (same `tuple_field` form,
names required on payload fields in v1); if any case has a payload, the enum is a tagged union and
`= value` on any case is an error. Mixed payload/payload-less cases are allowed. Enum methods
remain supported; inside a method `self` is the case value.

### 4.2 Parser

`parse_enum` (`fs_parser.cpp:2636-2857`): `EnumNode::Value` gains a payload field list
(identifier + `TypeNode *` pairs); `EnumNode` gains `is_tagged_union`. Today's rule requiring
`NAME = expr` explicit values inverts for tagged unions: values are forbidden, tags are ordinal.

### 4.3 Typing

`DataType` gains `is_tagged_union` plus per-case payload metadata (field names/types keyed by case
name; `enum_values` continues to map case name → tag). `make_enum_type` (`fs_analyzer.cpp:1467`)
and `resolve_enum_values` (`fs_analyzer_surface.cpp:696`) branch: tagged unions get
`builtin_type = ARRAY` (values) / `DICTIONARY` (meta), no constant folding of case ints.

- `Message.Quit` (payload-less) is a **value**: a per-case read-only singleton `[tag]` emitted into
  script constants (like enum constants at `fs_compiler.cpp:4297-4335`). Uniform `[tag, ...]`
  representation means tag extraction is always element 0 for every case of the type.
- `Message.Move` un-called is a case-constructor pseudo-type; using it as a value is an error.
  `Message.Move(1, 2)` arity/type-checks and lowers to `OPCODE_CONSTRUCT_TUPLE` with the tag
  constant as the first operand → `[tag, 1, 2]` read-only.
- Payload field access directly on the union (`m.x`) is an error — fields exist per-case only;
  access is via `is` binds or `match`.
- Tagged-union values in int contexts (arithmetic, bitwise, int-enum assignment) fail type checks
  via `builtin_type = ARRAY`, with friendly diagnostics added.
- **Audit requirement**: every switch on `DataType::Kind::ENUM` (analyzer, `fs_compiler.cpp:394-405`,
  `fs_type.cpp`, editor/LSP sites) must be enumerated by grep and dispositioned in the implementing
  PR; this is the principal risk of reusing the ENUM kind.

### 4.4 `is Case(binds)`

Extends `TypeTestNode` (`fs_parser.h:1564-1572`; parse at `fs_parser.cpp:5139`): after
`parse_type()`, if the next token is `(` and the type is a dotted name, parse a bind list
(`identifier | _`), stored as `Vector<IdentifierNode *> case_binds` plus a resolved case reference.

- Analyzer: RHS must resolve to a tagged-union case; bind count must equal payload arity
  (`_` skips). Binds on a payload-less case → error (`msg is Message.Quit` without parens is the
  supported tag test). `is not` with binds → error.
- **Bind position restriction**: binds are only permitted when the test is the condition (or an
  `and`-conjunct of the condition) of `if`/`elif`/`while`/`assert` — exactly the positions
  `apply_flow_narrowing_from_condition` handles (`fs_analyzer_flow_finality.cpp:1744-1770`).
  Anywhere else (`or`, `not`, ternary, arbitrary expressions) a bind-carrying test is an error; the
  bind-free form remains an ordinary boolean expression. Binds become suite locals of the guarded
  suite (`SuiteNode::Local::PATTERN_BIND` precedent, `fs_parser.h:1422-1436`) typed from case
  fields; the tested identifier also flow-narrows to the case
  (`type_test_narrowing_identifier`, `fs_analyzer_flow_finality.cpp:1670-1702`).
- Lowering: subject evaluated once into a temp; test = is Array ∧ size == 1+arity ∧
  `temp[0] == tag`, via new `OPCODE_TYPE_TEST_ENUM_CASE(tag, arity)`. Bind assignments (indexed
  gets from the temp) are emitted at the head of the true branch.
- Also added: `OPCODE_TYPE_TEST_ENUM` for `x is SomeEnum` (int-backed: int + membership; tagged
  union: shape + tag range). This closes an existing hole — today an ENUM `DataType` in
  `write_type_test` falls into the "Compiler bug: unresolved type in type test" default.

### 4.5 Match patterns and exhaustiveness

New pattern kind `PT_ENUM_CASE`: case reference (dotted attribute expression) + full sub-patterns
(binds, literals, wildcards, nested tuples/arrays), reusing the `PT_ARRAY` machinery and the
root-pattern `binds` map (`fs_parser.cpp:3844-3862`; the existing "binds only with a single
pattern per branch" rule stands). Payload-less cases in `match` are plain expression patterns —
deep Array `==` against the singleton is already correct. Compiler branch in
`_parse_match_pattern` (`fs_compiler.cpp:2139-2570`): tag check + per-field sub-pattern tests
against indexed gets.

Exhaustiveness (`check_match_exhaustiveness`, `fs_analyzer.cpp:4146-4249`): for a tagged-union
subject the finite domain is the case set. A case is covered by a bind/wildcard pattern, a bare
payload-less case expression, or a `PT_ENUM_CASE` whose sub-patterns are all irrefutable
(binds/wildcards, recursively). Case patterns with refutable sub-patterns (e.g. `Move(0, y)`)
conservatively cover nothing. Diagnostics name the missing cases.

## 5. Cross-cutting

- **Whole-file declarations**: `tuple` at file head mirrors the whole-file enum declaration
  (`is_enum_file`/`enum_file_decl`, `fs_parser.h:996-997`; `parse_enum_name`
  `fs_parser.cpp:1428`). Registration through `_get_global_class_name`
  (`foundry_script.cpp:3682-3804`); whether the script server carries a distinct tuple bit beside
  the existing enum bit is resolved in the implementing issue.
- **Bytecode**: format version bump; export/loader (`fs_bytecode_export.cpp:1076-1120`,
  `fs_bytecode_loader.cpp:1818-1880`) carry the TUPLE `FSDataType`, tagged-union metadata
  (flag + per-case arity table), and the four new opcodes (`OPCODE_CONSTRUCT_TUPLE`,
  `OPCODE_TYPE_TEST_TUPLE`, `OPCODE_TYPE_TEST_ENUM`, `OPCODE_TYPE_TEST_ENUM_CASE`); disassembler
  updated.
- **Name mangler** (`fs_name_mangler_analysis.cpp`, `fs_name_mangler_application.cpp`,
  `KEEP_RULES.md`): tuple/payload field names are analyzer-only (runtime is positional) and freely
  manglable; tuple type names and case names follow existing global/enum-value keep rules.
- **Formatter** (`fs_format.cpp`): tuple declarations in `print_member` (1258), tuple
  literals/types in expression/type printers, destructure statements (`print_statement` 1769),
  payload cases (`print_enum` 1591), `PT_TUPLE`/`PT_ENUM_CASE` (`print_pattern` 2065); fixture
  directories under `modules/foundry_script/tests/scripts/format/`.
- **Editor/LSP/docs**: completion for `.name`/`.0` and case constructors (`fs_editor.cpp`),
  document symbols (`language_server/fs_extend_parser.cpp:242-495`), docgen
  (`editor/fs_docgen.cpp`), refactoring at least non-crashing (`editor/fs_refactoring.cpp`).

## 6. Diagnostics inventory (new errors unless noted)

Single-element tuple literal/type; empty tuple; tuple arity mismatch (construction, destructure,
pattern); unknown tuple field; tuple index out of range; assignment to tuple element (immutable);
Array method on hard tuple; named-tuple nominal mismatch; destructure of non-tuple/Variant RHS;
cyclic by-value tuple type; `@export` on tuple/tagged-union type; explicit `= value` in tagged
union; payload case used without construction; binds on payload-less case; bind count mismatch;
`is not` with binds; bind-carrying `is` outside supported condition positions; tagged union in int
context; direct payload field access on union type; duplicate field names in a tuple/case;
non-exhaustive match naming missing cases (warning, existing codes); bind shadowing (existing
warning reused).

## 7. Edge cases

`(expr)` grouping vs `(a,)` error · trailing commas at arity ≥ 2 · `x.0.1` nested access ·
`x.0e5`/`x.0x1` errors · `.5`/`1.0`/`0.5e3` unaffected · `[a, b]` array patterns match tuples
(erasure artifact, documented) · payload-less cases are singleton values, never called · enum
methods on tagged unions (`self` = Array value) · tagged union where int-enum expected (error) ·
`Array[(int, int)]` erasure · tuples/cases as Dictionary keys (supported; content hash) · default
parameter values of tuple type (constant-foldable construction only) · `match` on a Variant subject
holding a tuple (patterns work; exhaustiveness bails as today) · comparing case constructors
(`Message.Move == Message.Write`) is a pseudo-type-in-value-context error · reordering tagged-union
cases changes tags (serialization break, documented).

## 8. Open items deferred from v1

- Inspector/`@export` support for tuples and tagged unions (PropertyInfo design).
- Stable case tags via a `@tag(n)`-style annotation.
- Nested destructuring, destructuring in `for`, per-binding type annotations.
- Element-type covariance for immutable tuples.
- Dedicated tuple printing (`(1, 2)` instead of Array printing).

## 9. Epic decomposition

Fourteen issues; dependency spine `1→2→3→4→{5,6}→{7,8}` for tuples and `9→10→11→12→13→14` for
payload enums, with 9 startable after 2 and 10 gated on 3. Each issue carries its own mechanical
acceptance criteria (fixtures under `modules/foundry_script/tests/scripts/`, full test suite green,
GRAMMAR.md/format fixtures in the same PR as syntax changes). See the epic issue for the live list.
