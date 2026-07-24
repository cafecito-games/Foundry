# Enum Host Function Parser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Teach the Foundry Script parser and AST to represent functions declared inside named,
indented enum bodies while rejecting invalid enum-body declarations with targeted diagnostics.

**Architecture:** Extend `EnumNode` with ordered function storage and give each stored `FunctionNode`
a direct owner-enum pointer. Refactor the enum-body loop into a value phase followed by a function
phase, reusing the existing declaration-modifier, annotation, signature, and suite parsers. Keep all
semantic resolution and runtime dispatch out of this parser-only slice.

**Tech Stack:** C++17, Foundry Script tokenizer/parser AST, doctest, script parser fixtures, ISO 14977
EBNF in `GRAMMAR.md`.

---

### Task 1: Lock the AST and parser contract with failing tests

**Files:**
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`

- [ ] **Step 1: Add a named-enum AST success test**

Parse a source containing values, annotated instance/static/async functions, a functions-only enum,
a nested enum, and an `enum_name`. Assert the ordered function vectors, modifiers, bodies, and each
function's direct owner pointer:

```cpp
CHECK_EQ(log_level->functions.size(), 2);
CHECK_EQ(log_level->functions[0]->identifier->name, SNAME("name"));
CHECK_EQ(log_level->functions[0]->owner_enum, log_level);
CHECK_FALSE(log_level->functions[0]->is_static);
CHECK(log_level->functions[1]->is_static);
```

- [ ] **Step 2: Add focused invalid-syntax tests**

Use `check_parse_source_error()` to prove the parser emits targeted errors for a value after a
function, an unnamed enum function, a non-function declaration, and `abstract`/`final` enum
functions:

```cpp
check_parse_source_error("enum E:\n\tfunc name():\n\t\tpass\n\tA = 0\n",
		"Enum values must be declared before enum functions.");
```

- [ ] **Step 3: Run the focused tests against the existing binary**

Run the new success fixture through the freshly built `origin/develop` binary:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless script lint --fail-on=error \
	modules/foundry_script/tests/scripts/parser/features/enum_host_functions.norun.fs
```

Expected: exit 1 with the first enum `func` rejected as an enum key.

### Task 2: Add AST ownership and parse enum functions

**Files:**
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser.cpp`

- [ ] **Step 1: Add enum function storage and owner metadata**

Add ordered storage and lookup metadata to `EnumNode`, plus a nullable direct owner on
`FunctionNode`:

```cpp
Vector<FunctionNode *> functions;
HashMap<StringName, int> functions_indices;
EnumNode *owner_enum = nullptr;
```

- [ ] **Step 2: Parse the enum value phase and function phase**

In `parse_enum()`, collect modifiers only for declarations, accept function annotations, validate
enum functions with static/async enabled and abstract/final disabled, parse via
`parse_function_declaration()`, attach annotations/docs, set `owner_enum`, and append to the enum.
Reject an identifier value after the first function:

```cpp
if (saw_function && check(FSTokenizer::Token::IDENTIFIER)) {
	push_error("Enum values must be declared before enum functions.");
}
```

- [ ] **Step 3: Preserve enum-body restrictions and recovery**

Reject functions on unnamed enums, declarations other than functions, `pass` mixed with values or
functions, and modifiers not followed by `func`. Consume malformed declarations through their body
without allowing them to become outer-class members.

- [ ] **Step 4: Build and run the focused C++ tests**

Run:

```sh
scons platform=macos target=editor dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*enum host functions*" --force-colors
```

Expected: all focused parser tests pass.

### Task 3: Add script fixtures and tree-printer coverage

**Files:**
- Modify: `modules/foundry_script/fs_parser.cpp`
- Create: `modules/foundry_script/tests/scripts/parser/features/enum_host_functions.norun.fs`
- Create: `modules/foundry_script/tests/scripts/parser/features/enum_host_functions.norun.out`
- Create parser error fixture pairs under: `modules/foundry_script/tests/scripts/parser/errors/`

- [ ] **Step 1: Print enum functions in AST debug output**

After printing enum values, print each stored function using the existing function printer:

```cpp
for (FunctionNode *function : p_enum->functions) {
	print_function(function, "Function");
}
```

- [ ] **Step 2: Add success and error fixtures**

Cover values plus functions, nested named enums, functions-only enums, `enum_name`, unchanged
value-only/empty enums, value-after-function, unnamed function, non-function declaration, and
abstract/final modifiers. Expected parser-error files contain the exact targeted diagnostic.

- [ ] **Step 3: Regenerate intentional fixture outputs and run parser fixtures**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test generate-fixtures \
	modules/foundry_script/tests/scripts
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*FoundryScript*" --force-colors
```

Expected: parser fixture outputs match and the focused Foundry Script suite passes.

### Task 4: Make the normative grammar authoritative

**Files:**
- Modify: `modules/foundry_script/GRAMMAR.md`

- [ ] **Step 1: Define enum functions and modifiers**

Replace the value-only enum body production with:

```ebnf
enum_body = NEWLINE, INDENT,
            ( "pass", NEWLINE
            | enum_value_line, { enum_value_line }, { enum_function_decl }
            | enum_function_decl, { enum_function_decl } ),
            DEDENT ;
enum_function_decl = { function_annotation }, { enum_function_modifier },
                     "func", function_signature, function_body ;
enum_function_modifier = "static" | "async" ;
```

Document in prose that only named enums accept functions, values precede functions, and
`abstract`/`final` and all non-function enum-body declarations are invalid.

- [ ] **Step 2: Check formatting and grammar references**

Run:

```sh
git diff --check
rg -n "enum_body|enum_function_decl|enum_function_modifier" \
	modules/foundry_script/GRAMMAR.md
```

Expected: no whitespace errors and all productions are defined and referenced.

### Task 5: Verify, commit, and review

**Files:**
- Verify all files changed by Tasks 1–4.

- [ ] **Step 1: Run CI-style build and focused tests**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes
./bin/foundry.macos.editor.dev.arm64 --headless test run \
	--case "*enum host functions*" --force-colors
```

Expected: build exit 0 and all focused tests pass.

- [ ] **Step 2: Commit the focused implementation**

```sh
git add modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser.cpp \
	modules/foundry_script/GRAMMAR.md modules/foundry_script/tests/test_foundry_script.cpp \
	modules/foundry_script/tests/scripts/parser docs/superpowers/plans/2026-07-24-enum-host-function-parser.md
git commit -m "Parse host functions in named enums"
```

- [ ] **Step 3: Run Cursor review against `origin/develop`**

Invoke the `cursor-review` skill read-only. Triage every finding with
`superpowers:receiving-code-review` and real bugs with `superpowers:systematic-debugging`; fix,
verify, commit, and repeat until the latest valid verdict is exactly `RESULT: clean`.
