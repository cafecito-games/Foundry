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

- [ ] Add a `[SceneTree][Rendering] Renderer shutdown drains owned RIDs` doctest that creates a viewport, camera, scenario, mesh, and instance through `RenderingServer`, attaches the viewport/camera and instance/mesh/scenario relationships, and synchronizes the server.
- [ ] Include `servers/rendering/renderer_scene_cull.h`, `servers/rendering/renderer_viewport.h`, and `servers/rendering/rendering_server_globals.h` so the test can inspect the concrete renderer owners.
- [ ] Call `RSG::viewport->finalize()` followed by `static_cast<RendererSceneCull *>(RSG::scene)->finalize()` and assert that the viewport owner and all scene owners report zero RIDs.
- [ ] Free the standalone mesh RID after the owner assertions so the test leaves storage resources clean for the listener teardown.
- [ ] Include the new test header beside `test_shader_preprocessor.h` in `tests/test_main.cpp`.
- [ ] Build and run the focused test before implementing the lifecycle hooks; the expected red result is that the new finalization API is not yet available, establishing the test contract before production behavior is added.

### Task 2: Add ordered finalization hooks

**Files:**
- Modify: `servers/rendering/rendering_method.h`
- Modify: `servers/rendering/renderer_scene_cull.h`
- Modify: `servers/rendering/renderer_scene_cull.cpp`
- Modify: `servers/rendering/renderer_viewport.h`
- Modify: `servers/rendering/renderer_viewport.cpp`

- [ ] Add `virtual void finalize() = 0;` to `RenderingMethod` next to its existing `free(RID)` lifecycle API and override it in `RendererSceneCull`.
- [ ] Implement `RendererSceneCull::finalize()` by taking owned-RID snapshots and freeing in dependency order: scenarios first (detaching their instances), instances second (releasing geometry instances and instance resources), and cameras last.
- [ ] Report each non-empty owner snapshot with the existing `WARN_PRINT(vformat(... RIDs ...))` shutdown-leak style before routing RIDs through `RendererSceneCull::free()`.
- [ ] Add `RendererViewport::finalize()` and drain a snapshot of `viewport_owner` through `RendererViewport::free()`, retaining render-target, shadow-atlas, canvas-map, scenario, and occlusion cleanup.
- [ ] Keep finalization idempotent: a second call sees empty owners and performs no work.

### Task 3: Wire finalization into server shutdown

**File:** `servers/rendering/rendering_server_default.cpp`

- [ ] At the start of `_finish()`, call `RSG::viewport->finalize()` while canvas, scene, storage, and rasterizer objects are still alive.
- [ ] Call `RSG::scene->finalize()` immediately after viewport finalization so viewport scenario references are already detached but geometry instances can still call the live scene renderer.
- [ ] Leave canvas finalization and rasterizer destruction after these hooks, preserving the existing backend teardown order and the issue #1176 SelfList safeguard.

### Task 4: Verify the fix and regression coverage

- [ ] Build with `python3 scripts/agent_build.py --dev-build` for the first focused iteration.
- [ ] Run `DISPLAY=:1 ./bin/foundry.* --headless test run --case "*Renderer shutdown drains owned RIDs*" --force-colors` and confirm the new test passes without owner leaks.
- [ ] Rebuild without the temporary fast-iteration flag using `python3 scripts/agent_build.py`.
- [ ] Run the focused rendering test again and run the relevant editor/Foundry test filters; capture any expected renderer leak diagnostics separately from doctest status.
- [ ] Run the full suite with `DISPLAY=:1 ./bin/foundry.* --headless test run --progress-format=jsonl --progress-file /tmp/foundry-issue-1176-test-progress.jsonl --force-colors` and use the doctest success summary as the pass criterion.
- [ ] Inspect `git diff --check`, `git status`, and the final diff to ensure only the issue fix, regression test, and plan are included.

### Task 5: Review and hand off

- [ ] Review the implementation against the approved spec and issue symptoms, checking shutdown ordering, all owner types, idempotency, and preservation of existing leak diagnostics.
- [ ] Commit the implementation and test changes with a focused imperative subject.
- [ ] Report the root cause, changed files, verification evidence, and any remaining high-level editor ObjectDB/resource warnings that are outside renderer-owner cleanup.
