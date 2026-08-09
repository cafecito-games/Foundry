# Grouping Closing Comment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve comments owned by redundant grouping closing delimiters without corrupting enclosing syntax.

**Architecture:** Record close-token ownership in each redundant grouping span. Re-wrap only proven comment-owning
multiline groups and canonicalize unsafe inline close comments to standalone trivia before the synthesized close.

**Tech Stack:** C++17, Foundry Script parser/formatter, golden fixtures, formatter idempotency tests.

---

### Task 1: Add a failing golden matrix

**Files:**
- Create: `modules/foundry_script/tests/scripts/format/expression_grouping_close_comment/input.fs`
- Create: `modules/foundry_script/tests/scripts/format/expression_grouping_close_comment/expected.fs`

- [ ] **Step 1: Add nested and top-level close-owned comments**

The input covers a top-level initializer, call argument, array/dictionary/tuple element, nested grouping, a content-line
comment plus close-line comment, and a full-line comment immediately before `)`. Expected output preserves each comment
once and uses this fallback in nested positions:

```foundryscript
foo(
	(
		1 + 2
		# close note
	),
)
```

- [ ] **Step 2: Add non-owner continuation controls**

Include binary and postfix continuation after `)` where a later trailing comment belongs to the whole continuation.
Keep `expression_grouping_comment_close_line_continuation` and
`expression_grouping_single_line_trailing_comment` unchanged.

- [ ] **Step 3: Build and prove the fixture fails**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*Format*"
```

Expected: golden mismatch or comment-count failure because nested close-line comments are dropped.

### Task 2: Track close-delimiter ownership

**Files:**
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser.cpp`

- [ ] **Step 1: Extend `GroupingSpan`**

```cpp
struct GroupingSpan {
	int open_line = 0;
	int close_line = 0;
	int close_column = 0;
	bool close_is_last_token_on_line = false;
};
```

- [ ] **Step 2: Populate metadata in `parse_grouping()`**

Capture the consumed close token's end column and determine from the tokenizer whether another non-comment token follows
on that physical line. Set `close_is_last_token_on_line` only when the close token owns the remaining inline comment.
Do not scan arbitrary comments inside the span.

### Task 3: Preserve and print only the selected grouping

**Files:**
- Modify: `modules/foundry_script/fs_format.cpp`

- [ ] **Step 1: Select an opening- or closing-comment wrapper**

Extend the existing outermost-first grouping selection. A span qualifies when it is multiline and either owns the
existing unambiguous opening comment or has a close-owned inline comment with no later token.

- [ ] **Step 2: Emit close-owned comments safely**

After printing the inner expression and inner full-line comments, append the selected close comment as an indented
standalone line before reducing indentation and writing `)`. Mark the source line consumed so the caller cannot flush it
again. Never append it directly after the collapsed inner expression.

- [ ] **Step 3: Rebuild and run formatter gates**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*Format*"
```

Expected: golden bytes, idempotency, and comment counts pass; all continuation controls remain unchanged.

- [ ] **Step 4: Root review and commit**

```sh
git add modules/foundry_script/fs_parser.h \
  modules/foundry_script/fs_parser.cpp \
  modules/foundry_script/fs_format.cpp \
  modules/foundry_script/tests/scripts/format/expression_grouping_close_comment
git commit -m "Preserve grouping closing-line comments"
```
