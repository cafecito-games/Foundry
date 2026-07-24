# Renderer Shutdown Resource Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drain renderer-owned viewport, scenario, instance, and camera RIDs before the scene renderer and its storage backends are destroyed, preventing leaked geometry instances and shutdown-time scene shader aborts for issue #1176.

**Architecture:** Add explicit finalization hooks to the scene culling method and viewport manager. Each hook snapshots its `RID_Owner` contents and routes every RID through the existing `free()` path, preserving dependency cleanup and leak diagnostics. Invoke viewport finalization first, then scene finalization, before canvas and rasterizer finalization in `RenderingServerDefault::_finish()`.

**Tech Stack:** C++17 Godot/Foundry renderer lifecycle code, `RID_Owner`, doctest renderer integration tests, SCons agent build wrapper.

---

### Task 1: Add the renderer shutdown regression test

**Files:**
- Create: `tests/servers/rendering/test_renderer_shutdown.h`
- Modify: `tests/test_main.cpp`

- [x] Add a `[Rendering] Renderer shutdown drains owned RIDs` doctest that creates an isolated rendering server, then creates a viewport, camera, scenario, surface-backed mesh, and instance through `RenderingServer`, attaches the viewport/camera and instance/mesh/scenario relationships, and synchronizes the server.
- [x] Include `servers/rendering/renderer_scene_cull.h`, `servers/rendering/renderer_viewport.h`, and `servers/rendering/rendering_server_globals.h` so the test can inspect the concrete renderer owners.
- [x] Assert that the viewport owner and all scene owners contain the expected RIDs, invoke the ordered finalization hooks, assert that every owner is drained, then create a second owner set and call only `RenderingServerDefault::finish()` under a renderer-leak detector to cover production shutdown wiring.
- [x] Include the new test header beside `test_shader_preprocessor.h` in `tests/test_main.cpp`.
- [x] Build and run the focused test before implementing the lifecycle hooks; the expected red result was the missing finalization API, establishing the test contract before production behavior was added.

### Task 2: Add ordered finalization hooks

**Files:**
- Modify: `servers/rendering/renderer_scene_cull.h`
- Modify: `servers/rendering/renderer_scene_cull.cpp`
- Modify: `servers/rendering/renderer_viewport.h`
- Modify: `servers/rendering/renderer_viewport.cpp`

- [x] Add `RendererSceneCull::finalize()` as a concrete renderer lifecycle hook; keep the generic `RenderingMethod` interface unchanged because `RSG::scene` is constructed as `RendererSceneCull` by `RenderingServerDefault`.
- [x] Implement `RendererSceneCull::finalize()` by taking owned-RID snapshots and freeing in dependency order: scenarios first (detaching their instances), instances second (releasing geometry instances and instance resources), and cameras last.
- [x] Report each non-empty owner snapshot with the existing `WARN_PRINT(vformat(... RIDs ...))` shutdown-leak style before routing RIDs through `RendererSceneCull::free()`.
- [x] Add `RendererViewport::finalize()` and drain a snapshot of `viewport_owner` through `RendererViewport::free()`, retaining render-target, shadow-atlas, canvas-map, scenario, and occlusion cleanup.
- [x] Keep finalization idempotent: a second call sees empty owners and performs no work.

### Task 3: Wire finalization into server shutdown

**File:** `servers/rendering/rendering_server_default.cpp`

- [x] At the start of `_finish()`, call `RSG::viewport->finalize()` while canvas, scene, storage, and rasterizer objects are still alive.
- [x] Call `static_cast<RendererSceneCull *>(RSG::scene)->finalize()` immediately after viewport finalization so viewport scenario references are already detached but geometry instances can still call the live scene renderer.
- [x] Leave canvas finalization and rasterizer destruction after these hooks, preserving the existing backend teardown order and the issue #1176 SelfList safeguard.

### Task 4: Verify the fix and regression coverage

- [x] Build with `python3 scripts/agent_build.py --dev-build` for the first focused iteration.
- [x] Run `DISPLAY=:1 ./bin/foundry.* --headless test run --case "*Renderer shutdown drains owned RIDs*" --force-colors` and confirm the new test passes through the production shutdown path without GeometryInstance or scene-shader leak diagnostics.
- [x] Rebuild without the temporary fast-iteration flag using `python3 scripts/agent_build.py`; the strict `dev_mode=yes`/`-Werror` build completed successfully.
- [x] Run the focused rendering test again and inspect the editor/Foundry coverage; the new test passes with 15/15 assertions and no GeometryInstance or scene-shader leak diagnostics, while the full suite's only failure is the unrelated `split-scene-root-button-context workflow subprocess` editor test.
- [x] Run the full suite with `DISPLAY=:1 ./bin/foundry.* --headless test run --progress-format=jsonl --progress-file /tmp/foundry-issue-1176-test-progress.jsonl --force-colors`; it reached 3,099 passed, 1 unrelated failure, and 3 skipped tests. The renderer-owned Scenario, Instance, Camera, GeometryInstance, and scene-shader leak categories were absent from shutdown diagnostics.
- [x] Inspect `git diff --check`, `git status`, and the final diff to ensure only the issue fix, regression test, and plan are included; generated editor fixture state was removed after the suite run.

### Task 5: Review and hand off

- [ ] Review the implementation against the approved spec and issue symptoms, checking shutdown ordering, all owner types, idempotency, and preservation of existing leak diagnostics.
- [ ] Commit the implementation and test changes with a focused imperative subject.
- [ ] Report the root cause, changed files, verification evidence, and any remaining high-level editor ObjectDB/resource warnings that are outside renderer-owner cleanup.
