# DAP/tooling host topology

Status: accepted decision, implementation deferred to a follow-up child issue.
Parent epic: #1418. Investigation issue: #1427.

This document records the investigation that selected the process topology for
FoundryScript editor tooling (language server plus debug adapter), the evidence
behind that selection, the rejected alternatives, and the contract an
implementation must satisfy. It does not add or change engine behavior.

## 1. Decision

Foundry ships **one combined full-editor tooling host that owns both LSP and
DAP**, started through a single command-first entry point:

```sh
foundry tooling serve --project <project> [--lsp-port <port>] [--dap-port <port>]
```

For an IDE-managed host that must not collide with anything else on the machine:

```sh
foundry tooling serve --project <project> --lsp-port 0 --dap-port 0
```

No standalone `dap serve` command is added. `lsp serve` may remain temporarily as
a deprecated alias onto the same combined host, but `tooling serve` is the only
documented editor-integration entry point. A lightweight editor-independent DAP
daemon is out of scope for v1.

## 2. What the code actually does today

### 2.1 Both servers are unconditional editor plugins

`DebugAdapterServer` is registered for every editor process except recovery mode:

- `editor/register_editor_types.cpp:236-238` adds `DebugAdapterServer` via
  `EditorPlugins::add_by_type<DebugAdapterServer>()`.
- `FSLanguageServer` is instantiated and added with
  `EditorNode::get_singleton()->add_editor_plugin(lsp_plugin)` in
  `modules/foundry_script/register_types.cpp:128-132`, guarded only by
  `TOOLS_ENABLED` and `FOUNDRY_SCRIPT_NO_LSP`.

Both bind as soon as they enter the tree or the editor becomes ready:

- `DebugAdapterServer::_notification` calls `start()` on
  `NOTIFICATION_ENTER_TREE` (`editor/debugger/debug_adapter/debug_adapter_server.cpp:46-52`),
  and `start()` listens on `remote_port`, default `6006`
  (`debug_adapter_server.cpp:81-88`, `debug_adapter_server.h:41`).
- `FSLanguageServer::_notification` calls `start()` from
  `NOTIFICATION_INTERNAL_PROCESS` once `EditorNode::is_editor_ready()` returns
  true (`modules/foundry_script/language_server/fs_language_server.cpp:57-63`),
  and `start()` listens on `port`, default `6005`
  (`fs_language_server.cpp:96-110`, `fs_language_server.h:47-50`).

### 2.2 `lsp serve` is already a full editor process

`Kind::LSP_SERVE` sets `editor = true` and only applies
`FSLanguageServer::port_override` (`main/main.cpp:805-814`). It does not disable
any other plugin. Consequently a process started as `foundry lsp serve` also
constructs `DebugAdapterServer` and takes port 6006. Symmetrically, a
hypothetical `dap serve` process would still start the language server and take
port 6005. **Two separate tooling processes therefore contend for each other's
listener**, which is the collision the epic asked this investigation to resolve.

`Kind::LSP_SERVE` also does not force `NULL_DISPLAY_DRIVER`, unlike the
`cmdline_tool` cases nearby (`main/main.cpp:775-800`). Today `foundry lsp serve`
opens a windowed editor unless the caller adds `--headless`. That is why
`tooling serve` must imply headless operation rather than relying on the caller.

### 2.3 DAP is structurally bound to the editor

The debug adapter is not a thin protocol shim over an engine service. Its
request handlers reach directly into editor singletons:

- `ScriptEditor::get_singleton()->get_breakpoints(...)`
  (`editor/debugger/debug_adapter/debug_adapter_parser.cpp:147`).
- `EditorDebuggerNode::get_singleton()->get_default_debugger()` for breakpoint
  clearing, stack dumps, stepping, and message dispatch
  (`debug_adapter_parser.cpp:156`, `:204`, `:249`, `:439`, `:483`, `:490`, `:526`).
- `EditorRunBar::get_singleton()` for launch, stop, and pause
  (`debug_adapter_parser.cpp:164`, `:214-218`, `:276`, `:292`, `:301`).
- `EditorExport` for non-host platforms (`debug_adapter_parser.cpp:220-235`).

Launching a debuggee goes through `EditorRunBar` into `EditorRun::run()`, which
builds an argument vector containing `project run --project <resource_path>`,
`--remote-debug <uri>`, and `--editor-pid <pid>`
(`editor/run/editor_run.cpp:50-72`) and tracks the spawned PIDs in `pids`
(`editor_run.cpp:182-186`), killable through `stop_child_process()` and `stop()`
(`editor_run.cpp:216-230`).

Extracting any of this into an editor-independent daemon means reimplementing or
relocating `EditorDebuggerNode`, `EditorRunBar`, `EditorRun`, and the script
editor's breakpoint store. That is a large refactor that solves no v1
requirement, so the epic's non-goal stands: **no separate daemon**.

## 3. Alternatives considered and rejected

### 3.1 Separate `lsp serve` and `dap serve` processes

Rejected. As shown in 2.1 and 2.2, both plugins start in any editor process, so
two hosts double-bind. Making this work would require a plugin-suppression
mechanism (start the editor with the other plugin disabled), which adds a new
startup mode, doubles headless editor cost (two full editor processes, two
project imports, two parse caches), and gives the IDE two lifecycles to
supervise for one project. It also duplicates FoundryScript analysis state
across processes with no shared cache.

### 3.2 Editor-independent lightweight DAP daemon

Rejected for v1. See 2.3: the adapter's control surface is the editor. The
epic explicitly permits this only if the investigation proves it necessary; it
does not. Every defect found in section 5 is fixable inside the full-editor host.

### 3.3 Keep `lsp serve` as the only command and let DAP ride along implicitly

Rejected. The DAP port would be untyped in the command surface: a caller could
not request a specific or ephemeral DAP port, could not learn the bound DAP
port, and would silently get a listener it never asked for. The readiness
contract in section 4 requires both ports to be explicit and reported.

### 3.4 A single-process host that shares one port

Rejected. LSP and DAP are distinct wire protocols with distinct clients and
distinct client lifetimes; multiplexing them would require a Foundry-specific
framing layer that no standard IDE client speaks.

## 4. Command and lifecycle contract

### 4.1 Startup

- `--project` is required and canonicalized before any service starts.
- `tooling serve` implies headless operation; callers need no `--headless` flag.
- LSP and DAP bind strictly to `127.0.0.1`. This is stricter than today's LSP,
  whose host comes from the `network/language_server/remote_host` editor setting
  (`fs_language_server.cpp:96-98`); the tooling host must ignore that setting.
- Omitted ports use LSP 6005 and DAP 6006, matching the current defaults.
- Ports are strict decimal integers in `[0, 65535]`. Port `0` requests an
  ephemeral port. Nonnumeric, negative, and out-of-range values are rejected.
  Note that today's `lsp serve` parsing uses `String::to_int()` and silently
  accepts garbage as `0` (`main/main.cpp:808-812`); the new command must not.
- Identical explicit nonzero LSP and DAP ports are rejected before either
  listener starts.
- Startup is atomic. If either listener fails to bind, close the other, emit no
  readiness line, and exit nonzero.

`Ref<TCPServer>` already supports both requirements: `listen()` accepts port `0`
and `get_local_port()` reports the bound port (`core/io/tcp_server.h:44-45`).
`EditorAutomationServer` is the precedent for this whole shape — ephemeral port
request, readback, loopback binding, machine-readable readiness line, and
fail-fast on bind error (`editor/automation/editor_automation_server.cpp:200-216`,
`:219-247`, `:249-295`). The tooling host should follow it rather than invent a
new convention.

### 4.2 Readiness record

Emitted exactly once, after the project is canonicalized, the editor is ready,
and both listeners have bound:

```text
FOUNDRY_TOOLING {"project":"/canonical/project","pid":1234,"local_only":true,"services":["lsp","dap"],"lsp_port":49152,"dap_port":49153}
```

The reported ports are always the actual bound ports, including when `0` was
requested. `local_only: true` covers every service in `services`, and any debug
transport this host starts must also stay loopback-only.

### 4.3 Failure record

```text
FOUNDRY_TOOLING_ERROR {"error":"bind_failed","service":"dap","requested_port":6006,"error_code":<code>,"message":"<detail>"}
```

Human-readable detail also goes to stderr, mirroring
`EditorAutomationServer::_fail_startup`
(`editor/automation/editor_automation_server.cpp:219-247`).

### 4.4 Process ownership and shutdown

The IDE or invoking supervisor owns the host process.

- DAP `terminate` stops only the debuggee.
- LSP or DAP client disconnection does not stop the host.
- Orderly shutdown through `SIGINT` or `SIGTERM` closes both listeners and
  terminates every child launched by that host. `EditorRun` already tracks those
  PIDs (`editor/run/editor_run.cpp:182-186`, `:216-230`); the gap is that no
  signal path invokes that cleanup.
- Externally attached debuggees are not terminated with the host.
- Abrupt uncatchable termination cannot guarantee child cleanup.

### 4.5 Project identity

A DAP launch project must be **exactly equal** to the host's canonical project.
Today `DebugAdapterParser::is_valid_path` accepts any path that merely
`begins_with` the resource path, and on Windows any path that `containsn` it
(`editor/debugger/debug_adapter/debug_adapter_parser.h:46-53`), used by both
`launch` (`debug_adapter_parser.cpp:172-176`) and `setBreakpoints`
(`debug_adapter_parser.cpp:361-366`). Prefix and substring matching are not
sufficient for the launch project check and must be replaced with canonical
equality. Source-file paths still legitimately live under the project root, so
the containment test remains appropriate for `setBreakpoints`.

## 5. Baseline DAP defects found

These are defects of the existing full-editor DAP implementation. They must be
fixed and covered end to end by the implementation child. None of them argues
for a different topology.

1. **Bind failure is silent.** `DebugAdapterServer::start()` ignores the result
   of `protocol.start()` unless it is `OK`
   (`debug_adapter_server.cpp:81-88`): a second host on an occupied port stays
   alive with no listener and no diagnostic. Same shape in
   `FSLanguageServer::start()` (`fs_language_server.cpp:100-110`).

2. **Breakpoint registration is routed through the script editor UI.**
   `setBreakpoints` reaches `DebugAdapterProtocol::update_breakpoints`, which
   calls `ScriptEditorDebugger::_set_breakpoint`
   (`debug_adapter_protocol.cpp:988-1010`). The full chain that has to complete
   before a breakpoint is real is:

   `ScriptEditorDebugger::_set_breakpoint` loads the script and emits
   `set_breakpoint` (`editor/debugger/script_editor_debugger.cpp:1219-1223`) →
   `EditorDebuggerNode::_breakpoint_set_in_tree` re-emits
   `breakpoint_set_in_tree` (`editor/debugger/editor_debugger_node.cpp:897-903`)
   → `ScriptEditorController::_set_breakpoint` applies it **only if that script
   is already open in a script editor tab**
   (`editor/script/script_editor_controller.cpp:1103-1116`) →
   `ScriptTextEditor::set_breakpoint` toggles the gutter
   (`editor/script/script_text_editor.cpp:2297`) →
   `ScriptTextEditor::_breakpoint_toggled` finally calls
   `EditorDebuggerNode::set_breakpoint`
   (`editor/script/script_text_editor.cpp:1303-1307`), which is the only place
   that stores into `EditorDebuggerNode::breakpoints` and emits
   `breakpoint_toggled` (`editor_debugger_node.cpp:697-704`) — the signal
   `DebugAdapterProtocol::on_debug_breakpoint_toggled` listens for
   (`debug_adapter_protocol.cpp:1068-1071`, connected at `:1248`), and the store
   that is replayed to a session on connect
   (`editor_debugger_node.cpp:429-433`).

   In a headless tooling host no script tab is open, so the chain breaks at
   `ScriptEditorController::_set_breakpoint` and nothing ever reaches
   `EditorDebuggerNode`. `update_breakpoints` then finds no entry in
   `breakpoint_list` and bails through `ERR_FAIL_NULL_V`
   (`debug_adapter_protocol.cpp:1006-1009`), and any breakpoint that does enter
   the list is marked `verified = true` unconditionally
   (`debug_adapter_protocol.cpp:1070`) regardless of whether the debuggee
   accepted it. The fix is to register DAP breakpoints directly through the
   authoritative `EditorDebuggerNode::set_breakpoint` entry point instead of the
   UI-origin signal round trip, and to derive `verified` from the real result.

3. **Manual pause yields an empty stack trace**, so `scopes`, `variables`,
   `evaluate`, and `continue` cannot be exercised. Pause goes through
   `EditorRunBar::get_singleton()->get_pause_button()->set_pressed(true)` plus
   `EditorDebuggerNode::_paused()` (`debug_adapter_parser.cpp:292-293`), a
   UI-widget-mediated path that must be reworked to a headless-safe call and
   then covered end to end together with `request_stack_dump`
   (`debug_adapter_parser.cpp:439`).

4. **Optional `Source.checksums` triggers a dictionary lookup warning.**
   `DAP::Source::from_json` unconditionally reads `p_params["checksums"]`
   (`editor/debugger/debug_adapter/debug_adapter_types.h:85-90`) even though the
   property is optional in the DAP specification. It must be read only when
   present.

5. **Interrupting the host orphans the debuggee.** See 4.4.

Items 2, 3, and 5 are the reason the epic's breakpoint and inspection
requirements are **not accepted on the current implementation**.

## 6. Selected-test debugging

Selected-test debugging is included in the v1 contract, gated on #1428. The
extension must not advertise a Test Explorer debug profile until the end-to-end
launch contract passes.

The ordinary (non-debug) runner transport already works and preserves exact
repeated selections, because `--` splits user arguments before command parsing
(`main/cli_parser.cpp:1187-1197`) and `project test` forwards the runner path
into the script-runner host (`main/main.cpp:4169-4171`):

```sh
foundry project test \
  --project <project> \
  --runner <runner> \
  -- adapter run \
  --protocol-version <version> \
  --report <report> \
  --select <stable-id> \
  --select <stable-id>
```

Debugging that same selection is not reachable today:

- The DAP launch path always builds `project run`, never `project test`
  (`editor/run/editor_run.cpp:57-64`).
- DAP `playArgs` are forwarded as run arguments
  (`debug_adapter_parser.cpp:188-198`, `:210-218`), so they cannot load the test
  runner.
- `project test` accepts only `--project`, `--runner`, and common global
  options; anything else, including `--remote-debug`, fails parsing
  (`main/cli_parser.cpp:374-407`).

Selected-test debugging therefore requires an explicit structured DAP launch
kind rather than argument smuggling:

```json
{
  "project": "/canonical/project",
  "noDebug": false,
  "foundry/launch": {
    "kind": "project_test",
    "runner": "res://addons/example/run.fs",
    "adapter": {
      "protocolVersion": 1,
      "report": "/unique/scratch/report.tap",
      "testIds": ["stable-id-1", "stable-id-2"]
    }
  }
}
```

Requirements for that launch kind:

- The host constructs an argument vector directly and never a shell command.
- Engine-owned debug transport and editor PID arguments (`--remote-debug`,
  `--editor-pid`, as in `editor/run/editor_run.cpp:65-72`) stay before `--`;
  only adapter arguments appear after it.
- The launch must work in a project with no main scene.
- The runner's `0`/`1`/`2` exit semantics are preserved, with TAP as the
  authoritative test report.

## 7. Thread-safety note

`FSLanguageProtocol` has a tracked pre-existing thread-safety gap (fingerprint
`lsp-parse-results-unsynchronized`). It is relevant context for a single-host
topology because the language server can poll on a dedicated thread when
`network/language_server/use_thread` is enabled
(`fs_language_server.cpp:86-94`, `:103-107`; the setting defaults to `false`,
`fs_language_server.h:47`). The combined host does not make that gap worse — it
is the same process and the same polling model that `lsp serve` uses today — but
an implementation that enables threaded polling by default would. The tooling
host should keep the existing default and leave the gap to its own fix.

## 8. Implementation follow-up

This investigation is complete when the decision above is recorded and a child
implementation issue exists. The child is titled **Add a combined command-first
tooling host for LSP and DAP**, with #1428 as a dependency for its selected-test
phase, and carries the acceptance criteria from sections 4, 5, and 6:

- Add `tooling serve` with command help, JSON help, strict project and port
  validation, and compatibility handling for `lsp serve`.
- Force headless, loopback-only operation.
- Coordinate both listeners, support ephemeral ports, and emit the readiness and
  error records.
- Fail atomically on either bind failure and release all acquired ports.
- Enforce exact canonical-project equality for DAP launch.
- Fix optional `Source.checksums` handling.
- Fix and test breakpoint hit, threads, stack trace, scopes, variables,
  evaluate, continue, and terminate.
- Clean up launched children on orderly host shutdown while preserving
  externally attached processes.
- Add the structured `project_test` launch kind after #1428 defines the runner
  protocol.
- Verify two exact selected IDs, unknown IDs, unsupported protocol versions,
  projects without a main scene, report output, exit semantics, termination,
  restart, and scene-launch regression.
- Keep the extension debug profile disabled until all selected-test DAP
  acceptance tests pass.

Tests must exercise protocol and subprocess behavior using the shared test
scratch space. Source-text assertions are not acceptable coverage.

## 9. Non-goals

- A separately launched `dap serve` process.
- An editor-independent DAP daemon.
- Advertising selected-test debugging before the runner-debug contract is
  accepted.
- Treating a separately launched `project test` process as automatically
  attached to the editor debugger.
