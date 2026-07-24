# Enum Host Function Formatter Design

## Scope

Issue #1121 owns only formatter output and its AST-equivalence test helper after
the parser foundation in #1116. It does not change enum analysis, dispatch,
bytecode, LSP, or documentation behavior.

## Decision

`FSPrinter::print_enum()` will treat enum values and enum functions as two
ordered sections. Values remain newline-separated. When both sections exist,
one canonical blank line separates them. Each enum function is emitted with
the existing annotation and function printers, so modifiers, annotation syntax,
doc comments, body formatting, and trailing comments follow normal formatter
rules. `pass` is emitted only when both sections are empty.

The AST round-trip comparator will compare `EnumNode::functions` with the
existing function-node structural comparison. This closes the previous blind
spot where formatting could silently discard enum functions and still appear
equivalent.

## Test Design

One golden formatter fixture will cover mixed values/functions, functions-only,
an actually empty enum, comments, doc comments, annotations, `static async`,
and a nested enum. The existing golden fixture and corpus idempotence tests
exercise byte-level output and parse/format stability; updating `node_eq`
makes the semantic round-trip assertion detect future function loss.
