# Renderer Shutdown Resource Cleanup

## Context

Issue #1176 reported a Forward+ editor abort while shutting down. The immediate
`SelfList` assertion is already guarded by commit `2bff0fae54`, but the original
log also reported leaked Canvas/CanvasItem RIDs, geometry-instance allocator pages,
and scene shader versions.

The renderer shutdown path currently destroys the rasterizer before deleting the
scene culling and viewport services:

1. `RenderingServerDefault::_finish()` finalizes and deletes the canvas renderer.
2. `RendererCompositorRD::finalize()` deletes the scene renderer and its Forward+
   geometry/shader state, then the other rendering storages.
3. `RendererViewport` and `RendererSceneCull` are deleted afterward.

The higher-level services therefore cannot release remaining RIDs through the
live renderer during shutdown. `RendererSceneCull::~RendererSceneCull()` also
only resets temporary cull-result arrays; it does not drain its camera, scenario,
or instance owners. A surviving geometry instance consequently remains allocated
when `RenderForwardClustered` destroys its `PagedAllocator` and may keep material
and shader resources alive.

## Goals

- Release remaining scene-owned resources while the renderer and storage services
  are still valid.
- Release remaining viewport-owned resources before the renderer storage backing
  their render targets and shadow atlases is destroyed.
- Preserve leak diagnostics for genuinely leaked higher-level objects and avoid
  replacing the assertion with silent cleanup.
- Keep normal runtime destruction paths unchanged.
- Add a regression test that exercises shutdown with live scene/viewport RIDs and
  proves the cleanup path runs before renderer teardown.

## Non-goals

- Rework editor `ObjectDB`/`Ref` lifetime leaks unrelated to renderer-owned RIDs.
- Suppress existing leak warnings by changing the leak-reporting helpers.
- Change the already-landed `SceneShaderForwardClustered`/`SceneShaderForwardMobile`
  `SelfList` safeguard except where the new cleanup makes it unnecessary.

## Design

Add explicit finalization methods to the renderer services that own top-level RIDs.
Each method snapshots its owner RIDs and routes them through the existing `free()`
or equivalent cleanup path, so dependency unpairing and backend-specific cleanup
remain centralized.

- `RendererSceneCull::finish()` drains camera, scenario, and instance owners in a
  dependency-safe order. Freeing scenarios first detaches their instances; any
  remaining instances then release their geometry instances through
  `scene_render->geometry_instance_free()` while the Forward+ renderer is alive.
- `RendererViewport::finish()` drains all viewport RIDs while texture and light
  storage remain available, releasing the render targets and shadow atlases owned
  by each viewport.
- `RenderingServerDefault::_finish()` calls these methods before canvas/rasterizer
  finalization, then follows the existing destruction order.

The methods are shutdown-only and idempotent. They must not call back into a
service after that service has been finalized. Owner enumeration uses the existing
`RID_Owner::get_owned_list()` snapshot API so freeing an item cannot invalidate the
iteration.

Canvas and CanvasItem warnings will be evaluated separately during verification.
`RendererCanvasCull::finalize()` already frees those RIDs safely; if a remaining
warning is caused by an editor object surviving `OS::delete_main_loop()`, it is an
editor object-lifetime issue rather than a renderer teardown ordering issue and is
outside this focused change.

## Error handling

Cleanup follows existing `free()` error conventions. Invalid or already-freed RIDs
are ignored by the owner snapshot path, and cleanup continues for the remaining
owners. No new hard assertions are added to shutdown.

## Testing

Add a focused renderer shutdown regression test that:

1. Creates the rendering server with the Forward+ renderer where supported.
2. Allocates a scenario, a geometry instance, and a viewport without freeing the
   high-level RIDs.
3. Calls the shutdown cleanup path while the renderer is still alive.
4. Verifies that the owners are drained and that renderer finalization no longer
   reports the corresponding geometry-instance allocator leak or shader-list
   failure.

Retain the existing editor automation workflow as an end-to-end smoke check, and
run the focused rendering tests plus the full test suite before completion.

