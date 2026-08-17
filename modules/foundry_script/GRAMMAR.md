# Foundry Script Grammar Specification

This document is the authoritative, exhaustive grammar specification for **Foundry
Script** (`.fs`), the gradually-typed scripting language implemented by this module.
It is written to be detailed enough to serve as a blueprint for re-implementing the
front-end (tokenizer + parser) in another language, in particular a
[Pratt / precedence-climbing parser](https://en.wikipedia.org/wiki/Operator-precedence_parser)
for the expression layer.

It mirrors the behavior of the reference implementation:

- Tokenizer: [`fs_tokenizer.h`](fs_tokenizer.h) / [`fs_tokenizer.cpp`](fs_tokenizer.cpp)
- Parser: [`fs_parser.h`](fs_parser.h) / [`fs_parser.cpp`](fs_parser.cpp)

> **Keep this in sync.** Any change to the scripting language that affects the grammar
> (new tokens, keywords, operators, precedence, statements, declarations, type syntax,
> etc.) **must** be reflected here in the same change. See the note at the bottom and in
> the repository `AGENTS.md`.

Foundry Script is a fork/derivative of GDScript. If you know GDScript, most of this will
be familiar; the Foundry-specific additions (traits, namespaces/imports, generics,
nullable types, `Coroutine[T]`/`async`/`await`, custom annotation declarations,
`final`/`abstract`, named call arguments, `enum_name`/`trait_name` files) are called out
throughout.

---

## 1. Notation

The grammar is described using **ISO/IEC 14977 Extended Backus–Naur Form (EBNF)** with
the conventions below. EBNF is used because it is a recognized standard and expresses
optionality, repetition, and grouping compactly.

| Notation        | Meaning                                                     |
|-----------------|-------------------------------------------------------------|
| `=`             | definition                                                  |
| `,`             | concatenation (sequence)                                    |
| `\|`            | alternation (choice)                                        |
| `[ x ]`         | optional: zero or one `x`                                   |
| `{ x }`         | repetition: zero or more `x`                                |
| `( x )`         | grouping                                                    |
| `"x"`           | terminal: a literal token / keyword / punctuation           |
| `UPPER_CASE`    | a lexical token class produced by the tokenizer (§2)        |
| `lower_case`    | a non-terminal (grammar production)                         |
| `(* ... *)`     | comment                                                     |

Because Foundry Script is **indentation-sensitive**, the tokenizer emits three synthetic
layout tokens — `NEWLINE`, `INDENT`, `DEDENT` — exactly like Python. These are treated as
ordinary terminals in the syntactic grammar (§4+). Their generation rules are described in
§2.3.

The expression grammar (§5) is **not** expressed purely as EBNF productions; instead it is
specified as a Pratt parser with an explicit precedence table, because that is how the
reference parser is built and is the recommended way to re-implement it. The EBNF for
expressions is provided as an informative cross-check.

---

## 2. Lexical grammar

The tokenizer reads UTF-32 code points and produces a stream of `Token`s. The complete
token type enumeration is `FSTokenizer::Token::Type` in [`fs_tokenizer.h`](fs_tokenizer.h).

### 2.1 Whitespace, line continuation, and comments

```ebnf
whitespace        = " " | "\t" ;                 (* space or tab *)
line_comment      = "#", { any_char - newline } ;
line_continuation = "\\", newline ;              (* backslash before EOL joins lines *)
```

- **Indentation** is significant only at the start of a logical line (see §2.3). Other
  whitespace separates tokens and is otherwise insignificant.
- A `#` begins a comment that runs to the end of the physical line. A comment starting
  with `##` is a **documentation comment** and is captured separately for tooling (it does
  not affect grammar).
- A backslash `\` immediately before a newline is a **line continuation**: the logical
  line continues on the next physical line and no `NEWLINE` token is produced.
- Inside an open bracket pair (`(`, `[`, `{`) the tokenizer is in **multiline mode**:
  newlines and indentation are ignored and no `NEWLINE`/`INDENT`/`DEDENT` tokens are
  produced until the matching closing bracket. The parser also enables multiline mode
  explicitly around constructs such as call/parameter lists.

### 2.2 The cursor / completion

In editor/tooling builds the tokenizer tracks a "cursor" position for code completion.
This does **not** affect the language grammar and can be ignored by a standalone
implementation.

### 2.3 Layout tokens: `NEWLINE`, `INDENT`, `DEDENT`

These are generated, not written in source:

- `NEWLINE` — emitted at the end of a logical line that contains tokens. Blank lines and
  comment-only lines do not produce `NEWLINE`. Suppressed in multiline mode and across a
  line continuation.
- `INDENT` — emitted when a logical line's leading indentation is **greater** than the top
  of the indentation stack.
- `DEDENT` — emitted (possibly several at once) when leading indentation is **less** than
  the top of the stack, unwinding to a matching level.

Indentation may use spaces or tabs, but the **first** indentation character encountered in
a file fixes the indent character for the whole file; mixing produces an error. The
default tab size is 4. A block is introduced syntactically by `:` followed by a `NEWLINE`
and an `INDENT`; it ends at the matching `DEDENT`.

Lambdas manipulate the indentation stack specially (see `push_expression_indented_block` /
`pop_expression_indented_block`) so a multi-line lambda body can appear inside an
expression.

### 2.4 Identifiers

```ebnf
identifier      = id_start, { id_continue } ;
id_start        = unicode_xid_start  | "_" ;
id_continue     = unicode_xid_continue | "_" ;
```

- Identifiers follow Unicode identifier rules (`is_unicode_identifier_start` /
  `is_unicode_identifier_continue`); ASCII letters, digits (not first), and `_` are the
  common case. A lone `_` is the dedicated `UNDERSCORE` token (used as the match wildcard),
  not an identifier.
- In tooling builds, identifiers that are confusable with keywords or that fail a spoof
  check produce a warning/error, but this is not part of the core grammar.

### 2.5 Keywords

Reserved keywords (from the tokenizer keyword table). Each maps to a dedicated token type:

```
abstract  as        and       assert    await
break     breakpoint
class     class_name const     continue
elif      else      enum      enum_name extends
final     for       func
if        import    in        is
match
namespace not
or
pass      preload
return
self      signal    static    super
trait     trait_name tuple     tuple_name
uses
var       void
while     when
yield
```

Built-in numeric constants are also keyword tokens: `INF`, `NAN`, `PI`, `TAU` (token types
`CONST_INF`, `CONST_NAN`, `CONST_PI`, `CONST_TAU`).

**Contextual keywords** (lexed as ordinary `IDENTIFIER`, given meaning only by position):

- `annotation` — starts a custom annotation declaration when it appears where a root-level
  declaration is valid (§4.4).
- `extend` — starts a retroactive trait conformance when it appears where a root-level
  declaration is valid (§4.8). Distinct from the `extends` keyword token: `extend` is an
  ordinary identifier everywhere else, just like `annotation`.
- `async` — function modifier when it immediately precedes `func`/other modifiers (§4.5).
- `targets` — separates an annotation declaration's parameter list from its target list.
- `get` / `set` — property accessor names.
- `CLASS`, `METHOD`, `VARIABLE`, `SIGNAL`, `CONSTANT`, `PARAMETER` — annotation target names (uppercase,
  so they are ordinary identifiers).

**Keywords usable as identifiers / node names:** A few keyword tokens are still accepted
where an identifier is expected: `match`, `when`, `uses`, and the constant keywords
`PI`/`TAU`/`INF`/`NAN` are treated as identifiers (`Token::is_identifier`). Additionally a
broad set of keywords is accepted as **node names** after `$`/`%`/`/` in a get-node path
(`Token::is_node_name`), and as attribute names after `.`.

`yield` is reserved but always an error (removed; use `await`).

### 2.6 Literals

All of the following produce a `LITERAL` token whose `literal` value carries the parsed
value/type.

#### 2.6.1 Numbers

```ebnf
number       = integer | float ;

integer      = integer_body, [ integer_suffix ] ;
integer_body = int_dec | int_hex | int_bin ;
integer_suffix = "U" | "L" | "UL" ;    (* uppercase only; "LU" is not a suffix *)

int_dec      = digit, { digit | "_" } ;
int_hex      = "0", ("x" | "X"), hex_digit, { hex_digit | "_" } ;
int_bin      = "0", ("b" | "B"), bin_digit, { bin_digit | "_" } ;

float        = ( dec_part, ".", [ dec_part ] | ".", dec_part | dec_part ),
               [ exponent ] ;          (* must contain "." and/or exponent *)
dec_part     = digit, { digit | "_" } ;
exponent     = ("e" | "E"), [ "+" | "-" ], digit, { digit | "_" } ;

digit        = "0".."9" ;
hex_digit    = digit | "a".."f" | "A".."F" ;
bin_digit    = "0" | "1" ;
```

Rules and constraints enforced by the tokenizer:

- `_` digit separators are allowed for readability but **not** adjacent (`1__0` is an
  error) and not immediately after a base prefix (`0x_`, `0b_`) or a decimal point
  (`10._`).
- Hex and binary literals must have at least one digit after the prefix and may not contain
  a decimal point or exponent.
- A decimal point may not appear twice; `..` is tokenized as the range/rest token, not a
  second decimal point, so `1..2` is `1`, `..`, `2`.
- A letter immediately following a number is an "invalid numeric notation" error, except for
  the integer suffixes below.
- Decimal literals with a `.` or exponent become floats; otherwise integers. Hex/bin become
  integers.
- An integer suffix applies equally to decimal, hexadecimal, binary, and underscore-separated
  literals, and selects the literal's type: `U` is `uint`, `L` is `long`, and `UL` is `ulong`.
  An unsuffixed integer is `int` when its value fits `int`, otherwise `long`.

  ```foundry
  42       # int
  42U      # uint
  42L      # long
  42UL     # ulong
  0xFFFFU  # uint
  0b1010UL # ulong
  1_000L   # long
  ```

- Suffixes are **uppercase only** and ordered `U` before `L`. `1u`, `1l`, `1uL`, `1Ul`, and
  `1LU` are all errors naming the exact canonical replacement (`1U`, `1L`, `1UL`, `1UL`, and
  `1UL`). Any other trailing letters remain an "invalid numeric notation" error.
- A suffix selects the type outright: it never widens on overflow and never reinterprets the
  bit pattern. A literal outside the suffixed type's inclusive range is an error, as is a
  negative literal with `U` or `UL`. An unsuffixed literal above the `long` range is an error
  that names the `UL` suffix.
- A suffix is not allowed on a float literal (one with a decimal point or exponent) or on a
  tuple index.
- A digit is only the start of a number when the preceding token cannot end a value
  (`Token::can_precede_bin_op()` is false), mirroring the `+`/`-` sign-number rule. After a
  value token (`IDENTIFIER`, a literal, `)`, `]`, or a numeric constant keyword), `.` followed
  by a digit is a `PERIOD` token (tuple index access, e.g. `t.0`), not the start of a float.
- A digit immediately following a `PERIOD` token lexes as a decimal integer **only**: no
  fractional part, exponent, base prefix, or trailing letter is allowed, so `t.0.1` is nested
  member access (`t`, `.`, `0`, `.`, `1`) and `t.0e5`/`t.0x1` are lexer errors.

#### 2.6.2 Strings

```ebnf
string        = [ string_prefix ], ( short_string | long_string ) ;
string_prefix = "r" | "&" | "^" ;     (* raw, StringName, NodePath *)
short_string  = '"',  { schar  | escape }, '"'
              | "'",  { schar' | escape }, "'" ;
long_string   = '"""', { any - '"""' }, '"""'
              | "'''", { any - "'''" }, "'''" ;
```

- `"..."` and `'...'` are equivalent regular strings. Triple-quoted forms (`"""`/`'''`) are
  **multiline** strings.
- Prefixes (applied to the quote that follows):
  - `r"..."` — **raw** string: backslashes are literal except `\"`/`\'` (to allow a quote)
    and `\\`.
  - `&"..."` — **StringName** literal.
  - `^"..."` — **NodePath** literal.
- **Escape sequences** in non-raw strings: `\a \b \f \n \r \t \v \' \" \\`, line-continuation
  `\` + newline, and Unicode escapes `\uXXXX` (4 hex digits) and `\UXXXXXX` (6 hex digits).
- Invisible bidirectional control characters inside a string are rejected (must be escaped).
- A standalone string literal used as a statement is permitted as a block "comment" in
  class bodies and at the top of a file.

#### 2.6.3 Boolean / null

`true`, `false`, and `null` are **not** keywords; they are ordinary identifiers resolved to
constant values during analysis. Syntactically they are `IDENTIFIER`.

### 2.7 Annotations token

```ebnf
ANNOTATION = "@", identifier, { ".", identifier } ;
```

The `@` plus following dotted name is lexed as a single `ANNOTATION` token (e.g. `@export`,
`@onready`, `@cafecito.test.timeout`). There is no separate "qualified annotation" token type:
`@export` and `@cafecito.test.timeout` are both single `ANNOTATION` tokens, and the token's
`literal` includes the leading `@` (e.g. `@cafecito.test.timeout` has literal `"@cafecito.test.timeout"`).

A "." only extends the annotation name when it is *immediately* followed by an `id_start`
character; a "." not followed by an `id_start` character (a stray "." or one that begins
something else, e.g. `@export.5`) is left unconsumed for the regular tokenizer and is not part
of the `ANNOTATION` token. There is no dedicated syntax for a dotted name that ends in a
trailing "." — outside of completion, such a "." is simply not absorbed into the token, so
`@cafecito.` lexes as `ANNOTATION("@cafecito")` followed by a separate `.` token, which the
parser (§4.6) is not expecting after an annotation name and rejects. As an editor-tooling
exception, when the tokenizer is running in completion mode and a trailing "." sits
immediately before the completion cursor (e.g. `@cafecito.|`), that "." *is* absorbed into the
`ANNOTATION` token so qualified-name completion can offer the declarations under that
namespace; this cursor-adjacency carve-out is a completion-tooling behavior, not part of the
language's static grammar. Its argument list (if any) and placement are handled by the parser
(§4.6).

### 2.8 Operators and punctuation

Each lexeme below is its own token type.

```
Comparison:     <   <=   >   >=   ==   !=
Logical:        and  or   not  &&   ||   !
Bitwise:        &    |    ~    ^    <<   >>
Arithmetic:     +    -    *    **   /    %
Assignment:     =    +=   -=   *=   **=  /=   %=
                <<=  >>=  &=   |=   ^=
Cast:           as!
Brackets:       (  )   [  ]   {  }
Punctuation:    ,   ;   .   ..   ...   :   $   ->   _
Other:          ?   `    (backtick)
```

Notes:

- `as!` is the `AS_BANG` token, the unchecked bit-reinterpret cast operator. It is lexed as the `as`
  keyword immediately followed by `!` with no intervening whitespace, so `as !` is not this operator.
  The checked cast keyword `as` remains a plain keyword token (§2.5); only the contiguous `as!` form
  is this operator. See §5.5.

- `..` is `PERIOD_PERIOD` (match rest / dictionary rest); `...` is `PERIOD_PERIOD_PERIOD`
  (rest/variadic parameter).
- `_` alone is `UNDERSCORE` (match wildcard).
- `$` is `DOLLAR` (get-node). `%` is both modulo and a unique-name node prefix in get-node
  paths.
- `->` is `FORWARD_ARROW` (function return type).
- `?` is `QUESTION_MARK`: used as the nullable-type suffix (§4.8) and rejected as a standalone
  operator with a hint to use the `if/else` ternary.
- Backtick and VCS conflict markers (`<<<<<<<`, etc.) exist only to produce better error
  messages.

---

## 3. Source file structure (top level)

A `.fs` file is an implicit class (the **head class**). The top-level form is:

```ebnf
program        = { script_annotation | string NEWLINE },
                 [ namespace_decl ], { import_decl },
                 { head_modifier },
                 [ class_name_decl | trait_name_decl | enum_name_decl | tuple_name_decl ],
                 [ extends_decl [ uses_decl ] | uses_decl ],
                 { class_annotation | string NEWLINE },
                 class_body ;
```

Ordering rules enforced by `parse_program` (`fs_parser.cpp`):

1. Script-level annotations (`@tool`, `@icon`, `@static_unload`) and class-level annotations
   may appear first. Class-level annotations are buffered and attached to either the head
   class or the first inner declaration.
2. `namespace` (at most once) must come before any `import`.
3. `import` declarations follow `namespace`.
4. `class_name`/`trait_name`/`enum_name`/`tuple_name` are mutually exclusive and may be
   used at most once. `final`/`abstract` may precede `class_name`/`trait_name`/`extends` to
   mark the head class (a trait cannot be `final`).
5. `extends` may be used once and must come before `uses`.
6. The class body follows.

A file with neither `class_name` nor `extends` is still a valid class implicitly extending
`RefCounted`.

### 3.1 Namespace and import

```ebnf
namespace_decl = "namespace", dotted_name, NEWLINE ;
import_decl    = "import",    dotted_name, NEWLINE ;
dotted_name    = identifier, { ".", identifier } ;
```

### 3.2 Global names

```ebnf
class_name_decl = "class_name", identifier, [ type_parameters ],
                  [ extends_decl [ uses_decl ] | uses_decl ], NEWLINE ;
trait_name_decl = "trait_name", identifier, [ type_parameters ],
                  [ extends_decl [ uses_decl ] | uses_decl ], NEWLINE ;
enum_name_decl = "enum_name", identifier, [ type_parameters ], ":", enum_body ;   (* whole-file enum *)
tuple_name_decl = "tuple_name", identifier,
                  "(", tuple_field, { ",", tuple_field }, [ "," ], ")", NEWLINE ;
```

`extends`/`uses` may appear on the same line as `class_name`/`trait_name`. An `enum_name`
file may contain only the enum declaration, and a `tuple_name` file may contain only its
tuple declaration (§4.4a describes the field list). A `tuple_name` file declares a global
tuple type rather than a script: it has no base class, and the declared name is registered
as a global type (qualified by `namespace` when present) that other files reference by name
or through `import`.

A native (engine) class is reachable under the same conditions as a script class: a flat
(non-namespaced) native class is a global name; a namespaced native class is reachable
through `import` of its namespace or by its fully qualified dotted chain. Native namespaces
are sourced from the engine class registry, not the script registry. A namespaced native
class is not reachable by its bare name unless an explicit global alias is registered.
Native namespacing introduces no new tokens or keywords.

### 3.3 Extends and uses

```ebnf
extends_decl   = "extends",
                 ( STRING [ ".", dotted_name ] | dotted_name ),
                 [ type_arguments ] ;
uses_decl      = "uses", trait_use, { ",", trait_use } ;
trait_use      = dotted_name, [ type_arguments ] ;
type_arguments = "[", type, { ",", type }, "]" ;
```

- `extends` accepts a path string (`extends "res://base.fs"`), an inheritance chain of
  identifiers (`extends A.B.C`), or a path string followed by an inner-class chain. A
  generic base may carry `type_arguments` (`extends List[int]`).
- `uses` mixes in one or more traits, each optionally specialized with type arguments.
- A trait's type arguments are fixed by the first class in an inheritance chain that applies
  it. A subclass may re-apply the same trait only with the same arguments; re-applying it with
  different ones is an error, because a reference typed as the base would otherwise reach a
  body typed for the other arguments. A position either side leaves on an unreified type
  parameter (including `Self`) carries no argument and never conflicts.

---

## 4. Declarations

### 4.1 Class body and members

```ebnf
class_body      = { member } ;

member          = { declaration_modifier },
                  ( variable_decl
                  | constant_decl
                  | signal_decl
                  | function_decl
                  | inner_class_decl
                  | trait_decl
                  | enum_decl
                  | tuple_decl
                  | type_alias_decl
                  | annotation_declaration
                  | conformance_declaration
                  | class_annotation
                  | standalone_annotation
                  | "pass" NEWLINE
                  | string NEWLINE ) ;

declaration_modifier = "final" | "abstract" | "static" | "async" ;
```

Modifiers form a leading run collected before the declaration keyword. Which modifiers are
legal depends on the member (`validate_declaration_modifiers`):

| Member    | final | abstract | static | async | Notes |
|-----------|:-----:|:--------:|:------:|:-----:|-------|
| `var`     |  yes  |    no    |  yes   |  no   | |
| `const`   |  no   |    no    |   no   |  no   | |
| `signal`  |  no   |    no    |   no   |  no   | |
| `func`    |  yes  |   yes    |  yes   |  yes  | `abstract`+`static` only in a trait; `abstract`+`async` allowed |
| `class`   |  yes  |   yes    |   no   |  no   | |
| `trait`   |  no   |   yes    |   no   |  no   | |
| `enum`    |  no   |    no    |   no   |  no   | |
| `tuple`   |  no   |    no    |   no   |  no   | |
| `type`    |  no   |    no    |   no   |  no   | Contextual; see §4.4b |

`final`+`abstract` is always contradictory. `abstract`+`static` is only allowed inside a
trait.

### 4.2 Inner classes and traits

```ebnf
inner_class_decl = "class", identifier, [ type_parameters ],
                   [ "extends", ... ], [ "uses", ... ], ":", block_or_inline ;
trait_decl       = "trait", identifier, [ type_parameters ],
                   [ "extends", ... ], [ "uses", ... ], ":", block_or_inline ;
```

`extends`/`uses` may appear on the declaration line and/or as the first lines inside the
indented body. `block_or_inline` is either a single inline statement/member or a
`NEWLINE INDENT ... DEDENT` block.

### 4.3 Type parameters (generics)

```ebnf
type_parameters = "[", type_parameter, { ",", type_parameter }, [ "," ], "]" ;
type_parameter  = identifier, [ ":", type ] ;   (* optional upper bound *)
```

Generics appear on classes (`class Box[T]`, `class_name Pair[K, V]`), traits
(`trait Container[T]`), functions (`func swap[T](...)`), and named tagged unions
(`enum Result[T, E]`, `enum_name Tree[T]`; see §4.4). A bound constrains the
parameter (`[T: Resource]`). A trailing comma is allowed.

A bound is an ordinary type, so it may be a type union or an alias naming one
(`[T: int | uint]`, `[T: Number]`). Satisfaction against a union bound is asymmetric:
a **concrete** argument satisfies it when it satisfies **at least one** alternative; a
**union** argument (which inference produces from a union-typed value) satisfies it only
when **every** alternative does; and a **type-parameter** argument satisfies it only when
its own bound proves it does, so an unbounded parameter never satisfies a concrete bound.

A class's `static var` may not be typed by a class type parameter, directly or nested
(`static var value: T`, `static var items: Array[T]`). Static storage is one slot per
declaring class and specializing a class does not create a distinct one, so such a slot
would be seen as a different static type through every specialization while holding a
single value. A generic trait may declare one (`trait Slotted[T]: static var slot: T`) —
each implementer flattens it into a slot of its own — but the implementer must fix the
argument concretely (`uses Slotted[int]`) rather than forward its own parameter.

### 4.4 Variables, constants, signals, enums

```ebnf
variable_decl   = "var", identifier,
                  [ ":", ( type | (* inferred *) ) ],
                  [ "=", expression ],
                  [ property_clause ],
                  NEWLINE ;

constant_decl   = "const", identifier, [ ":", [ type ] ], "=", expression, NEWLINE ;

signal_decl     = "signal", identifier,
                  [ "(", [ parameter, { ",", parameter }, [ "," ] ], ")" ],
                  NEWLINE ;

enum_decl       = "enum", [ identifier, [ type_parameters ] ], ":", enum_body ;
enum_body       = NEWLINE, INDENT,
                  ( "pass", NEWLINE
                  | enum_value_line, { enum_value_line }, { enum_function_decl }
                  | enum_function_decl, { enum_function_decl } ),
                  DEDENT ;
enum_value_line = identifier, [ enum_case_payload ], [ "=", expression ], NEWLINE ;
enum_case_payload = "(", enum_payload_field, { ",", enum_payload_field }, [ "," ], ")" ;
enum_payload_field = identifier, ":", type ;   (* names are required on payload fields *)
enum_function_decl = { function_annotation }, { enum_function_modifier }, function_decl ;
enum_function_modifier = "static" | "async" ;
function_annotation = ANNOTATION, [ "(", [ annotation_args ], ")" ], [ NEWLINE ] ;
```

- A `var`'s type may be written explicitly (`var x: int = ...`), **inferred** from the
  initializer when a `:` is immediately followed by `=` (`var x := value`), or omitted
  entirely (`var x = value`). The same applies to `const` and parameters.
- Signal parameters may have a type annotation but **not** a default value.
- An **unnamed** enum (`enum:`) injects its values as constants into the enclosing
  class; a **named** enum (`enum Dir:`) defines an enum type. Only named enums may
  contain functions.
- Named enum declarations use the constant annotation target. This currently allows the
  built-in `@keep_name` annotation; enum values remain unsupported annotation targets.
- Every enum value in a plain (non-tagged-union) enum must provide an explicit integer
  expression (`NAME = expression`). Values do not receive implicit numbers, and enum
  members are separated by newlines rather than commas. Commas remain valid inside an
  enum value expression.
- A case may declare a **payload**: a parenthesized, comma-separated field list
  (`Move(x: int, y: int)`), reusing the tuple field form (§4.4a) except every payload
  field must be named — a bare (positional) payload field is a parse error, as is a
  duplicate field name within one case's payload. A trailing comma is allowed once the
  payload has at least one field.
- If **any** case in an enum declares a payload, the whole enum is a **tagged union**
  and `= expression` is a parse error on *every* case, payload-bearing or not — case
  tags are ordinal by declaration order (0-based), not explicit values. Cases with and
  without a payload may be freely mixed within one tagged union
  (`enum Message: Quit \n Move(x: int, y: int)`). Whether an enum is a tagged union is
  only known once its whole body has been parsed, so this rule is validated after the
  body, not case-by-case during parsing.
- A tagged union must be a **named** enum (`enum Name:` or `enum_name Name:`), because
  its cases are only reachable as `Name.Case`; payload cases in an unnamed enum are a
  semantic error.
- A tagged union's values are `[tag, payload...]` read-only arrays, not integers, so a
  case value never participates in integer contexts (arithmetic, bitwise operators,
  `int` conversions, or assignment to an int-backed enum). A payload-less case is a
  value on its own; a payload-carrying case is only usable when constructed with its
  declared field types (`Name.Case(argument, ...)`), and its payload fields are not
  reachable directly on a value of the union type.
- A tagged union's payload field types **may reference the union itself**, directly
  (`Link(next: Chain)`) or indirectly through a typed collection
  (`Branch(children: Array[Tree])`, `Section(entries: Dictionary[String, Config])`). This
  is valid in both the inner `enum` and whole-file `enum_name` forms. Recursion terminates
  at runtime because a value is finite: a payload slot holds another `[tag, payload...]`
  read-only array. Int-backed enums are unaffected — their `= expression` values still
  cannot reference the enum being declared.
- A named enum may declare **type parameters** (§4.3) between its name and the `:`
  (`enum Result[T, E: Resource]:`, `enum_name Tree[T]:`), making it a generic tagged
  union whose payload field types may reference those parameters. Two declaration
  shapes are rejected: type parameters on an unnamed enum (`enum[T]:`), and type
  parameters on an enum whose completed body declares no payload-bearing case (an
  integer-backed enum). Like the tagged-union rules above, the second is validated
  after the whole body has been parsed.
- Inside a generic tagged union's own declaration, the union names itself either bare
  (`Link(next: Tree)`) or with its exact parameter vector in declaration order
  (`Explicit(children: Array[Tree[T]])`); both spell the same open self type. A generic
  tagged union has no bare form anywhere else, and its type parameters shadow same-named
  class parameters while being shadowed in turn by an enum function's own parameters.
- Everywhere else a generic tagged union is named by **applying a full type-argument
  vector**, using the same bracketed syntax as a generic class, in a type position
  (`func take(value: Result[int, String])`) and in an expression position
  (`var handle = Result[int, String]`). The argument count must equal the declared
  parameter count, each argument must satisfy its parameter's bound, and a union that
  declares no parameters accepts no arguments at all. Type arguments are invariant, so
  two applications of one union that differ in any argument are unrelated types. A
  decorated spelling of the declaration's own parameter (`Slot[T?]`) is an ordinary
  application rather than the open self type.
- Enum values must appear before enum functions. A functions-only named enum is valid.
  Enum functions reuse ordinary function signatures and bodies, allow `static` and
  `async`, and reject `abstract` and `final`. Variables, constants, signals, nested
  classes/enums/traits, and conformances are not valid enum-body declarations. Enum
  functions remain supported on a tagged union.
- The builtin `Result[T, E]` union ships at `foundry://builtin/result.fs` with stable
  case order: `Ok` is tag 0 and `Err` is tag 1. It follows the same specialization,
  construction, match, and `is` rules as user-declared generic tagged unions.
- An empty enum uses `pass` as its only body statement (`enum Empty:` followed by
  an indented `pass`).
- `enum_name` (§3.2) declares a file-level named enum using the same indented body,
  including payload cases and type parameters.

#### Property accessors

```ebnf
property_clause     = ":", ( property_block | inline_property ) ;
property_block      = NEWLINE, INDENT, accessor, { accessor }, DEDENT ;
inline_property     = accessor_setget, [ ",", accessor_setget ] ;

accessor            = getter_inline | setter_inline ;
getter_inline       = "get", [ "(", ")" ], ":", block ;
setter_inline       = "set", "(", identifier, ")", ":", block ;

accessor_setget     = "get", "=", identifier
                    | "set", "=", identifier ;
```

A property combines a backing variable with a getter and/or setter. Two styles exist:
inline bodies (`get:` / `set(value):` with indented blocks) and pointer style
(`get = method`, `set = method`). `get` and `set` may appear in either order. The style is
chosen by whether `=` follows the accessor name.

### 4.4a Tuple declarations

```ebnf
tuple_decl      = "tuple", identifier, "(", tuple_field, { ",", tuple_field }, [ "," ], ")",
                   NEWLINE ;
tuple_field     = [ identifier, ":" ], type ;   (* a bare type is a positional field *)
```

- Declares a named, fixed-arity tuple type at class-body level (single-line; no indented
  body). Each field is either **named** (`x: float`) or **positional** (a bare type, e.g.
  `int`); the two forms may be mixed freely in one declaration
  (`tuple Player(name: String, int, bool)`).
- Arity must be at least 2: a tuple declaration with fewer than two fields is a parse
  error. A trailing comma is allowed once arity is >= 2.
- A named field's identifier must be unique within the declaration; a duplicate name is a
  parse error.
- `tuple` is a fully reserved keyword token. The whole-file form uses the separate
  `tuple_name` keyword (§3.2), which keeps a head-position `tuple` declaration an ordinary
  class-body member.
- Named tuple declarations use the constant annotation target, matching named enums; this
  currently allows the built-in `@keep_name` annotation.

### 4.4b Type alias declarations

```ebnf
type_alias_decl = "type", identifier, "=", type, NEWLINE ;
```

`type` is a **contextual keyword**, not a reserved word: it is lexed as an ordinary identifier and
only introduces an alias where a member declaration is valid and the next token is an identifier.
The decision is exactly that two-token lookahead (`type` `IDENTIFIER`), which steals nothing, since
no declaration position admits an expression statement. `var type = 5`, `type = 5`, `type.value`,
and `type(x)` all remain ordinary uses of the identifier.

- Aliases may be declared at **file scope and in a class body only**. One written in a function
  body is a parse error
  (`Type alias declarations are only allowed at file scope or in a class body.`).
- An alias takes **no type parameters**: `type Pair[T] = ...` is a parse error
  (`A type alias cannot declare type parameters.`). A *member* may still be a generic
  specialization (`type Numbers = Array[int]`).
- An alias declares no runtime member, so **no annotation may be applied to it**
  (`Annotation "@export" cannot be applied to a type alias.`).
- An alias is **transparent**: it names a type, it does not create one. The name is not an
  expression, not a constructor, and not an `extends`/`uses` target.
- An alias shares the **member name space** of the class body it is declared in, so it collides
  with a variable, constant, function, or nested type of the same name in either direction
  (`Type alias "X" has the same name as a previously declared constant.`).
- An alias is **file-local** in this version: it is not a global name and is not reachable through
  `import`/`namespace`.
- An alias name may not be a **built-in type or native class name**, which would win the type lookup
  and leave the alias unreachable (`Type alias "int" hides a built-in type.`,
  `Type alias "Label" hides a native class.`).
- A **type handle** cannot represent a union: `Type[A | B]` is an error, since a handle names exactly
  one type at runtime.
- Alias visibility is **lexical, not nominal**: the name belongs to the body that declares it and to
  the bodies nested inside it. Extending a class does not carry its aliases along, in the same file
  or across files
  (`Type alias "X" is not in scope here. A type alias is visible only inside the file and the body
  that declare it, so it is neither inherited nor imported.`). A retroactive conformance witness is
  written in the conformance's own file, so that file's aliases are in scope in the witness body even
  though the target type is declared elsewhere.

### 4.5 Functions and parameters

```ebnf
function_decl   = "func", identifier, [ type_parameters ],
                  "(", [ parameter_list ], ")",
                  [ "->", return_type ],
                  ( ":", block
                  | (* only when the "abstract" modifier is present *) NEWLINE ) ;

parameter_list  = param_item, { ",", param_item }, [ "," ] ;
param_item      = [ "..." ], parameter_annotation*, parameter ; (* "..." marks the rest parameter *)
parameter       = identifier, [ ":", ( type | (* inferred *) ) ], [ "=", expression ] ;
parameter_annotation = ANNOTATION, [ "(", [ annotation_args ], ")" ] ;

return_type     = type | "void" ;
```

Rules (`parse_function_signature`):

- At most one **rest** parameter (`...name`), which must be last and cannot have a default.
- A rest parameter describes the **collected array**, so its annotation must resolve to `Array`
  or `Array[T]`; any other type is rejected with *"The rest parameter type must be `Array`, but
  `X` is specified."* The element-oriented spelling `...name: T` is not accepted for functions
  and lambdas.
- With `Array[T]`, `T` is the expected type of every **surplus** call argument: arguments past
  the fixed parameter list are checked against `T` under the same conversion, nullability,
  strict-dynamic and diagnostic rules as a fixed parameter, and the diagnostic names `T` rather
  than `Array[T]`. Inside the body the parameter has exactly the declared `Array[T]` type, and a
  call that supplies no surplus arguments still receives a typed empty array whenever `T` is
  representable as a typed container (nullable and other erased element forms follow the same
  typed-container erasure as an ordinary `Array[T]` value).
- `...name`, `...name: Array`, and `...name: Array[Variant]` are gradual: surplus arguments are
  unconstrained, and such a tail is indistinguishable from a native untyped vararg.
- The rest parameter can never be passed **by name**: named call arguments target only the fixed
  parameters that precede it (*"The rest parameter `name` cannot be passed by name."*). Surplus
  arguments are always positional, and no positional argument may follow a named one.
- The rest element type is **contravariant** across an override, an abstract requirement, a trait
  witness, a callable assignment, and a signal connection: the implementing side may accept the same
  element type, a broader one, or a gradual tail, but never a narrower one.
- A **generic** method infers its type parameters from every rest argument, so
  `func collect[T](...values: Array[T])` solves `T` with no fixed parameter, and an explicit type
  application (`collect[int]()`) supports an empty call. Because method type arguments are not
  reified in the call frame, a rest Array whose element depends on a method type parameter is
  **runtime-erased** — statically it is `Array[T]`, but the packed value carries no element
  metadata. A rest element that does not depend on a method type parameter stays reified even
  inside a generic method.
- Parameters with defaults must follow parameters without defaults (except the rest
  parameter).
- `void` is allowed only as a return type.
- An **abstract** function has no body: the signature is terminated by `NEWLINE` instead of
  `:` + block. The `abstract` modifier is **required** for that form — a body-less `func` is
  an error even inside a trait, where every requirement must be written `abstract func`. No
  context exempts a function from this; the analyzer rejects a body-less `func` with *"A
  function must either have a `:` followed by a body, or be marked as `abstract`."*
- `async func` (the contextual `async` modifier before `func`) declares a coroutine; `await`
  in a body also makes a function a coroutine.
- The special static constructor `_static_init` must be `static` and parameterless.

### 4.6 Annotations (usage)

```ebnf
class_annotation      = ANNOTATION, [ "(", [ annotation_args ], ")" ], [ NEWLINE ] ;
standalone_annotation = ANNOTATION, [ "(", [ annotation_args ], ")" ], NEWLINE ;

annotation_args       = annotation_arg, { ",", annotation_arg }, [ "," ] ;
annotation_arg        = [ identifier, "=" ], expression ;   (* named args: custom + @autoload *)
```

- An annotation precedes the declaration (class/var/const/signal/func) or statement it
  applies to, or a method/signal parameter name in a parameter list. The newline after an
  annotation is optional so it may sit on the same line as its target.
- Named arguments (`name = value`) are accepted only for **custom** annotations and the
  built-in `@autoload`; all other built-ins are positional.
- Placement is validated against each annotation's allowed targets (script-level,
  class-level, statement, standalone). See §6 for the built-in list.

### 4.7 Custom annotation declarations

```ebnf
annotation_declaration = "annotation", identifier,
                         [ "(", [ annotation_decl_params ], ")" ],
                         "targets", target_list, NEWLINE ;

annotation_decl_params = adp_item, { ",", adp_item }, [ "," ] ;
adp_item               = [ "..." ], parameter ;   (* variadic last param allowed *)

target_list            = target_name, { ",", target_name } ;
target_name            = "CLASS" | "METHOD" | "VARIABLE" | "SIGNAL" | "CONSTANT" | "PARAMETER" ;
```

`annotation` is contextual: it only starts a declaration at the **root** of a script where a
declaration is valid; elsewhere it is an ordinary identifier. The declaration defines a
reusable custom annotation with typed parameters (defaults must be constant) and a set of
valid targets.

### 4.8 Retroactive conformance (`extend`)

```ebnf
conformance_declaration = "extend", conformance_target,
                          "uses", trait_use, { ",", trait_use },
                          ":", conformance_body ;
conformance_target = dotted_name ;          (* unspecialized; NO type_arguments *)
conformance_body = NEWLINE, INDENT, conformance_member, { conformance_member }, DEDENT
                 | conformance_member ;
conformance_member = { "static" | "async" }, function_decl ;
```

`extend` is contextual and root-only, exactly like `annotation` (§4.7): it only starts a
conformance at the **root** of a script where a declaration is valid; elsewhere it is an
ordinary identifier (and it is distinct from the `extends` keyword token, §2.5). A
conformance declares that an existing type (which the script need not own) retroactively
conforms to one or more traits, supplying the required methods externally as witnesses.

The target must be **unspecialized**: writing type arguments (e.g. `extend Box[int] uses
T`) is a parse error, because a conformance applies to **all** specializations of a generic
base. The `uses` clause reuses `trait_use` from §3.3. The body contains **only** function /
accessor members; `var`, `const`, `signal`, inner `class`/`trait`, and `enum` members are
rejected. Witness methods may carry the `static`/`async` modifiers. Inside the witnesses,
`self` is typed as the target. For builtin value-type targets (`extend int uses ...`), witness
`self` is a copy for scalars and strings (mutations do not propagate to the caller) but shares
storage for `Array` and `Dictionary` (mutations through `self` are visible to the caller).

**Witness scope.** A witness signature and body resolve names in a **dual scope**: first the
target's own member/type scope (its members, its base chain, its inner types, `Self`, and the
target's existing lexical outer chain), and then — only for names the target scope did not
supply — the lexical **type** scope of the file that declares the `extend`. The target always
wins on a collision, so an unqualified name that exists on both sides means the target's. This
rule is uniform across every supported target kind: Foundry Script classes (same-file or
foreign, root or inner), native engine classes, and builtin value types.

The declaration-site half exposes only what an ordinary lexical outer class contributes as
*types*: inner classes and traits, enums, named tuples, and constants that denote a type (such
as a `preload`ed script or a class alias), together with their qualified nested types. The
declaring file's variables, functions, signals, and plain value constants do **not** become
members of the target and are not reachable from a witness. Declaration order does not matter:
a helper type declared after the `extend` is as visible as one declared before it. A name that
neither scope supplies remains an ordinary "could not find type" error.

**Coherence.** The analyzer rejects duplicate `(target, trait)` conformances (same file or
cross-file). It also rejects **witness method-name collisions** on the same target: runtime
witness dispatch keys on `(target alias, method name)` only, so two conformances on the same
target that each supply a witness with the same name — even for different traits — are an
error. Requirements satisfied by the target's own existing methods without a supplied witness
do not participate in this check.

**Inheritance-chain shadowing** is legal: conforming the same trait on a base type and on a
derived type (native engine classes or FS classes) is allowed. Witness dispatch walks the
instance's class chain most-derived-first and uses the first matching witness; `is`/`as`
against the trait succeed if any level in the chain declares the conformance. Colliding witness
names on different levels of the chain (e.g. a base conformance to trait A supplies `foo()` and
a derived conformance to trait B supplies `foo()`) follow this shadowing rule and are not
rejected.

A conformance's type arguments answer the same membership question a declared `uses` answers,
so a conformance may not record arguments that differ from a binding already present on the
target's class chain. Recording different ones would make the value satisfy the trait at two
arguments at once, since either evidence source is enough on its own. As with `uses`, a
position left on an unreified type parameter carries no argument and never conflicts.

**Reach.** A conformance takes effect for the files that **load** its declaring file, not for
the whole project. A file loads it when it:

1. declares it itself;
2. `preload`s (or `extends`) the declaring file, directly or transitively; or
3. is in the same **named** namespace as the declaring file, or `import`s that namespace
   (§4.2). A file's own namespace counts as implicitly imported, exactly as it does for short
   class names and custom annotations.

The global namespace is excluded from rule 3: it has no `import` syntax, so being in it is not
a choice a file makes, and treating it as implicitly imported would make every script in a
project load every other one that happens to declare a conformance. A conformance declared in a
file with no `namespace` declaration is reached only by rules 1 and 2.

Reach is a load edge, not just a name-resolution one: a conformance only exists at run time
once its declaring script has been compiled, so a file that reaches a conformance through
rule 3 keeps that file loaded even though nothing in its emitted code names it. A call to a
witness supplied by a conformance the file does **not** reach is an analysis error naming the
declaring file — never an accepted call that misses at run time.

---

## 5. Expressions (Pratt parser)

This is the core of the front-end and the part most relevant to a Pratt re-implementation.
The reference parser is a single-token-lookahead **Pratt / precedence-climbing** parser:
`parse_precedence(min_precedence)` drives a table (`get_rule`) that maps each token type to
an optional **prefix** rule, an optional **infix** rule, and the infix **precedence**.

### 5.1 Algorithm

```
parse_precedence(min_prec, can_assign):
    token = current
    prefix = rule(token).prefix
    if prefix == null: return null            (* not the start of an expression *)
    advance()
    left = prefix(can_assign)
    while min_prec <= rule(current).precedence:
        token = advance()
        infix = rule(token).infix
        left = infix(left, can_assign)
    return left
```

- A full expression is parsed with `min_prec = PREC_ASSIGNMENT`.
- `can_assign` controls whether an assignment infix is permitted at this position
  (assignment is only allowed as a statement-level expression, not nested).
- Binary operators are **left-associative**: a binary operator parses its right operand with
  `precedence + 1`. This uniform rule applies even to `**` (power), so `2 ** 3 ** 2` parses
  as `(2 ** 3) ** 2` — left-associative, unlike mathematical convention. The exceptions to
  left-associativity are the prefix unary operators (`-` `+` `~` `not` `!`, which recurse at
  their own precedence and are effectively right-associative) and the ternary (right-
  associative, §5.5).

### 5.2 Precedence levels

From lowest to highest (`FSParser::Precedence`). Higher binds tighter.

| # | Level                       | Operators / forms (infix unless noted)             | Assoc. |
|---|-----------------------------|----------------------------------------------------|--------|
| 1 | `PREC_ASSIGNMENT`           | `=` `+=` `-=` `*=` `**=` `/=` `%=` `<<=` `>>=` `&=` `\|=` `^=` | right (stmt-level) |
| 2 | `PREC_CAST`                 | `as`, `as!` (and the invalid `?` handler)          | left |
| 3 | `PREC_TERNARY`              | `value if cond else value`                         | right |
| 4 | `PREC_LOGIC_OR`             | `or` `\|\|`                                        | left |
| 5 | `PREC_LOGIC_AND`            | `and` `&&`                                         | left |
| 6 | `PREC_LOGIC_NOT`            | `not` `!` (prefix)                                 | right |
| 7 | `PREC_CONTENT_TEST`         | `in`, `not in`                                     | left |
| 8 | `PREC_COMPARISON`           | `<` `<=` `>` `>=` `==` `!=`                         | left |
| 9 | `PREC_BIT_OR`               | `\|`                                               | left |
| 10| `PREC_BIT_XOR`              | `^`                                                | left |
| 11| `PREC_BIT_AND`              | `&`                                                | left |
| 12| `PREC_BIT_SHIFT`            | `<<` `>>`                                          | left |
| 13| `PREC_ADDITION_SUBTRACTION` | `+` `-`                                            | left |
| 14| `PREC_FACTOR`               | `*` `/` `%`                                        | left |
| 15| `PREC_SIGN`                 | unary `+` `-` (prefix)                             | right |
| 16| `PREC_BIT_NOT`              | unary `~` (prefix)                                 | right |
| 17| `PREC_POWER`                | `**`                                               | left  |
| 18| `PREC_TYPE_TEST`            | `is`, `is not`                                     | left |
| 19| `PREC_AWAIT`                | `await` (prefix)                                   | right |
| 20| `PREC_CALL`                 | `(` … `)` call                                     | left |
| 21| `PREC_ATTRIBUTE`            | `.` attribute access                               | left |
| 22| `PREC_SUBSCRIPT`            | `[` … `]` subscript / type args                    | left |
| 23| `PREC_PRIMARY`              | literals, identifiers, grouping, primaries         | —    |

> Implementation detail: `**` (`STAR_STAR`) sits at `PREC_POWER` and binds tighter than
> unary sign, so `-2 ** 2` parses as `-(2 ** 2)`. Because every binary operator (including
> `**`) recurses with `precedence + 1`, `**` is **left**-associative
> (`2 ** 3 ** 2` → `(2 ** 3) ** 2`).

### 5.3 Prefix (null-denotation) forms

```ebnf
primary =
      LITERAL
    | identifier
    | "self"
    | "PI" | "TAU" | "INF" | "NAN"
    | unary_op
    | "(", expression, ")"                          (* grouping *)
    | tuple_literal
    | array_literal
    | dictionary_literal
    | lambda
    | "await", expression
    | "preload", "(", expression, [ "," ], ")"
    | get_node
    | contextual_enum_case
    | "super", super_tail                            (* only as call base *)
    ;

contextual_enum_case = ".", identifier ;
                                                     (* names a case of a tagged union without
                                                        naming the union; the payload form
                                                        `.Case(a, b)` is this primary followed by
                                                        the ordinary call suffix of section 5.4 *)

tuple_literal = "(", expression, ",", expression, { ",", expression }, [ "," ], ")" ;
                                                      (* arity >= 2; see §9 for `(a)` vs `(a,)` *)

unary_op = ( "-" | "+" | "~" | "not" | "!" ), expression ;
```

The token → prefix-rule mapping (`get_rule`):

| Token              | Prefix meaning                                  |
|--------------------|-------------------------------------------------|
| `IDENTIFIER`       | identifier reference                            |
| `LITERAL`          | literal value                                   |
| `SELF`             | `self`                                          |
| `CONST_PI/TAU/INF/NAN` | numeric constant                            |
| `MINUS`/`PLUS`     | unary sign (`PREC_SIGN`)                         |
| `TILDE`            | bitwise complement (`PREC_BIT_NOT`)             |
| `NOT`/`BANG`       | logical not (`PREC_LOGIC_NOT`)                  |
| `PARENTHESIS_OPEN` | grouping, or a tuple literal if a `,` follows the first element |
| `BRACKET_OPEN`     | array literal                                   |
| `BRACE_OPEN`       | dictionary literal                              |
| `FUNC`             | lambda                                          |
| `AWAIT`            | await expression                                |
| `PRELOAD`          | preload expression                              |
| `DOLLAR`           | get-node (`$`)                                  |
| `PERCENT`          | get-node unique-name shorthand (`%`)            |
| `PERIOD`           | contextual tagged-union case (`.Case`)          |
| `SUPER`            | super call/access                               |
| `YIELD`            | error (removed)                                 |

`PERIOD` is the one token with both a prefix and an infix rule: in prefix position it starts a
contextual tagged-union case, in infix position it is attribute access. The two never compete,
because a prefix rule is only consulted where an expression may start. Note also that `.` followed
by a digit never reaches the prefix rule: in expression-start position the tokenizer produces a
single float `LITERAL` (`.5` is `0.5`), and after a value it produces the infix `PERIOD` of a tuple
index (§2.6.1).

A contextual case is accepted syntactically wherever an expression is, but it is only *valid* where
the surrounding consumer supplies a complete specialized tagged-union expected type: a variable or
constant initializer with a declared type, a `return`, an assignment, a call argument (including a
variadic slot and a defaulted parameter), an element of a typed array literal, a key or a value of a
typed dictionary literal, the operand of an `as` cast, or a branch of a conditional expression whose
own expected type is a union. Inferred, untyped, and `Variant` targets are rejected. This is a
semantic requirement, not a syntactic one, so a re-implementation resolves it after parsing.

In a `match` case pattern (§6.1) and on the right of `is` (§5.5), the union is supplied by the
subject being matched or tested rather than by an expected type.

### 5.4 Infix (left-denotation) forms

| Token(s)                              | Infix rule          | Precedence            |
|---------------------------------------|---------------------|-----------------------|
| `<` `<=` `>` `>=` `==` `!=`           | binary comparison   | `PREC_COMPARISON`     |
| `and` `&&`                            | logical and         | `PREC_LOGIC_AND`      |
| `or` `\|\|`                           | logical or          | `PREC_LOGIC_OR`       |
| `not`                                 | `not in` content test | `PREC_CONTENT_TEST` |
| `in`                                  | content test        | `PREC_CONTENT_TEST`   |
| `&`                                   | bit and             | `PREC_BIT_AND`        |
| `\|`                                  | bit or              | `PREC_BIT_OR`         |
| `^`                                   | bit xor             | `PREC_BIT_XOR`        |
| `<<` `>>`                             | bit shift           | `PREC_BIT_SHIFT`      |
| `+` `-`                               | add/subtract        | `PREC_ADDITION_SUBTRACTION` |
| `*` `/` `%`                           | multiply/divide/mod | `PREC_FACTOR`         |
| `**`                                  | power               | `PREC_POWER`          |
| `=` and compound assignments          | assignment          | `PREC_ASSIGNMENT`     |
| `if`                                  | ternary             | `PREC_TERNARY`        |
| `as`                                  | checked cast        | `PREC_CAST`           |
| `as!`                                 | bit reinterpret     | `PREC_CAST`           |
| `is`                                  | type test           | `PREC_TYPE_TEST`      |
| `(`                                   | call                | `PREC_CALL`           |
| `.`                                   | attribute access    | `PREC_ATTRIBUTE`      |
| `[`                                   | subscript/type args | `PREC_SUBSCRIPT`      |
| `?`                                   | invalid (error)     | `PREC_CAST`           |

### 5.5 Specific expression forms

#### Ternary

```ebnf
ternary = expression, "if", expression, "else", expression ;
```

Parsed as an infix on `if`: the already-parsed left operand becomes the *true* branch, then
the condition, then `else`, then the *false* branch.

#### Assignment

```ebnf
assignment = assign_target, assign_op, expression ;
assign_target = identifier | attribute_access | subscript ;
assign_op  = "=" | "+=" | "-=" | "*=" | "**=" | "/=" | "%="
           | "<<=" | ">>=" | "&=" | "|=" | "^=" ;
```

Assignment is parsed as an expression for convenience but is only valid as a statement; the
target must be an identifier, attribute, or subscript. It is rejected inside other
expressions (`can_assign` is false there).

#### Cast and type test

```ebnf
cast          = expression, ( "as" | "as!" ), type ;
type_test     = expression, "is", [ "not" ], type_test_type, [ case_bind_list ] ;
type_test_type = type | contextual_enum_case ;
case_bind_list = "(", case_bind, { ",", case_bind }, ")" ;
case_bind     = identifier | "_" ;
```

`as` is the checked cast. `as!` is the unchecked **bit-reinterpret** operator, tokenized as the
single compound token `AS_BANG` (the `as` keyword immediately followed by `!`, with no intervening
whitespace). It is restricted to a crossing between the source-nameable integer types of **equal
width** — `int` ↔ `uint` (both 32-bit) and `long` ↔ `ulong` (both 64-bit). Unlike `as`, it never
range-checks: it takes the operand's N-bit pattern, masks it to the target width, and lays that
pattern on the target carrier. Every other use is an error, so `as!` cannot become a general
unchecked-cast escape hatch:

- The target must be `int`, `uint`, `long`, or `ulong`; any other target type is rejected.
- The operand must be an integer, and its width must equal the target width, so a genuine narrowing
  such as `some_ulong as! uint` stays an error. An unpinned integer (an unsuffixed literal, or a value
  whose width was never declared) counts as 64-bit — its carrier's full width — so it reinterprets
  only into a 64-bit target; reaching a 32-bit target requires an explicitly 32-bit operand (an
  `int`/`uint` value, or an `as int`/`as uint` narrowing applied first).
- Constant folding, the reinterpret opcode, and the analyzer all produce the identical masked pattern.

A signed shift is a checked multiply-by-2ⁿ, so it overflows before it can set the sign bit
(`(255 as long) << 56` is out of range). The idiom is to shift on the unsigned carrier and then
reinterpret: `(255 as ulong) << 56UL as! long` assembles the high byte and reads it back as signed
(the shift count shares the left operand's carrier, and `as!` binds looser than `<<`).

`x is not int` is parsed as `not (x is int)`. The negation node records that it came from this
sugar, so the formatter reprints the source form `x is not int`; an author-written `not (x is int)`
keeps its own spelling. Nothing in the semantics distinguishes the two.

A `case_bind_list` may only follow a type that names a tagged-union case, `msg is Message.Move(x, y)`;
a case name is accepted in this position only, never as a type annotation. The list is rejected
after `is not`, must be non-empty, must not repeat a bind name, and must have exactly one entry per
payload field of the case (`_` skips a field without declaring a name). A bind-carrying test is only
valid as the condition of `if`, `elif`, `while`, or `assert`, either directly or as an operand of
`and` within that condition; the binds become locals of the guarded suite (of the enclosing suite for
`assert`). Without a bind list, `msg is Message.Move` is an ordinary boolean expression that tests
the case tag, and `msg is Message` tests membership in the enum.

The tested type may also be the contextual case shorthand (`msg is .Move(x, y)`, `opt is .None`),
which names a case of the union the tested operand already has. It is only valid where that operand's
type is a complete tagged-union specialization; the shorthand names exactly one case and therefore
takes neither a type-argument list nor a longer dotted chain.

Because the binds of an `assert` outlive the assertion, a bind-carrying `assert` condition is still
evaluated in builds where assertions are stripped; only the failure check is removed.

##### Generic type tests and casts

Type arguments are reified and invariant, and `is`/`as` observe them. `as` succeeds exactly when the
corresponding `is` holds: on success it yields the original value unchanged, and on failure it yields
`null`. Both forms are shallow — they compare runtime type descriptors and never read members or walk
container contents, so an empty `Crate[int]` and a full one answer alike.

- A raw target asks only the nominal question. `Crate[int].new() is Crate` is true, and so is
  `Crate.new() is Crate`.
- A specialized target additionally requires runtime evidence: the value must be nominally a `Crate`,
  and its effective arguments for that `Crate` must be known and equal to the target's, argument by
  argument, recursively, in order. Arguments are invariant, so `Crate[int]` and `Crate[String]` are
  unrelated in both directions.
- Missing evidence fails rather than being accepted gradually. `Crate.new() is Crate[int]` is false,
  and so is a raw `Crate` class handle against `Type[Crate[int]]`. An explicitly written `Variant`
  argument is evidence for `Variant` and is not the same as an absent argument.
- Inheritance projects the value's specialization onto the tested base through the declared bindings,
  not by argument position. `class IntCrate extends Crate[int]` satisfies `Crate[int]`, and
  `class Derived[U] extends Crate[U]` specialized as `Derived[int]` satisfies `Crate[int]`.
- Instance tests (`value is Crate[int]`) and class-handle tests (`handle is Type[Crate[int]]`) apply
  the same nominal and argument rules to their respective runtime values.
- `Self` resolves to the frame's exact receiver before the rules above apply, at every nesting depth,
  so a method reached through `Crate[int]` accepts a `Crate[int]` for `is Self` and rejects a
  `Crate[String]` and a raw `Crate`.
- A generic **trait** target follows the same two questions. The nominal one is conformance rather
  than inheritance; the argument one reads the arguments the value's class conformed with. So with
  `trait Holder[T]` and `class StringHolder: uses Holder[String]`, a `StringHolder` satisfies
  `Holder` and `Holder[String]` but not `Holder[int]`. Conformance arguments project the same way
  inheritance does: a subclass of a conforming class, a supertrait reached through
  `trait Relayed[U]: uses Holder[U]`, and a generic implementer `class Forwarder[W]: uses Holder[W]`
  all answer from the argument that reaches the tested trait. A raw `Forwarder` proves nothing and
  fails a specialized target.
- A **retroactive** conformance (`extend Target uses Trait[arg]`) records the arguments it declared,
  so a value that conforms solely through it answers a specialized target from them: an exact match
  succeeds, a mismatch fails, and a conformance that supplied no arguments at all satisfies only the
  raw target. A supertrait reached through the conformance answers from the argument substituted into
  it, so `trait Sub[T]: uses Super[T]` with `extend Target uses Sub[int]` satisfies `Super[int]` and
  fails `Super[String]`. A conformed engine class answers for its subclasses, and a conformed builtin
  answers for its values, by the same rules.
- `null` is never an instance or a class handle, so it fails every such test.

##### Specialized types at a store boundary

A store asks a weaker question than `is` does, and the two answers differ for a value whose type
arguments were never recorded.

- A specialized type in a **declaration** — a variable, constant, parameter, member, return type,
  typed-container element, or tuple element — checks the value's nominal identity unconditionally and
  its type arguments only against the evidence the value carries. A value whose arguments for that
  class or trait are **absent** is accepted; a value whose arguments are **known and different** is
  rejected.
- This is the gradual rule, and it holds whether the source is a `Variant` or an ordinary
  argument-erased static type: `var slot: Crate[int] = Crate.new()` is accepted, and so is the same
  store reached through a `Variant`.
- `is` and `as` are therefore **strictly stronger** than the declaration. `value is Crate[int]` is
  false for a value the declaration `var slot: Crate[int] = value` accepts. A guarded test is the way
  to demand the arguments.
- The rule is the same for a class target and for a trait target, and the same in the analyzer and at
  run time. The two layers differ only in the evidence they can read: the analyzer reads the source's
  static type, and the runtime reads the value's actual one. So a store whose source is a `Variant`
  carries no static evidence, is accepted by the analyzer, and is asked again under the same rule when
  it executes — where a value with conflicting arguments is rejected.
- A **class-handle** declaration is the exception. `Type[Crate[int]]` demands evidence exactly as `is`
  does, so a raw `Crate` handle fails that store while a `Crate[int]` handle passes it. The gradual
  rule covers declarations whose type names a class or a trait, never one that names a class handle.

#### `await`

```ebnf
await_expr = "await", expression ;   (* operand parsed at PREC_AWAIT *)
```

Makes the enclosing function a coroutine.

#### Calls and named arguments

```ebnf
call            = callee, "(", [ call_args ], ")" ;
callee          = expression | "super" [ ".", identifier ] | generic_application ;
call_args       = call_arg, { ",", call_arg }, [ "," ] ;
call_arg        = [ identifier, "=" ], expression ;     (* named argument *)
generic_application = ( identifier | attribute_access ), "[", type_arg_list, "]" ;
```

- `super(...)` calls the parent method of the same name; `super.name(...)` calls a named
  parent method.
- A **named argument** is `identifier = value`. This is unambiguous because assignment is a
  statement, never an expression, so `IDENTIFIER` followed by `=` inside an argument list is
  always a named argument.
- `name[TypeArgs](...)` / `receiver.method[TypeArgs](...)` is explicit generic-method
  application; the bracket list is a use-site type-argument list (see subscript below).
- A named argument may target only a **fixed** parameter. A rest parameter is never nameable, and
  every argument past the fixed parameter list is positional and checked against the rest element
  type (see the function-declaration rules above).

#### Attribute access

```ebnf
attribute_access = expression, ".", identifier ;
```

A broad set of keywords is accepted as the attribute name (node-name rule).

#### Tuple index access

```ebnf
tuple_index_access = expression, ".", decimal_integer_literal ;
```

Valid on any expression, not only a tuple-typed one. The tokenizer already lexes a digit
directly following a value-preceded `.` as a bare decimal-integer `LITERAL` rather than a
float (§2.6, §9), so `t.0` is unambiguous: `t . 0`. The parser folds this shape into the
same subscript node as `attribute_access`/`subscript` (marked distinctly so later passes can
tell `t.0` apart from `t[0]`), not a separate expression kind. `t.0.1` is nested member
access (`(t.0).1`), never a float.

#### Subscript and use-site type arguments

```ebnf
subscript      = expression, "[", index_or_type_args, "]" ;
index_or_type_args = expression
                   | type_arg, { ",", type_arg }, [ "," ] ;   (* >1 arg, or "?" markers *)
type_arg       = ( expression | tuple_type_arg | signature_type_arg ), [ "?" ] ;

tuple_type_arg     = "(", type_arg, ",", type_arg, { ",", type_arg }, [ "," ], ")" ;
signature_type_arg = ( "Callable" | "AsyncCallable" ),
                     "[", "[", [ type_arg, { ",", type_arg } ], "]", ",", type_arg, "]"
                   | "Signal", "[", "[", [ type_arg, { ",", type_arg } ], "]", "]"
                   | "Coroutine", "[", type_arg, "]"
                   | "Type", "[", type_arg, "]" ;
```

A single index is ordinary subscription. When the bracket list contains commas and/or a
trailing `?` nullable marker on an element (`Pair[int, String]`, `id[Node?]`), it is a
use-site type-argument list captured for generic specialization. The first element always
aliases the index. (Parsing stops the index expression before a trailing `?` so the nullable
marker is not consumed as the invalid `?` operator.)

A type argument is a type, so the spellings a type has that an expression does not are admitted
here too and denote exactly the same type they do in an annotation: an unnamed tuple type
(`Box[(int, String)]`) and the built-in signature forms (`Box[Callable[[int], bool]]`,
`Box[Signal[[int]]]`, `Box[Coroutine[int]]`, `Box[Type[Node]]`). These shapes are read out of the
expression grammar — a tuple type is lexed as a tuple literal and a signature's parameter list as an
array literal — and are re-read as types once the head is known to take type arguments. The two
type-only spellings with no value-position form are `void` and a Callable rest tail (`...Array[T]`);
neither is spellable as a type argument.

#### Array and dictionary literals

```ebnf
array_literal      = "[", [ expression, { ",", expression }, [ "," ] ], "]" ;

dictionary_literal = "{", [ dict_entry, { ",", dict_entry }, [ "," ] ], "}" ;
dict_entry         = python_entry | lua_entry ;
python_entry       = expression, ":", expression ;       (* { key: value } *)
lua_entry          = ( identifier | STRING ), "=", expression ;  (* { key = value } *)
```

A dictionary uses **one** style consistently: Python (`key: value`) or Lua-table
(`key = value`). The style is decided by the first entry's separator; mixing is an error.
In Lua style the key must be an identifier or string literal and is treated as a constant
StringName.

#### Lambdas

```ebnf
lambda = "func", [ identifier ], "(", [ parameter_list ], ")",
         [ "->", return_type ], ":", block ;
```

A lambda is `func` used as an expression, optionally named. Its body is a suite/block; the
tokenizer cooperates so a multi-line lambda body can appear inside a larger expression.

#### Get-node (`$` / `%`)

```ebnf
get_node       = "$", node_path
               | "%", node_path                  (* unique-name shorthand *)
               | "%", ...                          (* also valid as % prefix in a path *)
               ;
node_path      = ( STRING | node_segment ), { "/", path_part } ;
path_part      = [ "%" ], ( STRING | node_segment ) ;
node_segment   = node_name ;   (* identifier or many keywords accepted as node names *)
```

`$node/child`, `$"quoted/path"`, `%UniqueName`, and `$%Unique/child` are all valid. A
leading `%` marks a unique-name lookup and is only valid at the start of a name (after `$`
or `/`).

#### Preload

```ebnf
preload_expr = "preload", "(", expression, [ "," ], ")" ;
```

`preload` is a keyword that takes a single resource-path expression (a trailing comma is
tolerated).

---

## 6. Statements

```ebnf
statement =
      pass_stmt
    | var_decl_stmt
    | const_stmt
    | destructure_stmt
    | if_stmt
    | for_stmt
    | while_stmt
    | match_stmt
    | break_stmt
    | continue_stmt
    | return_stmt
    | breakpoint_stmt
    | assert_stmt
    | annotation_stmt
    | expression_stmt ;

block          = NEWLINE, INDENT, statement, { statement }, DEDENT
               | statement ;                       (* single-line / inline suite *)
```

Statement terminator: a statement ends at `NEWLINE`, `;`, or end-of-file. Multiple
statements may be separated by `;` on one line. (`is_statement_end` / `end_statement`.)

```ebnf
pass_stmt       = "pass", NEWLINE ;
break_stmt      = "break", NEWLINE ;               (* only inside a loop *)
continue_stmt   = "continue", NEWLINE ;            (* only inside a loop *)
breakpoint_stmt = "breakpoint", NEWLINE ;
return_stmt     = "return", [ expression ], NEWLINE ;

var_decl_stmt   = [ "final" ], "var", identifier,
                  [ ":", ( type | (* inferred *) ) ], [ "=", expression ], NEWLINE ;
const_stmt      = "const", identifier, [ ":", [ type ] ], "=", expression, NEWLINE ;

destructure_stmt = ( "var" | "const" ), "(", destructure_binding,
                   ",", destructure_binding, { ",", destructure_binding }, [ "," ],
                   ")", "=", expression, NEWLINE ;
destructure_binding = identifier | "_" ;

assert_stmt     = "assert", "(", expression, [ ",", expression [ "," ] ], ")", NEWLINE ;

if_stmt         = "if", expression, ":", block,
                  { "elif", expression, ":", block },
                  [ "else", ":", block ] ;

while_stmt      = "while", expression, ":", block ;

for_stmt        = "for", identifier, [ ":", type ], "in", expression, ":", block ;

annotation_stmt = ANNOTATION, [ "(", [ annotation_args ], ")" ], [ NEWLINE ] ;

expression_stmt = expression, NEWLINE ;            (* assignment / call / await, etc. *)
```

Notes:

- Local `var` may be `final var`. Local `final const` is rejected (`const` is already
  immutable).
- A destructuring declaration is a statement only; it has no class-body form. It is chosen
  over `var_decl_stmt`/`const_stmt` purely by a `(` following `var`/`const`.
- It must bind at least two elements (matching the arity-2 minimum of every other tuple
  form), a trailing comma is allowed, and an initializer is mandatory. A `_` binding
  discards its element instead of declaring a name; repeating `_` is allowed.
- Bindings carry no type annotation: each takes the static type of the tuple element it
  reads. The initializer must be a statically known tuple of exactly the bound arity;
  `Variant` and every non-tuple type are rejected.
- `const` bindings are write-once locals (enforced by the same analysis as `final var`),
  not compile-time constants: their values come from a runtime tuple.
- Nested destructuring, per-binding annotations, and destructuring in `for` are not part of
  the language.
- `for` may bind a typed loop variable (`for i: int in ...`).
- `assert` takes a condition and an optional message.
- Statement-level expressions are typically calls, assignments, or `await`; a bare
  standalone expression triggers a warning (except string "comments").

### 6.1 Match statement and patterns

```ebnf
match_stmt   = "match", expression, ":", NEWLINE, INDENT,
               { match_branch | "pass" NEWLINE | match_branch_annotation },
               DEDENT ;

match_branch = pattern, { ",", pattern },
               [ "when", expression ],            (* pattern guard *)
               ":", block ;

pattern      =
      "var", identifier                            (* bind *)
    | "_"                                           (* wildcard *)
    | ".."                                          (* rest (array/dict only) *)
    | "[", [ pattern, { ",", pattern } ], "]"       (* array pattern *)
    | "{", [ dict_pattern_entry, { ",", dict_pattern_entry } ], "}"  (* dict pattern *)
    | "(", pattern, ")"                             (* grouping *)
    | tuple_pattern
    | case_pattern
    | expression ;                                  (* literal or value pattern *)

tuple_pattern = "(", pattern, ",", [ pattern, { ",", pattern } ], [ "," ], ")" ;

case_pattern = case_reference,
               "(", case_payload_pattern, { ",", case_payload_pattern }, [ "," ], ")" ;

case_reference = qualified_case_reference | contextual_enum_case ;

qualified_case_reference = identifier, { ".", identifier }, [ case_type_arguments ],
                           ".", identifier, { ".", identifier } ;
                                                    (* the contextual form names a single case of
                                                       the subject's union and takes neither a
                                                       type-argument list nor a longer chain *)

case_type_arguments = "[", type_arg, { ",", type_arg }, "]" ;
                                                    (* generic tagged union application;
                                                       `type_arg` as in subscript, section 8 *)

case_payload_pattern =
      identifier                                    (* payload bind, no "var" needed *)
    | pattern ;

dict_pattern_entry =
      ".."                                          (* rest *)
    | expression, [ ":", pattern ] ;                (* key [: value pattern] *)
```

Rules:

- A branch may list multiple comma-separated patterns; a variable bind (`var x`) cannot be
  combined with multiple patterns. This includes payload binds of a case pattern.
- `..` (rest) is valid only inside array/dictionary patterns and must be last.
- A `when` guard adds a boolean condition; pattern binds are in scope in the guard and the
  branch body.
- Only `@warning_ignore` annotations are allowed on match branches.
- An expression pattern ordinarily matches by type-and-value equality against the subject, with one
  exception: when the expression is a type test (§5.5) whose operand is the **same identifier** as
  the match subject, and the subject itself is a plain identifier, the pattern is that type test —
  the same predicate `value is T` denotes outside a `match`, evaluated against the subject value the
  statement saved once. The subject then narrows to the tested type inside the branch, exactly as it
  does inside `if value is T:`. Anything else stays an equality pattern or is rejected: an unrelated
  operand, a non-identifier subject, and the negated form `value is not T` (which parses as a unary
  `not` over a type test, not as a type test) are all errors. The `is` operand rules of §5.5 apply
  unchanged, so a multi-member union on the right of `is` remains an error here too.
- A tuple pattern has arity >= 2 and matches element by element; `(p)` is a grouping and `(p,)`
  is an error, never a one-element tuple. Because a tuple erases to a read-only Array, an array
  pattern of the same arity (`[a, b]`) also matches a tuple value.
- A case pattern is a dotted name **immediately** followed by `(`, and names a tagged-union case;
  its sub-pattern count must equal the case's payload arity. A payload-less case is matched as
  the ordinary value it is (`Message.Quit`), without parentheses.
- The head may also be the contextual shorthand (`.Ok(value)`), which names a case of the union
  the match subject already has. A payload-less contextual case (`.None`) is likewise matched as
  the ordinary value expression it is, without parentheses.
- A **generic** tagged union's case pattern applies the union's full type-argument vector on the
  head, before the case name: `Result[int, String].Ok(value)`. The payload binds then take the
  specialized field types, and a pattern from a different specialization than the subject is
  rejected. A payload-less case of a generic union is written the same way and matched as the
  ordinary value it is (`Bundle[int].Empty`). Only the `(` that follows the dotted name tells a
  case reference apart from an indexed value pattern (`TABLE[INDEX]`), so `case_type_arguments`
  is the same `type_arg` list a subscript carries, and each argument is read as a type only once
  the head is known to be a case reference. `type_arg` admits every type spelling, so a
  specialization whose argument has no expression form — an unnamed tuple type, a signature form —
  is written the same way here as in an annotation: `Holder[(int, String)].Some(value)`.
- A case reference carries its arguments on the name that owns them, which is the **last** name of
  the qualified head, so a generic union nested under a class is written
  `Outer.Result[int, String].Ok(...)` — the same position the type production uses for
  `Outer.Result[int, String]`.
- Directly inside a case pattern's parentheses a bare identifier is a payload bind, matching the
  `is Case(x, y)` form; `_` skips the position and any other expression stays a value pattern
  (so a constant is still written `Message.Move(Config.ORIGIN_X, y)`). Nested patterns follow the
  ordinary pattern rules, so a bind inside one needs `var` (`Shape.Rect((var w, var h))`).
- Exhaustiveness over a tagged union counts a case as handled by a bind or wildcard branch, by a
  payload-less case value, or by a case pattern whose sub-patterns are all irrefutable
  (binds/wildcards, recursively). A refutable sub-pattern such as `Move(0, y)` covers nothing.
- A guarded branch never contributes coverage, since its guard can fail, and a `match` over a
  nullable subject is exhaustive only when a `null` pattern (or a wildcard) is also present.
- A **plain enum is an open, integer-backed domain**. Its declared members name values; they are not
  proof that no other integer inhabits an enum-typed slot, since a cast such as `99 as Level` warns
  and proceeds. A `match` over a plain enum is therefore exhaustive only through a branch that
  cannot fail: an unguarded wildcard or bind, an unguarded `value is Variant` test, or an unguarded
  test against the enum's whole integer carrier (`value is long`), which admits the undeclared values
  as well. Listing every member, handling extra integer literals, or testing `value is Level` all
  leave the undeclared carrier values unhandled. Tagged unions and `bool` remain closed domains.
- An unguarded same-subject type-test pattern covers the whole subject when the tested type accepts
  every value the subject can hold, and the branch then counts exactly as a wildcard does. That is
  decided in three shapes: any subject tested against `Variant`; a closed domain tested against its
  own type — a non-nullable `bool` against `bool`, and a non-nullable tagged union against its own
  type (a case test such as `value is Message.Move` covers that case, not the union); and a
  non-nullable plain enum tested against the whole of its integer carrier (`value is long`). Every
  other test covers nothing: a partial test such as `value is int` on an `int | String` subject, a
  narrower carrier width such as `value is int` on a plain enum, and `value is Level` on a plain-enum
  subject, which is a runtime membership test over the declared values.
- Exhaustiveness is normative for flow analysis, not only for diagnostics: a `match` over a closed
  domain — a tagged union or `bool` — whose branches cover the whole domain and all terminate is
  itself terminating, so a value-returning function needs no trailing `return` after it. The same
  holds for a `match` with a branch that cannot fail, which is the only way a plain-enum `match`
  becomes terminating.

---

## 7. Types

```ebnf
type = type_member, { "|", type_member } ;           (* `|` is the loosest type operator *)

type_member =
      "void"                                         (* only where allowed: return type *)
    | identifier, { ".", identifier }, [ type_suffix ], { ".", identifier }, [ "?" ]
    | tuple_type ;
                                                     (* at most one type_suffix, and only
                                                        collection_args may follow a dotted head *)

type_name = identifier, { ".", identifier } ;        (* dotted, e.g. MyEnum, A.B *)

tuple_type = "(", type, ",", type, { ",", type }, [ "," ], ")", [ "?" ] ;
                                                      (* unnamed, structural; arity >= 2 *)

type_suffix =
      collection_args                                (* Array[int], Dictionary[String, int] *)
    | callable_signature                             (* Callable[[...], R], AsyncCallable[...] *)
    | signal_signature                               (* Signal[[...]] *)
    | coroutine_arg                                  (* Coroutine[T] *)
    | type_handle_arg ;                              (* Type[T] *)

collection_args   = "[", type, { ",", type }, "]" ;
callable_signature= "[", "[", [ callable_parameter_list ], "]", ",", type, "]" ;
callable_parameter_list
                  = callable_parameter, { ",", callable_parameter } ;
callable_parameter= type
                  | "...", type ;                    (* rest tail; final and at most once *)
signal_signature  = "[", "[", [ type, { ",", type } ], "]", "]" ;
                                                     (* a Signal never has a rest tail *)
coroutine_arg     = "[", type, "]" ;                 (* exactly one; void allowed *)
type_handle_arg   = "[", type, "]" ;                 (* exactly one *)
```

Details (`parse_type`):

- A trailing `?` marks the type **nullable** (`Node?`, `Array[int]?`, `Callable[...]?`).
- **Type unions.** `A | B` denotes the static set of its alternatives. `|` is contextual: it unions
  types only while a type is being parsed, and expression-level `|` keeps its own precedence and
  meaning as bitwise OR (§5.2). Within a type, `|` is the **loosest** operator — looser than `?` —
  so `int? | uint` is the union of a nullable `int` and a `uint`. There is no parenthesized type
  form to say otherwise, because `(A, B)` is already an unnamed tuple, so `(int | uint)?` is not
  spellable; a nullable union is written by marking any one member instead. A union with a missing
  alternative (`int |`) is a parse error (`Expected a type after "|".`).
- **Union members.** Any type may be a member -- built-ins, native/script classes, traits, enums and
  tagged unions, tuples, generic specializations, `Type[T]` handles, nullable forms of these, and
  other aliases -- except `void` (`"void" cannot be a member of a type union.`) and `Variant`,
  which already admits every type
  (`"Variant" cannot be a member of a type union, since it already admits every type. Write
  "Variant" on its own instead.`), and a **bare type parameter**
  (`Type parameter "T" cannot be a member of a type union.`). A type parameter still nests freely
  inside a member, so `Array[T] | int` is valid.
- **Union normalization.** A union denotes a canonical set: alias members are expanded, nested
  unions flattened, nullability **hoisted** onto the union (a union is nullable when any member is,
  and members are then stored non-nullable), duplicates removed, and member order canonicalized. So
  `int? | uint` and `int | uint?` are the same type, as are `int | uint` and `uint | int`. A set
  that normalizes to a **single** member is that member, indistinguishable from it thereafter, so
  `type Meters = float` behaves exactly like `float`, its integer width included.
- **Runtime erasure.** A multi-member union has no runtime representation: it produces no typed
  local, no typed parameter check, no runtime type test, and a `PropertyInfo` of `Variant::NIL`.
  A single-member alias keeps the member's runtime typing in full.
- This section specifies the syntax only; for when a type union is the right tool and when a tagged
  union, a trait, `T?`, or `Variant` is, see `docs/fs_language_primer.md`. Two consequences follow. A
  multi-member union is **not a valid typed-container element type** — `Array[int | uint]` and
  `Dictionary[String, int | uint]` are errors, because a typed container enforces exactly one
  element type at runtime — and it is **not a valid `is` or `as` operand type**, because the
  runtime has no union carrier to test or cast against; an individual alternative is named instead.
- **Union compatibility.** A concrete value satisfies a union slot when it satisfies at least one
  alternative. A union-typed value satisfies a concrete slot only when **every** alternative does,
  since nothing narrows the value at the boundary.
- A **type suffix binds to the last name of the dotted head**, so `Outer.Box[int]` applies `[int]`
  to `Box`, exactly as the value-position spelling does. A type carries **at most one** suffix;
  writing it on an earlier name (`Outer[int].Box`) is a parse error
  (`A type-argument list must be written after the last name of a qualified type.`), and so is a
  second one (`A type can carry only one type-argument list, written after the last name of a
  qualified type.`).
- Only `collection_args` is reachable after a dotted head. The built-in suffix forms
  (`callable_signature`, `signal_signature`, `coroutine_arg`, `type_handle_arg`) stay
  **unqualified**, so `A.Callable[[int], bool]` is a parse error.
- A dotted tail may follow the suffix, and in practice only `collection_args` is ever followed by
  one, because the other suffixes name built-in forms that carry no members: the tail after an
  argument list applies a generic tagged union before naming one of its cases
  (`Result[int, String].Ok`, `Outer.Result[int, String].Ok`). That tail is admitted only where a
  case reference is — the right-hand side of `is` — and it is what makes the suffix-position parse
  error above unambiguous everywhere else.
- **Integer type names** are exactly `int`, `uint`, `long`, and `ulong`. They are ordinary
  built-in type names rather than keywords, so they are resolved in type position only and
  remain usable as identifiers elsewhere. No other integer spelling exists.
- **`Number`** is a compiler-provided, globally visible type naming the union of the
  source-spellable numeric types — `int`, `uint`, `long`, `ulong`, and `float`. It is closed: no
  user declaration joins it. Like any other multi-member union it is type-position-only and
  erases at runtime, and because it is answered at the same precedence as a built-in type name,
  a declaration that reuses the spelling (`class_name Number`, `trait_name Number`, `class
  Number`, `type Number = ...`) is reported at its own declaration
  (`... "Number" hides the compiler-provided type "Number".`).
- **Typed collections**: `Array[int]`, `Dictionary[String, int]`, etc. — one or more
  comma-separated element types. `void` is not allowed as an element type.
- **`Callable[[P1, P2], R]`** — a parameter-type list in inner brackets, a comma, then
  the return type (`void` allowed). `AsyncCallable[...]` is the same shape but flagged async.
- **Variadic callable `Callable[[P1, ...Array[T]], R]`** — the parameter list may end with one
  rest entry spelled `"..."` followed by a type. The rest entry is optional, may appear at most
  once, must be the **final** parameter entry, and is **Callable-only**. Its type must resolve to
  `Array` or `Array[T]`; any other resolved type is an error
  (`The Callable rest parameter type must be "Array", but "int" is specified.`). A middle-position
  rest entry (`The rest parameter type must be the final Callable parameter type.`), a repeated one
  (`A Callable signature can contain only one rest parameter type.`), and a rest entry on a `Signal`
  (`Signal signatures cannot declare a rest parameter.`) are parse errors. An untyped `...Array`
  (or `...Array[Variant]`) tail marks the callable variadic but stays gradual, indistinguishable
  from a native untyped vararg.
- **`Signal[[P1, P2]]`** — a parameter-type list only; a signal signature may not
  specify a return type, and signals are always fixed-arity (no rest entry).
- **`Coroutine[T]`** — exactly one result type (`void` allowed) — the typed handle to an
  in-flight async computation.
- **`Type[T]`** — exactly one represented instance type (a class/type handle).
- **Unnamed tuple type `(T1, T2, ...)`** — structural, arity >= 2; a trailing comma is
  allowed once arity is >= 2. An empty `()` or single-element `(T)` tuple type is a parse
  error (`(T)` alone is never a type-position grouping — unlike the expression grammar,
  there is no ambiguity to preserve). Nesting is allowed: `((int, int), bool)`.
- Inner type nesting is depth-bounded to avoid stack overflow on pathological input.

Types appear in: variable/constant/parameter annotations, `for` loop variable annotations,
return types, casts (`as type`), type tests (`is type`), `extends`/`uses` type arguments,
and type-parameter bounds.

### 7.1 `Self`

`Self` is a contextual type name, not a keyword: it is an ordinary identifier that only has this
meaning inside a class, trait, or `extend` body, and remains usable as a plain identifier
everywhere else. It is written wherever a `type_name` is, may not be qualified (`Self.Inner`) or
specialized (`Self[T]`), and nests like any other type: `Array[Self]`, `Dictionary[String, Self]`,
`Type[Self]`, `Callable[[Self], Self]`, and a generic specialization such as `Crate[Self]`.

`Self` also appears in **expression** position inside a function body, where it denotes the class
handle of the receiver the running call was made through — the same value `Type[Self]` describes.
`Self.new()` is therefore the ordinary class-handle construction form, and its result type is
`Self`.

**Receiver rule.** For a static call made through a class handle whose exact runtime type is `R`,
every occurrence of `Self` in the invoked implementation denotes `R`, at every nesting depth. This
holds when method lookup selects an implementation declared on an ancestor `B`, and equally when
it selects a retroactive conformance witness declared for a conformance target: the target is what
validates the witness *declaration*, never what `Self` means when the witness *runs*.

- `Derived.make()` uses `Derived`, even when `make` is inherited from `Base`.
- An explicit `Base.make()` uses `Base`.
- An inherited implementation that delegates through an unqualified call or `super` preserves the
  incoming receiver; it does not reset `Self` to the class containing the delegated implementation.
- A specialized generic receiver keeps all of its concrete type arguments in `Self`.
- In an instance method, `Self` is the class the running member was materialized for, and
  `Self` in expression position is the class handle of the object the method runs on.

Runtime validation uses that exact specialization. Argument and return checks, typed containers,
reified generic arguments, class handles, construction, casts, and type tests are all resolved
against it, and none of them widen `Self` to `Variant`, to the declaring class, to the conformance
target, or to a common base in order to make a call succeed. A static call that reaches an
implementation without a receiver is reported as an error rather than resolved approximately.

**Parameter positions.** What satisfies a parameter whose type mentions `Self` follows from how exactly
the call site knows the receiver, and the answer is the same at every nesting depth.

- When the receiver is exact — a call through a class handle, or an instance of a `final` class —
  `Self` denotes that class, and a parameter mentioning it admits exactly the values that class
  admits, in every position and in every container. `Derived.take((1, derived))` is an ordinary
  argument check against `(int, Derived)`.
- When the receiver is open — an instance typed as the class that declares the member, as an
  overriding subclass, as a conformance target, or as `self`/`super` — `Self` denotes the runtime
  leaf, which the call site does not know. A value merely typed as the static class is rejected,
  because an override may narrow what the call dispatches to. What is admitted instead is the
  receiver itself: an argument that *is* the receiver is an instance of the leaf whatever the leaf
  turns out to be. Identity must be provable where the call is written, so the argument is the
  receiver reference the call is made through (`self` for an unqualified, `self.`, or `super.` call,
  and the same name for a call qualified by one).
- Tuple element positions carry that rule inward, positionally and recursively: a parameter typed
  `(int, Self)` admits a tuple literal whose second element is the receiver, `(int, (String, Self))`
  admits a literal whose nested second element is, and non-`Self` positions are checked by the
  ordinary rules. Nullability is unwrapped first, so a `(int, Self)?` parameter also admits `null`.
- Typed containers and generic type arguments of `Self` — `Array[Self]`, `Dictionary[String, Self]`,
  `Type[Self]`, `Callable[[Self], Self]`, `Crate[Self]` — require an exact receiver. They are checked
  as a carrier's declared element type, invariantly, so holding the receiver inside a carrier built
  against the static class does not make the carrier the leaf's.

Whether a parameter is satisfied never depends on the calling function's own signature or body: the
same call written in two functions of one class is accepted or rejected alike.

**Deferred execution.** The receiver rule is unaffected by a call and its execution being separated
in time.

- Reading a static function off a class handle without calling it (`Derived.make`, or an
  unqualified `make` inside a static body) yields a callable paired with that exact receiver.
  Invoking it later means what calling through the same handle would have meant, and two callables
  extracted from different receivers for one inherited implementation keep their own
  specializations. A callable extracted from a specialized generic handle keeps that
  specialization for as long as the callable is reachable, including past the scope it was
  extracted in.
- A call that suspends resumes with the receiver it began on, including across a suspension that
  happens inside a nested call it awaits. Validation performed after a resumption reports that
  receiver, not the class the resumed implementation was declared on.
- A callable or a resumption whose receiver is gone is an error, never a call resolved against the
  declaring class.
- A lambda created while a static frame runs captures that frame's receiver lexically, the same way
  it captures an enclosing local. The captured receiver is immutable for that callable, so invoking
  it after the creating function has returned, from a non-static caller, or while an unrelated
  receiver `S` is active all use the captured receiver `R`, never `S` or the declaring class. Two
  lambdas created through sibling receivers keep independent receivers. This is the ordinary lambda
  path, distinct from instance-`self` capture: a static lambda never carries an instance object, and
  a lambda created with no receiver reports the missing receiver if its body or signature needs
  `Self` rather than substituting another class.


---

## 8. Built-in annotations

The parser pre-registers the following built-in annotations (`register_annotation` in
`fs_parser.cpp`). Custom annotations are declared with the `annotation` declaration (§4.7).

| Annotation | Targets | Notable args |
|------------|---------|--------------|
| `@tool` | script | — |
| `@icon` | script | `icon_path` |
| `@static_unload` | script | — |
| `@autoload` | script | `depends_on`, `order_id` (named args allowed) |
| `@keep_name` | class, variable, function, signal, constant, named enum | — |
| `@noreturn` | function | — |
| `@onready` | variable | — |
| `@export` | variable | — |
| `@export_enum` | variable | `names...` (vararg) |
| `@export_file` / `@export_file_path` / `@export_dir` | variable | optional `filter` |
| `@export_global_file` / `@export_global_dir` | variable | optional `filter` |
| `@export_multiline` / `@export_placeholder` | variable | text/placeholder |
| `@export_range` | variable | `min, max, step, extra_hints...` |
| `@export_exp_easing` | variable | `hints...` |
| `@export_color_no_alpha` | variable | — |
| `@export_node_path` | variable | `type...` |
| `@export_flags` | variable | `names...` |
| `@export_flags_2d_render` / `_2d_physics` / `_2d_navigation` | variable | — |
| `@export_flags_3d_render` / `_3d_physics` / `_3d_navigation` | variable | — |
| `@export_flags_avoidance` | variable | — |
| `@export_storage` | variable | — |
| `@export_custom` | variable | `hint, hint_string, usage` |
| `@export_tool_button` | variable | `text, icon` |
| `@export_category` / `@export_group` / `@export_subgroup` | standalone | `name[, prefix]` |
| `@warning_ignore` | class-level + statement | `warning...` (vararg) |
| `@warning_ignore_start` / `@warning_ignore_restore` | standalone | `warning...` (vararg) |
| `@rpc` | function | `mode, sync, transfer_mode, transfer_channel` |

`@deprecated`, `@experimental`, and `@tutorial` are intentionally **not** annotations; they
are written in `##` doc comments and produce errors if used as annotations.

---

## 9. Parsing notes and ambiguities

- **Single-token lookahead.** The parser uses only the current token and one buffered
  lookahead. Re-implementations should preserve this property; it keeps the grammar simple.
- **Recursion bounds.** Expression, statement, type, and pattern nesting are each bounded
  (`MAX_NESTING_DEPTH`) so deeply nested input reports an error instead of overflowing the
  native stack.
- **`|` in a type vs. bitwise OR.** `|` is a type-union operator only while a type is being
  parsed. Because the right-hand side of `is` and `as` is a type position, `x is int | uint` and
  `x as int | uint` union the two types there rather than applying bitwise OR to the test result;
  everywhere else `|` is unchanged.
- **`?` nullable vs. operator.** `?` is only valid as a type suffix. Subscript index parsing
  stops before a trailing `?` so `Box[Node?]` works; a bare `?` elsewhere is an error
  pointing to the `if/else` ternary.
- **Named arguments are unambiguous** because assignment is never an expression: inside an
  argument list, `IDENTIFIER` `=` is always a named argument, while `==` is comparison.
- **Dictionary style detection** is based on the first entry's separator (`:` vs `=`).
- **Contextual keywords** (`annotation`, `extend`, `type`, `async`, `targets`, `get`, `set`,
  `CLASS`/`METHOD`/`VARIABLE`/`SIGNAL`/`CONSTANT`) are lexed as identifiers and only gain
  meaning from position. `extend` is distinct from the reserved `extends` keyword token.
- **Multiline mode** inside brackets and around lambda bodies suspends layout-token
  generation; a re-implementation must replicate this to handle multi-line literals,
  argument lists, and lambdas.
- **`.` after a value token is always member access, never a float.** `.` followed by a digit
  only starts a number literal when the previous token cannot end a value; after an
  `IDENTIFIER`, a literal, `)`, `]`, or a numeric constant keyword, `.<digit>` lexes as
  `PERIOD` then a plain decimal-integer literal (tuple index access), so `t.0`, `t.0.1`, and
  `(f()).0` are all member access, while `.5` at the start of an expression is still the float
  `0.5`.
- **`(a)` is grouping; `(a,)` and `()` are errors.** A single parenthesized expression is
  always ordinary grouping, never a 1-tuple; FoundryScript has no 1-tuples or empty tuples, so
  a lone trailing comma around one element (`(a,)`) and an empty parenthesized pair (`()`) in
  expression position are both hard parse errors rather than silently becoming a tuple or a
  no-op. The parser only commits to the tuple-literal shape once it sees a `,` after the
  first element; `(a, b)`, `(a, b,)`, ... are tuple literals (arity >= 2, trailing comma
  allowed). The same arity-2 minimum and error shapes apply to the unnamed tuple *type*
  `(T1, T2)` in type position (§7) and to a `tuple Name(...)` *declaration* (§4.4a) and to a
  destructuring declaration's binding list (§6) — an empty or single-element tuple type,
  declaration, or binding list is a parse error there too.

---

## 10. Keeping this document in sync

This file is a normative specification consumed by external re-implementations (e.g. a
Pratt parser in another language). **Whenever a change to Foundry Script affects its
grammar** — adding/removing/renaming tokens or keywords, changing operator precedence or
associativity, altering statement/declaration/type/expression/pattern syntax, or changing
the built-in annotation set — update this document in the **same** change so it stays
authoritative. This requirement is also recorded in the repository `AGENTS.md`.
