# Enum Host Function Bytecode Persistence Design

## Context

Issue #1119 made enum-hosted functions executable from source-compiled scripts. Each exact
`FoundryScript` class owns an `enum_functions` map keyed by enum type; each entry has separate
instance and static function maps. Enum call opcodes carry a stable scalar identity:

`owner script path`, `owner class FQCN`, `enum type`, `function name`, and `call kind`.

The bytecode exporter currently serializes member, implicit, and witness functions but never
serializes `enum_functions`. A bytecode-loaded caller therefore retains its enum-call opcodes while
the loaded owner has no function table for the VM to resolve.

This design is intentionally limited to #1120. It does not change enum syntax, analysis, call
metadata, reflection, dispatch selection, or the runtime errors introduced by #1119.

## Decision

Serialize each class's enum function table in that class's existing class-body payload.

The alternatives were:

1. A dedicated top-level bytecode section keyed by owner references.
2. Folding enum functions into the member-function table.

A separate section would duplicate the class tree's ownership identity and require an additional
cross-reference/fixup layer. Folding into member functions would violate the deliberate #1119
separation and expose enum methods as ordinary class methods. The class-body table preserves the
in-memory model directly and lets the existing per-class function serializer handle every symbolic
pointer and lambda fixup.

## Wire Layout

After the existing member-function table and before the three optional initializer functions, each
class body writes:

```text
u32 enum_count
repeat enum_count:
    u32 enum_type_string_index
    u32 instance_function_count
    repeat instance_function_count:
        function_payload
    u32 static_function_count
    repeat static_function_count:
        function_payload
```

`function_payload` is the existing `serialize_function` representation. Function names and the
static bit are already part of that payload, so the table does not add a redundant function key.

This changes the class-body layout, so `FSBytecodeFormat::FORMAT_VERSION` advances from 2 to 3. Old
buffers continue to fail at the header guard instead of being misread with the new field order.

## Ownership, Lifetime, and Fixups

Every loaded enum function is deserialized with the exact class body currently being loaded as its
`_script`. This mirrors compilation:

- root enum functions point to the root script;
- nested enum functions point to the matching nested class script;
- lambda metadata is registered on that same owner script;
- the owner script owns and deletes the functions through its existing `enum_functions` cleanup.

No new strong external reference is required. Cross-file enum calls persist only the stable scalar
identity already embedded in the opcode. At runtime, `FSCache` resolves the external owner script;
the target script owns its own reconstructed enum table. This differs from conformance witnesses,
whose functions may intentionally compile against a foreign target and therefore need an explicit
target reference.

All existing function-payload pointer fixups, including validated calls, method binds, utilities,
global stores, lambdas, and data types, are reused unchanged. Export-time named-global validation
must traverse enum functions and their nested lambdas because those functions now enter exported
bytecode.

## Validation and Failure Behavior

The exporter rejects inconsistent in-memory state before writing:

- empty enum or function names;
- null functions;
- a function whose `_script` is not the class body owner;
- a function whose static bit does not match its instance/static table;
- duplicate names across instance and static maps.

The loader treats all table metadata as untrusted:

- bound counts against the remaining bytes before iterating;
- reject empty enum and function names;
- reject duplicate enum types;
- reject duplicate function names within or across call kinds;
- reject a function whose serialized static bit disagrees with its table;
- delete an unregistered function immediately on validation failure;
- leave any already-inserted functions owned by the invalid script for normal cleanup.

The enum-call verifier keeps the uniform `argument_count + 2` address layout from #1119, validates
all packed addresses, all four global-name indices, call kind, and now also rejects empty owner path,
owner class, enum type, or function identities. This moves malformed identity references from a
runtime failure to a bytecode-load failure while preserving #1119's clear runtime errors for valid
bytecode whose external owner/function later cannot be resolved.

## Test Strategy

Tests are added before implementation and must fail against the #1119 base:

- whole-script round trip preserves root and nested instance/static enum functions, exact owner
  pointers, enum-function lambdas, and callable behavior;
- the existing four runtime fixtures run in the compiled-bytecode pass after their temporary
  sentinels are removed, covering ordinary, async, nested, and cross-file calls;
- exporter named-global validation finds enum-function and enum-function-lambda references;
- verifier tests reject malformed counts, addresses, indices, call kinds, truncation, and empty
  identity entries for all three enum-call opcodes while accepting the valid layout;
- loader hardening tests corrupt enum and function names to create duplicate table references and
  require a clean `ERR_INVALID_DATA`;
- the format-version pin advances to 3 and the hostile-buffer corpus continues to run.
