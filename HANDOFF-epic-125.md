# Handoff — Epic #125 (GDScript generics), CafecitoGames/godot

Worktree: `/Users/christian/CafecitoGames/godot/.worktrees/epic-125-generics`
Goal/skill: `/work-issue <n>` (skill `epic-125`) — TDD → build → adversarial Codex review to convergence → squash-merge PR with auto-merge against `develop`.

## Completed and merged this session (6 sub-issues)

| Issue | PR | What landed |
|---|---|---|
| #142 Generic traits | #331 | `trait Box[T]` + `uses Box[int]` conformance under substitution; transitive/diamond; conflicting-arg diamond rejected |
| #254 Validate direct T-member writes | #353 | `OPCODE_ASSIGN_TYPED_PARAMETER` validates direct/compound/field-init stores into `T` members |
| #294 Reify T-members through fixed base specialization | #365 | `TypeArgumentBinding {NONE/FIXED/OPEN}` re-resolved down the `extends` chain; `set()` + opcode branch on it; `FIXED` stored as `GDScriptDataType` to avoid CRTP self-ref leak |
| #305 create_proxy[T] through inherited specialization | #367 | per-ancestor binding table `type_parameter_bindings_by_ancestor`; `OPCODE_GET_TYPE_PARAMETER` lookup |
| #255 T-members under concrete base specialization | #368 | test-only; behavior already covered by #294 |
| #306 Retype generic typed-container returns | #370 | `CallNode::returns_erased_container` flag + `OPCODE_ASSIGN_TYPED_ARRAY_CONVERT` retypes erased `Array[T]` returns at the call site |

Follow-up filed: **#369** (generic method can't construct a `Dictionary[K,V]` in its body — blocks #306's Dictionary facet; indexing `Dictionary[K,V]` by a `K` key fails analysis).

All merged; each passed full suite (`1842` cases) + adversarial Codex review to convergence.

## DONE (not yet pushed) — #242 (carry reified type args through stored/aliased specialized class handles)

Final commit on `issue-242`: `9c7c99a0ee feat(gdscript): reify type args through stored/aliased specialized handles (#242)`. Full suite 1842 cases pass; Codex adversarial review converged (NO BLOCKING ISSUES) after one round (fixed: re-resolve via `main_script->find_class` instead of the global fqcn lookup that fabricates shallow scripts).

Two root causes found and fixed (the WIP handoff notes below were partly wrong — the var broadening never fired):
1. A stored/aliased handle's meta-type is **SCRIPT kind, not CLASS**, and an untyped `var h = Box[int]` is **weakly inferred** so `_gdtype_from_datatype(handle)` discards the whole type. Fix: broaden the trigger to `kind == CLASS || SCRIPT` with non-empty `type_arguments`, and read the reified args directly from the parser type's `type_arguments` (each arg is a hard explicit type).
2. A `const` handle folds to the analyzer's shallow `!valid` class; re-resolve to the live `main_script->find_class(fqcn)` at the call site. See memory `const-inner-class-folds-shallow-script`.

Fixtures: `runtime/features/generic_aliased_handle_construction.gd` (const + hard/weak var + create_proxy[T] + erasure-on-widening) and `runtime/errors/generic_aliased_handle_wrong_type.gd` (runtime member validation through aliased handle).

Remaining for #242: push `issue-242`, open squash auto-merge PR vs `develop`, move issue → Done, file any follow-ups.

### (original WIP notes, superseded)
### IN PROGRESS — #242 (carry reified type args through stored/aliased specialized class handles)

Design is settled on the issue as **mechanism (b)**: broaden `OPCODE_CONSTRUCT_SPECIALIZED` codegen to any callee base whose static `DataType.is_meta_type` has non-empty `type_arguments`; erasure-on-widening semantics; no new runtime representation.

**WIP commit on local branch `issue-242` (NOT pushed): `b8fd549cb4`.** Two changes in `modules/gdscript/gdscript_compiler.cpp`:
1. Value-subscript short-circuit in `_parse_expression` SUBSCRIPT case (~line 977): a specialized class meta-type used as a value (`Box[int]`) evaluates to the base class object (returns `_parse_expression(subscript->base)`), skipping the runtime index. This makes `var h = Box[int]` **compile** (previously "Identifier not found: int").
2. `.new()` trigger broadening (~line 793): when `subscript->base`'s static type is a meta-type CLASS with non-empty `type_arguments`, emit `OPCODE_CONSTRUCT_SPECIALIZED` with `base = subscript->base` and those args. Preserves the direct `Box[int].new()` path.

**State: both forms now CONSTRUCT, but reification is broken — DO NOT MERGE.**
- `var h = Box[int]; h.new()` — broadening fires (verified: compile-time `base_static.is_meta_type=1, kind=CLASS, type_arguments.size()=1`), but the instance is **not reified** (a dynamic wrong-typed member write is wrongly accepted). The `[int]` is lost between the handle's static type and the constructed instance. Prime suspect: `_gdtype_from_datatype(base_static, ...)` (default `p_handle_metatype=true`) may drop `type_arguments` for the inferred meta-type, OR the runtime `type_argument_count` reaching `OPCODE_CONSTRUCT_SPECIALIZED` differs from the direct form. NEXT STEP: add a temporary `print_line` of `type_argument_count` inside `OPCODE(OPCODE_CONSTRUCT_SPECIALIZED)` (gdscript_vm.cpp ~2076) and compare direct (`Box[int].new()`) vs var (`h.new()`) — run via the test harness (`--gdscript-generate-tests`), NOT `--script` (the latter doesn't call `test()`).
- `const IntBox = Box[int]; IntBox.new()` — reaches `_new_specialized` but fails its `if (!valid)` guard (error: "Nonexistent constructor 'new'"). The const's reduced value is a GDScript that is not `valid`. The analyzer sets `subscript->reduced_value = base->reduced_value` (the `Box` script via `find_class`) at `gdscript_analyzer.cpp` ~7944; that script object is evidently not the live/valid one at runtime. NEXT STEP: confirm whether the const reduction captures a shallow (uncompiled) `Box` script and, if so, resolve the const handle to the live class at the `.new()` site (use the same path the direct form uses for `specialization->base`), or make the value short-circuit also apply to a constant-folded handle.

Acceptance still to satisfy: `const IntBox = Box[int]; IntBox.new()` and `var h = Box[int]; h.new()` bind the SAME reified `type_arguments` as `Box[int].new()`; member-validation and `create_proxy[T]` identical to the direct form; runtime fixtures for both. Direct `Box[int].new()` must stay working (verified no regression so far).

## Remaining open epic #125 sub-issues (assessment)

- **#242** — in progress (above).
- **#240** Track reified script type args in typed-container element metadata — RISKY; an earlier attempt this epic broadly regressed `Array[Box[int]]` construction and was reverted. Needs coordinated VM+codegen change; route specialized object element types through `make_container_type_descriptor` in `get_container_type_pos` and honor `ContainerType::type_arguments` in compare/validate paths.
- **#286** Parser/analyzer: nullable + Callable-signature use-site type args — entangled with #129/#130/#172 (nullable marker parse + Callable-shape interpretation). Partly blocked.
- **#287** Multi-argument explicit application for `create_proxy[...]` — needs multi-arg construction support.
- **#137** Serialization: `.tres`/scene round-trip + hot-reload of reified instances — large, self-contained.
- **#139** Docs: GDScript reference + `doc/` updates — large, low-risk.
- **#140** Tests: analyzer/runtime/completion fixtures + C++ unit tests — large, consolidating.
- **#369** (follow-up) Dictionary[K,V] body construction — unblocks #306's Dictionary facet.

## Workflow / environment notes (IMPORTANT)

- Build: `scons platform=macos target=editor dev_mode=yes tests=yes -j12` → `bin/godot.macos.editor.arm64`. Incremental builds ~10–70s (shared SCons cache via gitignored `custom.py`). Do not clean.
- Full suite: `./bin/godot.macos.editor.arm64 --headless --test`. Filter script fixtures with `--test-case="*compilation*"` (NOT `*GDScript*`, which silently skips). Single-script iteration: `--headless --check-only --script <f.gd>`.
- **macOS misses Linux `-Werror=shadow`.** A clean local build can fail Linux CI; a lambda parameter shadowing an enclosing function parameter is a repeat offender. After pushing, watch `gh pr checks <n>`; on a shadow failure, rename + force-push. (Bit PR #331 — `record_trait_binding`'s `p_source`.)
- **`--gdscript-generate-tests` corrupts unrelated `.out` files on exit (macOS).** After generating, ALWAYS `git checkout -- modules/gdscript/tests/scripts` to restore tracked `.out`; your new untracked fixtures survive. Forgetting this looks like ~18 unrelated test failures (it's not a regression).
- **Codex review (`codex:codex-rescue`) hangs ~50% of the time** mid-investigation before emitting a verdict. Use a TIGHT verdict-only prompt from the start ("FAST, ≤3-4 reads, then STOP; no python/here-docs; end with exactly one line NO BLOCKING ISSUES / BLOCKING:<file:line>"). If the task output file is stale >5 min, kill and relaunch a fresh verdict-only agent (it completes in ~2-3 min). Run a fresh agent each round, pinned to HEAD.
- Project board: Experiment, project number `3`, owner `cafecito-games`, id `PVT_kwDODvOSms4Bbc4g`; Status field `PVTSSF_lADODvOSms4Bbc4gzhWNUKA` (In Progress=`47fc9ee4`, Todo=`f75ad846`, Done=`98236657`). Move issue → In Progress at start, → Done after merge.
- Branch per issue off `origin/develop`: `git checkout -B issue-<n> origin/develop`. PR base `develop`; enable squash auto-merge.

## Key runtime-generics architecture (for #242/#240)

- Instance reified args: `GDScriptInstance::type_arguments` (`Vector<ContainerType>`), set only by `_new_specialized` from the leaf's args.
- `GDScript::TypeArgumentBinding {kind NONE/FIXED/OPEN; GDScriptDataType fixed; int leaf_ordinal}` (gdscript.h) + slot-indexed `member_type_argument_bindings` + per-ancestor `type_parameter_bindings_by_ancestor`. Re-resolved one level per `extends` via `GDScriptCompiler::_specialize_type_argument_binding`.
- LEAK TRAP (see memory `containertype-local-class-strong-ref-leak`): never persist a baked `ContainerType` with a script ref in member metadata — `to_container_type()` rebuilds a strong ref even for local classes (which `_gdtype_from_datatype` deliberately avoids), causing CRTP self-ref leaks that LeakSanitizer CI catches. Store `GDScriptDataType` and convert to a temporary `ContainerType` at use.
- `OPCODE_CONSTRUCT_SPECIALIZED` (gdscript_vm.cpp ~2076): instr args `[ctor args..., type-arg descriptors..., base script, target]`; `Ref<GDScript> = *base`; `gdscript->_new_specialized(args, argc, type_arguments, err)`.

Auto-memory at `/Users/christian/.claude/projects/-Users-christian-CafecitoGames-godot/memory/` has the workflow/leak/codex notes; they load automatically next session.
