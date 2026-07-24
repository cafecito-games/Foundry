# Enum Host Function Runtime Design

## Goal

Make named enum host functions executable in the live Foundry Script runtime.
The parser and analyzer already accept enum functions and annotate every valid
enum call with the stable scalar identity introduced by #1118. This change owns
the compiler and live-VM half of that contract: retain compiled enum functions,
emit dedicated calls, resolve them safely, and bind an instance receiver as
`self`.

This design deliberately does not serialize or reload enum function tables.
That persistence and hardening work belongs to #1120.

## Runtime ownership and identity

Each `FoundryScript` owns the functions declared by enums on that exact class.
The storage is separate from `member_functions`, so enum functions do not
become ordinary instance methods or leak into script reflection.

The table has two levels:

- enum type name -> enum function set
- enum function set -> distinct static and instance maps keyed by function name

The owning `FoundryScript` is part of the identity. At a call site the VM first
resolves the #1118 owner-class FQCN to the exact root or nested class script,
then looks up the enum type, call kind, and function. Resolving the class before
the enum name prevents nested classes that declare identically named enums from
colliding.

Compiled functions are owned by this table and participate in every existing
function-lifetime path:

- source recompilation and clear
- partial compiled-bytecode link cleanup
- recursive lambda metadata cleanup
- hot-reload lambda replacement matching

The compiler uses `_parse_function(..., p_skip_member_register = true)`, then
stores the result in the enum table. Enum functions therefore reuse normal
argument, return, lambda, debug, and coroutine compilation without registering
as class members.

## Call instruction

The bytecode generator exposes an enum-call writer with these semantic inputs:

- target address
- evaluated base/receiver address
- evaluated argument addresses
- owner script path
- owner class FQCN
- enum type
- function name
- static or instance call kind
- synchronous, value-returning, or async form

The generated instruction stores only scalar global-name indices and addresses.
It never embeds an `FSFunction *`. This keeps live instructions stable across
reload and matches the analyzer's pointer-free tuple:

`(owner_script_path, owner_class, enum_type, function, call_kind)`

Three opcodes parallel the ordinary call family:

- enum call with no retained result
- enum call with a result
- enum async call that retains the coroutine handle

All live opcode consumers are updated together: the opcode enum, bytecode
generator, computed-goto and switch VM dispatch, verifier layout validation,
and disassembler. This is safety plumbing for live instructions, not enum
function-table persistence.

## VM lookup and invocation

At runtime the VM:

1. Recovers the scalar tuple from the instruction.
2. Uses the current root script when the owner path is the current script path;
   otherwise resolves the foreign owner script through `FSCache`.
3. Resolves `owner_class` to the exact owning root or nested `FoundryScript`.
4. Looks up `enum_type`, the static/instance map, and `function`.
5. Calls the resulting `FSFunction`.

An instance call evaluates the enum integer receiver and supplies that value as
the `FSFunction::call` self override, matching the existing conformance-witness
mechanism. A static call still evaluates its metatype base so evaluation order
and side effects stay consistent, but it does not bind `self`.

Normal `FSFunction::call` behavior handles default arguments, varargs, errors,
and coroutine states. The enum async opcode mirrors ordinary
`OPCODE_CALL_ASYNC`: it stores the returned handle and suppresses the debug
"called without await" guard. Awaited enum calls use the ordinary await
pipeline.

Every failed lookup returns an explicit runtime error that names the call kind
and stable owner tuple. The VM must never fall through to `Variant::callp` or
silently dispatch a same-named enum from another class.

## Compiler routing

The expression compiler intercepts a call only when
`CallNode::enum_call_kind` is static or instance. It evaluates arguments and
the base using the existing expression rules, selects the enum call opcode,
and passes through the #1118 metadata. Calls whose enum kind is `NONE` retain
the existing ordinary dispatch behavior, including the current Dictionary
fallback for metatype operations.

Enum functions are compiled for every class before that class is marked valid.
Nested classes recurse through the same path and populate only their own
tables. Cross-file calls do not copy or borrow a function pointer into the
caller; the VM resolves the target owner's table from the scalar key.

## Test strategy

Tests are added before implementation.

Source-runtime fixtures cover:

- same-file static and instance calls
- nested classes with identical enum names
- cross-file calls through a `.notest.fs` dependency
- async static and instance calls with `await`

C++ tests cover storage separation and lookup failure behavior, including
clear/recompile safety where practical. Existing analyzer tests continue to
prove the call-site metadata contract.

The fixtures carry an explicit `#skip-compiled-bytecode` directive. The test
runner honors it only for the compiled-bytecode round-trip pass and documents
that #1120 must remove the sentinel when enum function tables are serialized
and reloaded. The source-runtime pass still compiles and executes every
fixture.

## Scope boundary

This issue includes only the minimum instruction/verifier/disassembler work
needed for safe live execution. It does not include:

- enum function-table export or load
- enum function fixups in serialized bytecode
- compiled-bytecode corruption/hardening cases for enum tables
- removing the `#skip-compiled-bytecode` fixture sentinel
- reflection, completion, hover, go-to-definition, or other tooling surfaces

Those belong to #1120 and #1122 respectively.
