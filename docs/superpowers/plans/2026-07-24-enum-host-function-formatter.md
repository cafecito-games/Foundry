# Enum Host Function Formatter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve enum-hosted functions, their metadata, and surrounding trivia when Foundry Script source is formatted.

**Architecture:** `FSPrinter::print_enum()` will use its existing value printer and the standard annotation/function printers for a second function section. The format-test AST comparator will include enum functions so round-trip tests observe every parsed enum child.

**Tech Stack:** C++, Foundry Script formatter, doctest fixtures.

---

### Task 1: Capture the formatter regression

**Files:**
- Create: `modules/foundry_script/tests/scripts/format/enum_host_functions/input.fs`
- Create: `modules/foundry_script/tests/scripts/format/enum_host_functions/expected.fs`
- Modify: `modules/foundry_script/tests/test_format.h:523-535`

- [ ] **Step 1: Write the failing fixture and AST-equivalence expectation**

Create compact mixed, functions-only, empty, and nested enum inputs containing
comments, doc comments, annotations, and static/async modifiers. Set expected
output to canonical colon/indented bodies. Extend the `Node::ENUM` comparison
to require equal `functions` sizes and call `node_vector_eq(a->functions, b->functions)`.

```cpp
if (identifier_name(a->identifier) != identifier_name(b->identifier) ||
		a->values.size() != b->values.size() || a->functions.size() != b->functions.size()) {
	return false;
}
return node_vector_eq(a->functions, b->functions);
```

- [ ] **Step 2: Run the focused formatter suite to verify the expected red failure**

Run: `./bin/foundry.* --headless test run --case "*FoundryScript*Format*" --force-colors`

Expected: the golden enum-host fixture fails because functions are absent from
formatted output; the old comparator would otherwise fail to observe that loss.

### Task 2: Print the enum function section

**Files:**
- Modify: `modules/foundry_script/fs_format.cpp:1591-1634`

- [ ] **Step 1: Write the minimal formatter behavior**

After printing values, emit the canonical blank line only when values and
functions are both non-empty. For each function, call
`print_annotations(function->annotations, function->start_line)` then
`print_function(function)`, retaining the surrounding trivia cursor. Emit
`pass` only when both vectors are empty.

```cpp
if (p_enum->values.is_empty() && p_enum->functions.is_empty()) {
	write_indent();
	write("pass");
	newline();
}
if (!p_enum->values.is_empty() && !p_enum->functions.is_empty()) {
	newline();
}
for (const FSParser::FunctionNode *function : p_enum->functions) {
	print_annotations(function->annotations, function->start_line);
	print_function(function);
}
```

- [ ] **Step 2: Run the focused formatter suite to verify it passes**

Run: `./bin/foundry.* --headless test run --case "*FoundryScript*Format*" --force-colors`

Expected: the golden fixtures and semantic round-trip tests pass.

### Task 3: Validate and review

**Files:**
- Modify: files above only

- [ ] **Step 1: Run strict build and relevant test coverage**

Run: `scons platform=macos target=editor dev_mode=yes tests=yes`

Run: `./bin/foundry.* --headless test run --case "*FoundryScript*Format*" --force-colors`

Run: `./bin/foundry.* --headless test run --case "*FoundryScript*" --force-colors`

- [ ] **Step 2: Commit, run read-only Cursor review against `origin/develop`, and converge**

Commit the focused formatter and test changes. Run the independent review with
`CURSOR_REVIEW_BASE=origin/develop`; triage any finding with receiving-code-review
and systematic-debugging, verify its fix, recommit, and repeat until the review
output is exactly `RESULT: clean`.
