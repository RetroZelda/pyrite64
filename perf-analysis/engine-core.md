# Engine Core Performance Analysis

**Date:** 2026-07-06

## Executive Summary

The Pyrite64 engine core is architecturally lean — component dispatch runs through a data-driven `COMP_TABLE[16]` function-pointer table instead of C++ virtuals, draw work is recorded into per-layer rspq queues, object matrices come from a pooled uncached allocator, and the swapchain is triple-buffered. Despite that, the core leaks a fixed, always-compiled per-frame CPU tax on VR4300 hardware. The single confirmed high-impact cost is unconditional per-frame SRT matrix rebuilds into uncached RDRAM for static models (~3-4us each, ~100-200us on a 50-static-model scene, plus stolen RDP bandwidth). A tier of medium wins scales with object count: always-on `unordered_map` timing instrumentation in the update and draw hot loops (~0.3-0.5ms on heavy scenes, with a hash DIVU and per-frame node malloc/free), full-object scans in the fixed-update loop, all runtime-spawned object ids bypassing the fast `idLookup` table and falling to O(n) scans, whole-scene rescans on any visibility toggle, and fragmented per-object heap allocation that costs a D-cache miss per object per pass. Several low-impact cleanups round it out. Every recommendation preserves observable behavior and determinism, none touches the parity-locked `particleSim.h`, and most are small localized edits measurable with the existing `ovlCPU`/`ovlComponents` debug overlay.

## Architecture & Frame Cost Model

**Frame flow.** `main()` loops `SceneManager::run()` → `Scene::update(dt)` until a scene change (sceneManager.cpp:36-49). `Scene::update` (scene.cpp:170-350) runs per frame: joypad poll, `AudioManager::update`, lighting reset, pending component ready/init, prefab spawn queue, object active/visible state transitions gated by `needsObjStateUpdate`, event-queue dispatch, a fixed-timestep loop (per-object component `fixedUpdate` + `Coll::step`, up to `MAX_PHYSICS_STEPS=5` on catch-up), physics render-interpolation, the main per-object component `update` dispatch, camera updates, deferred object deletion, and finally `VI::SwapChain::nextFrame()` which blocks until a framebuffer frees and synchronously invokes the pipeline draw task (pipelineDefault.cpp:37-46) → `rdpq_attach` → `Scene::draw`. `Scene::draw` (scene.cpp:352-449) iterates cameras; per camera it attaches the t3d viewport, then per enabled object iterates components calling `compDef.draw`; a `Culling` component may set `ObjectFlags::IS_CULLED` mid-loop to break out of the remaining comps, and the flag is reset per object after each camera.

**Data model.** An `Object` is a single `memalign` allocation (sceneLoader.cpp:108) holding a ~48-byte header (id/group/flags/compCount + quat/pos/scale), a `CompRef` table `{type,flags,offset}`, and inline component data. Dispatch is data-driven through the const `COMP_TABLE[16]` (componentTable.cpp) built by SFINAE detection — no virtuals. Every component type statically defines `update()`, some as empty stubs (`Model`, `Culling`), and `update` is called unconditionally (scene.cpp:310); `draw`/`fixedUpdate`/`onEvent` are null-checked. `CompRef.flags` is written `0` at load (sceneLoader.cpp:141) and read nowhere.

**Per-object / per-frame passes.** The `std::vector<Object*>` is walked by at least four passes each frame — the fixed-update scan (scene.cpp:271), the update dispatch (scene.cpp:299), the per-camera draw dispatch (scene.cpp:380), and the state/child scans (scene.cpp:250, iterObjectChildren scene.h:260-265). Because objects are individually heap-allocated, each header read is a compulsory D-cache miss on the 8KB direct-mapped cache (~40-70cy).

**Matrices.** `MatrixManager` (matrixManager.cpp) owns a static pool of 576 `T3DMat4FP` (36KB BSS) handed out as **uncached** KSEG1 pointers (`bufferPtr = unached(buffer)`, matrixManager.cpp:22) via a bitmask allocator. `Model`/`AnimModel` each hold a 3-deep `RingMat4FP`; `Model::draw` rebuilds the s16.16 matrix from SRT every frame (model.cpp:163-164) via `t3d_mat4fp_from_srt` = quaternion→float 4x4 + scale + float→fixed conversion + 8 uncached 64-bit writes. Cameras use tiny3d buffered viewports; `Camera::update` recomputes projection + view*proj + frustum every frame (camera.cpp:25-29).

**Existing instrumentation.** `Scene` keeps `ticksActorUpdate`/`ticksGlobalUpdate`/`ticksDraw`/`ticksGlobalDraw` plus per-component-type and per-script `std::unordered_map<uint8_t, ComponentTicks>` counters, filled in the update **and** draw hot loops every frame regardless of whether the overlay is open (scene.cpp:295-325, 394-408). The CPU overlay (`src/debug/overlay/ovlCPU.cpp`) consumes them over 32-frame sliding windows; `ovlComponents` shows per-type rows. `RSPQ_PROFILE` (`rspq_profile_dump` every 30 frames) is compile-time optional. `Mem::getHeapDiff` runs only on scene transitions. These are the primary measurement hooks referenced throughout the findings below.

## Findings Overview

| ID | Title | Impact | Effort | Risk | Status |
|----|-------|--------|--------|------|--------|
| model-matrix-rebuild-every-frame | Model::draw rebuilds & re-uploads the object matrix to uncached RAM every frame, no dirty flag | high | small | low | Confirmed |
| tick-instrumentation-always-on | Per-component timing instrumentation (unordered_map + per-frame node realloc) always on in update & draw | medium | small | low | Adjusted |
| fixed-update-full-object-scan | Fixed-timestep loop scans every object & component per physics step for fixedUpdate | medium | small | low | Confirmed |
| spawned-object-linear-id-lookup | All runtime-spawned objects miss idLookup — every getObjectById on them is a linear scan | medium | small | low | Confirmed |
| obj-state-change-full-rescan | Any setEnabled/setVisible triggers a whole-scene rescan with O(n) child scans | medium | medium | medium | Confirmed |
| object-fragmented-allocation | Objects individually heap-allocated and pointer-chased by four per-frame passes | medium | large | medium | Confirmed |
| empty-component-dispatch-no-flags | Unconditional indirect update() calls to empty stubs; unused CompRef.flags could carry has-update/has-draw | low | small | low | Adjusted |
| camera-viewport-recompute-every-frame | Camera::update recomputes projection/view*proj/frustum every frame; needsProjUpdate exists but is never read | low | small | low | Adjusted |
| draw-loop-nondrawable-objects | Per-camera draw loop visits pure-logic objects and unconditionally rewrites flags | low | small | low | Confirmed |
| pending-delete-linear-erase | Object deletion does O(n) vector erase + O(savedTransforms) scan per deleted object | low | small | low | Confirmed |
| empty-drawlayer-state-setup | DrawLayer::draw emits full RDP mode setup + queue run for every layer even when it recorded nothing | low | small | medium | Confirmed |

## Detailed Findings

### model-matrix-rebuild-every-frame — Model::draw rebuilds and re-uploads the object matrix to uncached RAM every frame

[model.cpp:163](../n64/engine/src/scene/components/model.cpp#L163)

**Claim.** Every visible `Model` component per frame runs `t3d_mat4fp_from_srt`: a quaternion→float-4x4 build (~30 FPU mul/add), scale multiply, float→s16.16 conversion (12 casts), and 8 x 64-bit writes to an **uncached** pointer (MatrixManager hands out KSEG1 pointers), each uncached write stalling toward an RDRAM round trip once the write buffer fills. That is roughly 2-4us per model per frame — verification puts it at ~300-400cy (~3-4us); a scene with 50 mostly-static models burns ~100-200us/frame recomputing matrices whose inputs did not change, and steals RDRAM bandwidth from the RDP mid-frame. The rebuild is per-model, per-camera, per-frame with no dirty check anywhere (`objectFlags.h` has no transform-dirty bit).

**Evidence.**

```cpp
void Model::draw(Object &obj, Model* data, float deltaTime)
{
  auto mat = data->matFP.getNext();
  t3d_mat4fp_from_srt(mat, obj.scale, obj.rot, obj.pos);

  if(data->layerIdx)DrawLayer::use3D(data->layerIdx);
  auto &material = data->getMatInstance();
  material.begin(obj);
  t3d_matrix_set(mat, true);
```

`matrixManager.cpp:22` sets `bufferPtr = unached(buffer)`, so the pool matrix is uncached. tiny3d's `t3d_mat4_to_fixed_3x4` (t3dmath.c:183-207) does the 12 float→fixed conversions and exactly 8 uncached 64-bit stores (4 rows x 2 doublewords). The `RingMat4FP` (matrixManager.h:27-48) exists precisely so a new value can be written while the RSP reads an old one. `matFP` is only referenced at model.cpp:163 and animModel.cpp:300. `-Os` cannot eliminate this: cross-frame state caching plus dispatch through the `COMP_TABLE` function-pointer table defeats the compiler.

**Recommendation.** Cache the last-uploaded pos/rot/scale (40 bytes / 10 floats) in `Comp::Model`; in `draw`, value-compare (10 float compares, ~30cy) and if unchanged skip both the fill and the ring advance, passing the previously filled slot to `t3d_matrix_set`. The fixed matrix is a pure deterministic function of those three fields, so value comparison captures every mutation path (script writes, physics render-interpolation) and is more robust than a dirty flag. Reusing the last-filled slot when unchanged is race-free because the bytes are identical — a concurrently-DMAing RSP sees the same data — and the 3-deep ring still buffers across the infrequent transition frames. When the SRT changes, advance to the next ring slot and fill as today. Apply to `Comp::Model` only; `AnimModel` genuinely changes each frame. **Implementation caveat:** initialize the cached SRT to a sentinel (or force-fill the first draw) so a default all-zero cache cannot spuriously match a genuinely all-zero transform and skip the very first fill, leaving an unwritten slot passed to `t3d_matrix_set`.

**How to measure.** Compare the `Model` row in the components overlay (`ovlComponents`) on a scene of static models before/after; total via the `Draw Obj` row in `ovlCPU`.

**Risk & functionality preservation.** Low risk. The change touches only model.cpp, not `particleSim.h`, so editor/hardware preview parity is unaffected. Output is bit-identical whenever inputs are unchanged and identical-by-construction when they change. Waste is 100% redundant and scales linearly with static-model count; models are the common rendered object type, so high impact is defensible (borderline-to-medium only for scenes with few static models).

### tick-instrumentation-always-on — Per-component timing instrumentation always on in the hot update and draw loops

[scene.cpp:295](../n64/engine/src/scene/scene.cpp#L295)

**Claim (adjusted).** Every enabled component pays real, always-compiled profiling overhead each frame with the overlay closed: two out-of-line `get_ticks()` calls (~15cy each) plus a `std::unordered_map<uint8_t,ComponentTicks>::operator[]` whose libstdc++ `_Prime_rehash_policy` does a true runtime hash % bucket_count (VR4300 DIVU ~37cy) and a bucket-list walk; `Code` components incur a second map lookup (`ticksScripts`). Because `ticksComponents.clear()`/`ticksScripts.clear()` run each frame, the first `operator[]` per distinct key re-mallocs a node — dozens (not hundreds) of malloc/free pairs per frame, bounded by distinct component/script types. The same block repeats in the draw loop per camera. Per-component cost is ~80-120cy (verified: ~80-90cy non-Code, ~120cy Code), **not** the originally claimed 150-250cy. For a heavy ~100-object x 2-3 comp single-camera scene this is ~0.3-0.5ms/frame (the original 0.4-0.8ms upper bound is optimistic); typical scenes are lower. Nothing gates any of it on overlay visibility.

**Evidence.** scene.cpp:295-296 `ticksComponents.clear(); ticksScripts.clear();` then per component in the update loop (309-324): `componentTicksStart = get_ticks(); compDef.update(...); componentTicksEnd = get_ticks(); ComponentTicks& componentTickCounter = ticksComponents[compRefs[i].type]; ... if(compRefs[i].type == 0) { ... ticksScripts[codeData->index] ... }`. The identical pattern repeats in `Scene::draw` at 394-408 per camera. scene.h:165-166 declares the two `std::unordered_map<uint8_t, ComponentTicks>`. `get_ticks` (n64sys.c:229) is a real out-of-line call; `COMP_TABLE_SIZE=16` is constexpr and `P64::Script::numNames` is extern-exported (ovlCPU.cpp:306).

**Recommendation.** Two complementary, behavior-preserving changes. **(1) Gating is the primary fix for shipping builds:** branch the entire `get_ticks`/accumulate block on a `Debug::Overlay`-enabled bool so frames with the overlay closed skip it entirely — the array swap alone still leaves the two `get_ticks` calls (~30cy/component) running every frame. **(2)** Replace both `unordered_map`s with flat arrays — `std::array<ComponentTicks, COMP_TABLE_SIZE>` for `ticksComponents` (type is 0..15) and a fixed array sized by `P64::Script::numNames` for `ticksScripts`; clear via `memset`, and update the six `ovlCPU.cpp` consumers (lines 232/276/293/323/355/372) to array indexing. This removes the DIVU, the bucket-walk pointer chase, and all per-frame malloc/free while producing byte-identical overlay numbers, and is the improvement for when the overlay is actually open (also reduces the observer effect). No `particleSim.h`/editor-parity surface is touched and `-Os` cannot perform either change.

**How to measure.** `ticksActorUpdate` (`Upd. Obj`) and `ticksDraw` (`Draw Obj`) rows in the CPU overlay before/after, on a scene with 100+ objects; also wrap the object loop in `get_ticks` with the instrumentation body `#if`'d out to get the delta directly.

**Risk & functionality preservation.** Low risk, no determinism or editor/HW-parity risk. The array swap yields byte-identical overlay numbers; the gating only changes behavior when the overlay is closed (numbers not shown anyway). Note: the array-only change captures only ~half the overhead — the two `get_ticks` calls survive it — which is why the gating is the load-bearing fix for release builds.

### fixed-update-full-object-scan — Fixed-timestep loop scans every object and component per physics step

[scene.cpp:269](../n64/engine/src/scene/scene.cpp#L269)

**Claim.** Per fixed step (1-2 per frame typically, up to `MAX_PHYSICS_STEPS=5` after a hitch), the engine iterates all enabled objects and all their components, loading each scattered `Object` header (D-cache miss ~40-70cy per object, since objects are individual heap allocations) and indexing `COMP_TABLE` just to discover that only `Code` components (code.h:106) have a `fixedUpdate`. For ~100 objects that is ~4000-7000cy (~45-75us) of scan cost per step, multiplied on catch-up frames — exactly when the frame is already over budget.

**Evidence.** scene.cpp:271-283: the `for(auto obj : objects)` loop, guarded by `if(!obj->isEnabled()) continue;`, iterates every `CompRef`, indexes `COMP_TABLE[compRefs[i].type]`, and tests `if(!compDef.fixedUpdate) continue;`, all inside `while (accumulator_ticks >= fixedDeltaTimeTicks)`. Grep confirms only `Comp::Code` defines `fixedUpdate`.

**Recommendation.** Maintain a `std::vector<std::pair<Object*, uint16_t /*offset*/>>` of components whose type has a `fixedUpdate`, built in `loadObject`/spawn and pruned on object delete (or lazily rebuilt when the object list changes). The fixed loop then touches only actual `fixedUpdate` targets; keep the `isEnabled()` check per entry and the list in scene order so update order is preserved.

**How to measure.** `get_ticks` around the fixed-step while-loop (subtract `Coll` ticks already measured by `collisionScene`) on a 100+ object scene at `physicsTickRate` 50; compare catch-up frames by forcing the accumulator to 5 steps.

**Risk & functionality preservation.** Low risk. Behavior and update order are preserved as long as the prebuilt list stays in scene order and is kept in sync at spawn/delete. The wasted portion scales with the fraction of objects lacking a `Code` component.

### spawned-object-linear-id-lookup — All runtime-spawned objects miss the idLookup fast path

[scene.cpp:543](../n64/engine/src/scene/scene.cpp#L543)

**Claim.** `idLookup` only covers ids < 128 (scene.h:117), but the runtime id counter starts at `0xFF` and only increments (scene.cpp:43 `nextId = 0xFF`, scene.cpp:221/533 `++nextId`), so every dynamically spawned object gets an id >= 256 and every lookup on it — `ObjectRef::get()` (object.cpp:73-76, also used by `operator->`/`operator bool` per use), `Object::getParent`, event dispatch in `runPendingEvents` (scene.cpp:458), and parent resolution in `updateChildObjectStates` (scene.cpp:562-563) — falls through to a full O(n) pointer-chasing scan comparing `objId == obj->id`, ~40-70cy of cache miss per element scanned. Scripts holding `ObjectRef`s to spawned objects (projectiles, pickups) pay ~n/2 header-miss scan per deref per frame (~40-80us worst case in a 150-obj scene).

**Evidence.** scene.cpp:543-558: `if(objId < idLookup.size()) { return idLookup[objId]; } for(auto obj : objects) { if (objId == obj->id) return obj; } return nullptr;`. scene.h:117: `std::array<Object*, 128> idLookup{};`. Spawn path scene.cpp:533 `.objectId = ++nextId` with `nextId` starting at `0xFF`.

**Recommendation.** Keep a secondary lookup for high ids maintained at spawn/delete: a small open-addressing hash (`id -> Object*`) or a sorted vector with binary search, updated in `loadObject` (sceneLoader.cpp:184-186) and in the delete path (scene.cpp:336-338). Alternatively recycle freed low ids so spawned objects land in `idLookup`. Lookup results are unchanged.

**How to measure.** Micro-benchmark: script deref of 32 `ObjectRef`s to spawned objects per frame in a 150-object scene, timed via the Script Update row in `ovlScripts` before/after.

**Risk & functionality preservation.** Low risk. Results are identical; only the lookup mechanism changes. The secondary structure must be maintained on both spawn and delete paths to avoid stale pointers.

### obj-state-change-full-rescan — Any setEnabled/setVisible triggers a whole-scene state rescan

[scene.cpp:248](../n64/engine/src/scene/scene.cpp#L248)

**Claim.** When the single global `needsObjStateUpdate` bool is set (any object toggling enabled/visible that frame — e.g. one blinking object makes this run every frame), `Scene::update` calls `updateChildObjectStates(nullptr, *obj)` for **every** object. Each call does a parent `getObjectById` (linear scan if the parent is a spawned id, per the previous finding) and, when state changed, `iterObjectChildren` — itself a full O(n) group-id scan (scene.h:260-265) — plus recursion, so children of changed parents are revisited. Worst case is O(n^2) object-header cache misses; a single toggle in a 200-object scene costs ~0.2-0.3ms of header misses.

**Evidence.** scene.cpp:248-253: `if(needsObjStateUpdate) { for(const auto obj : objects) { updateChildObjectStates(nullptr, *obj); } ... }`. scene.cpp:560-596: per call `if(!parent && obj.group) { parent = getObjectById(obj.group); }` then on change `iterObjectChildren(obj.id, [&](Object* child) { updateChildObjectStates(&obj, *child); });` where `iterObjectChildren` is `for (auto o : objects) { if(o->group != parentId)continue; f(o); }`. Triggered by `Object::setEnabled`/`setVisible` (object.cpp:26-42) setting the global bool. `ObjectFlags::HAS_CHILDREN` (objectFlags.h:15) is read at object.h:164 but grep shows it is never set, so it cannot currently gate anything.

**Recommendation.** Track dirtiness per object instead of one global bool: keep a small vector of objects whose `setEnabled`/`setVisible` was called and propagate only from those (recursing into children as today). Additionally maintain `HAS_CHILDREN` at load/spawn (set on the parent when a child references it) and gate the `iterObjectChildren` scan on it to skip the O(n) scan for leaf objects. State transitions and event-emission order for affected objects are unchanged.

**How to measure.** `get_ticks` around the `needsObjStateUpdate` block (there is already commented-out "State Change Time" timing at scene.cpp:247/255-256) with one object toggling visibility per frame in a 200-object scene.

**Risk & functionality preservation.** Medium risk — the change reworks propagation logic, so care is needed that per-object dirty tracking reproduces the exact set of objects the global rescan would have visited, and that `HAS_CHILDREN` is set on every parent that is actually referenced as a group. State-transition and event order must be preserved.

### object-fragmented-allocation — Objects individually heap-allocated and pointer-chased by four per-frame passes

[sceneLoader.cpp:108](../n64/engine/src/scene/sceneLoader.cpp#L108)

**Claim.** Each `Object` is a separate `memalign` reached through `std::vector<Object*>` (scene.h:94, with an in-code `@TODO` at scene.h:93). Per frame the pointer array is walked by the fixed loop (scene.cpp:271), update dispatch (scene.cpp:299), per-camera draw (scene.cpp:380), and state/child scans (scene.cpp:250, scene.h:260) — each object's header read is a compulsory D-cache miss on the 8KB direct-mapped cache. ~100 objects x 4+ passes x ~50cy ≈ 0.2-0.3ms/frame, worse once dynamic spawns/frees fragment the heap and under RDP/RSP RDRAM contention.

**Evidence.** sceneLoader.cpp:108 `void* objMem = memalign(DATA_ALIGN, allocSize); // @TODO: custom allocator`. Per-frame walkers cited above. Object header + CompRef + component data are co-located within one allocation (object.h:65-76), but the allocations themselves are scattered. Verification note: the VR4300 has no HW prefetch, so an arena mainly helps via 16B-line spatial locality on adjacent small headers — a real but partial win, and the estimate overlaps the loop-level findings.

**Recommendation.** Allocate load-time objects from a contiguous bump arena in scene-load order (they are created back-to-back in `loadScene`, sceneLoader.cpp:236-238) and free the arena wholesale in `~Scene`; dynamic spawns can keep `malloc` or use a size-classed pool with a free-list. Iteration order and object-address semantics are unchanged; only `~Scene`'s per-object `free(obj)` (scene.cpp:155-158) and the delete path (scene.cpp:334-345, `malloc_usable_size` accounting) need an "is arena pointer" branch.

**How to measure.** `Upd. Obj` + `Draw Obj` overlay rows on a 150-object scene before/after; the win is distributed across all object loops, so compare whole-frame CPU time via the General table Total.

**Risk & functionality preservation.** Medium risk and large effort — introduces a custom allocator lifetime and an is-arena-ptr branch on every free path; a mistake there is a use-after-free or double-free. The estimate is somewhat optimistic and overlaps the loop-level findings, but the architecture cost is genuine. This is the enabling change for spatial locality but the lowest priority-per-effort of the medium tier.

### empty-component-dispatch-no-flags — Unconditional indirect update() calls to empty stubs

[scene.cpp:310](../n64/engine/src/scene/scene.cpp#L310)

**Claim (adjusted).** Empty component stubs (`Model`/`Culling` `update`, `Light`/`Camera` `draw`) are dispatched through non-null function pointers every frame; standalone this is only the cost of an empty indirect call (~10-20cy each — load ptr + jalr + return, no arg-setup work), since the large ~150-250cy figure originally quoted is really finding *tick-instrumentation-always-on*'s timing block, double-counted here. For 50 stub `Model::update` calls that is ~0.01ms. `CompRef.flags` is written `0` and never read, so a `HAS_UPDATE`/`HAS_DRAW` bit could gate dispatch.

**Evidence.** scene.cpp:310 `compDef.update(*obj, dataPtr, deltaTime);` has no null/empty check; the SFINAE `get_update` (via `decltype(&T::update)`) yields a non-null pointer for `Model::update` (model.h:43 `{}`) and `Culling::update`. The draw loop null-checks `if(compDef.draw)` (scene.cpp:390) but empty inline bodies like `light.h:70` produce non-null pointers and get dispatched per camera. sceneLoader.cpp:141 writes `flags = 0`; grep shows no reader.

**Recommendation.** At load/spawn (`loadObject`), set `HAS_UPDATE`/`HAS_FIXED_UPDATE`/`HAS_DRAW` bits in `CompRef.flags` derived from the `COMP_TABLE` entry against a per-type constant marking which types have real bodies (distinct "empty" markers), then test the flag before dispatch in the update/draw loops. Observable behavior is identical (the stubs do nothing); only the debug overlay count column for those types changes.

**How to measure.** `Upd. Obj`/`Draw Obj` in `ovlCPU` on a model-heavy scene; per-type rows in `ovlComponents` drop to zero entries for stub types.

**Risk & functionality preservation.** Low risk, low value standalone. Best folded into the same load-time flag-setting pass as the draw-loop finding. The stubs do nothing, so gating them out is observationally identical.

### camera-viewport-recompute-every-frame — Camera::update recomputes projection, view*proj and frustum every frame

[camera.cpp:25](../n64/engine/src/scene/camera.cpp#L25)

**Claim (adjusted).** Per camera per frame `Camera::update` unconditionally calls `t3d_viewport_set_perspective` (tanf + w-normalize + divides) and `t3d_viewport_set_view_matrix` (full `t3d_mat4_mul` ~450cy + `t3d_mat4_to_frustum`: 6 plane normalizes with `sqrtf` ~29cy each + divides), ~1500-2000cy (~16-21us) per camera even when nothing changed. The recompute is genuinely skippable and `camera.h:23 needsProjUpdate` already exists (set at camera.cpp:91, never read). At one camera (the common case) this is only ~0.02ms/frame (~0.1% of a 16.6ms budget), below the project's medium tier; it reaches medium only with 2-4 split-screen cameras — hence impact corrected to low.

**Evidence.** camera.cpp:25-29: `t3d_viewport_set_perspective(&viewports, fov, aspectRatio, near, far); t3d_viewport_set_view_matrix(&viewports, &viewMatrix);`, called for all cameras every frame from scene.cpp:328-330. camera.h:23 `uint8_t needsProjUpdate{false};` — set at camera.cpp:91, never read (grep). tiny3d's attach still uploads the (unchanged) matrices per frame via its own buffered path, so skipping the recompute does not affect RSP state.

**Recommendation.** In `Camera::update`, cache last fov/aspectRatio/near/far and call `t3d_viewport_set_perspective` only when one differs; call `t3d_viewport_set_view_matrix` only when `needsProjUpdate` (set by `setLookAt`/`setPosRot`) is true, then clear it. **Caveat:** a perspective change sets `_isCamProjDirty` and must still force a `matCamProj`/frustum recompute, so gate the view-matrix recompute on `needsProjUpdate` OR a perspective-changed check. `viewMatrix` is only mutated through those setters (`getViewMatrix` returns const&), so results are bit-identical.

**How to measure.** `get_ticks` around the cameras loop in `Scene::update`; compare a static-camera scene before/after, or add a temporary counter.

**Risk & functionality preservation.** Low risk. Results are bit-identical as long as the perspective-changed check is honored alongside `needsProjUpdate`. Low impact at one camera; worth doing when the dirty flag is wired since it is nearly free.

### draw-loop-nondrawable-objects — Per-camera draw loop visits pure-logic objects and unconditionally rewrites flags

[scene.cpp:380](../n64/engine/src/scene/scene.cpp#L380)

**Claim.** For each camera, the draw pass touches every enabled object (one D-cache miss each), scans its `CompRef` table for non-null draw pointers, then unconditionally executes `setFlag(IS_CULLED, false)` at scene.cpp:414 — a read-modify-write that dirties the object's cache line forcing a later writeback — even for logic-only objects/colliders/triggers with no draw components and even when `IS_CULLED` was already clear. With multiple cameras this repeats per camera.

**Evidence.** scene.cpp:380-415: the per-object loop scans comps for `if(compDef.draw)` and then runs `obj->setFlag(ObjectFlags::IS_CULLED, false)` at 414 regardless of whether any comp drew or the flag was set. Objects without draw comps can never be culled (`Culling::draw` sets `IS_CULLED` and `Culling` is itself a draw comp).

**Recommendation.** At load/spawn set an object-level `HAS_DRAW_COMP` bit (free bits 8-15 of the `uint16` flags) when any component's draw pointer is non-null, and skip objects lacking it in the draw loop. Change line 414 to `if(flags & IS_CULLED) setFlag(...)` to avoid dirtying untouched cache lines. Both are observationally identical.

**How to measure.** `Draw Obj` row in `ovlCPU` on a scene with many trigger/logic objects (e.g. 50 drawables + 100 logic objects), single and dual camera.

**Risk & functionality preservation.** Low risk. Small per-object saving multiplied by camera count. Pairs naturally with the `empty-component-dispatch` load-time flag pass.

### pending-delete-linear-erase — Object deletion does O(n) vector erase plus O(savedTransforms) scan per deleted object

[scene.cpp:334](../n64/engine/src/scene/scene.cpp#L334)

**Claim.** Each deleted object triggers `std::erase(objects, obj)` — a full scan-and-shift of the objects vector — plus `std::erase_if` over `savedTransforms_`; deleting k objects in one frame (e.g. clearing a wave of enemies) costs O(k*n) pointer-array shifts in the frame the game is busiest.

**Evidence.** scene.cpp:334-346: the `for(auto &obj : pendingObjDelete)` loop runs `std::erase_if(savedTransforms_, ...)` and `std::erase(objects, obj)` per deleted object. `PENDING_REMOVE` is already set in `Object::remove` (object.cpp:44-46).

**Recommendation.** When `pendingObjDelete` is non-empty, run a single `std::erase_if(objects, [](Object* o){ return o->isPendingRemove(); })` and a single `erase_if` over `savedTransforms_` checking the same flag, then destroy/free in a second loop. Survivor order and destruction order are preserved; the `savedTransforms_` removal must stay so `restoreInterpolatedTransforms` (scene.cpp:500-506) never touches freed memory — the single-pass fix keeps that.

**How to measure.** `get_ticks` around the `pendingObjDelete` block while force-removing 30 objects in one frame in a 200-object scene.

**Risk & functionality preservation.** Low risk. Order preservation is the only correctness concern and the single-pass `erase_if` maintains it; keep `savedTransforms_` cleanup in the same pass.

### empty-drawlayer-state-setup — DrawLayer::draw emits full RDP mode setup and queue run for every layer even when it recorded nothing

[drawLayer.cpp:69](../n64/engine/src/renderer/drawLayer.cpp#L69)

**Claim.** Per frame each configured layer unconditionally enqueues an rdpq mode block (zbuf/blender/fog), optional fog range + color, and `rspq_queue_run`, with no emptiness check — RSP/RDP work and rspq buffer traffic that is pure waste for layers no object rendered into this frame. A project configuring, say, 4 3D layers but using 1-2 pays 2-3 redundant SOM/mode bundles per frame. (The empty `rspq_queue_run` itself is cheap; the wasted part is the mode/fog setup.)

**Evidence.** drawLayer.cpp:69-108: `rdpq_mode_begin()` / `rdpq_mode_zbuf` / `rdpq_mode_blender` / `rdpq_mode_fog` / `rdpq_mode_end()`, optional fog range+color, then `rspq_queue_run(layer[frameIdx].queue)`, all with no emptiness check; `draw3D`/`drawPtx`/`draw2D` (111-160) iterate all configured layer counts.

**Recommendation.** Track per layer per frame whether anything was recorded (set a bool in `DrawLayer::use()` when switching to the layer, cleared in `nextFrame` alongside `rspq_queue_clear`, drawLayer.cpp:162-169) and skip the mode setup + queue run for untouched layers.

**How to measure.** `RSPQ_PROFILE` build (`rspq_profile_dump` already wired at scene.cpp:441-448): compare RDP/ovl command counts per frame with 4 configured layers of which 2 are unused.

**Risk & functionality preservation.** Medium risk. The `lastLightMode` cache (drawLayer.cpp:81-84) and fog-enable state must be left in the state the next non-empty layer expects; in practice each drawn layer fully re-establishes zbuf/blender/fog so skipping empty ones is safe except for the t3d lighting-mode cache, which must stay consistent.

## Considered & Rejected

None. All eleven findings survived verification (nine confirmed, two adjusted with corrected impact/claim/recommendation applied above). No finding was refuted.

## Suggested Order of Attack

1. **model-matrix-rebuild-every-frame** (high / small / low) — the largest confirmed win, fully localized to model.cpp, bit-identical, no dependencies. Do first; remember the sentinel-init caveat.
2. **tick-instrumentation-always-on** (medium / small / low) — start with the overlay-enabled gating (the load-bearing lever for shipping builds), then the flat-array swap for overlay-open frames. Independent of everything else. Landing this first also de-noises the `ovlCPU`/`ovlComponents` measurements you will use to verify the rest.
3. **Load-time flag pass — bundle three findings that share one mechanism:** set `HAS_UPDATE`/`HAS_FIXED_UPDATE`/`HAS_DRAW`/`HAS_DRAW_COMP` bits in `CompRef.flags`/object flags at `loadObject`/spawn, then consume them. This covers **empty-component-dispatch-no-flags**, **draw-loop-nondrawable-objects** (plus the `if(flags & IS_CULLED)` guard), and provides the `HAS_CHILDREN` machinery reused later. All small/low.
4. **fixed-update-full-object-scan** and **spawned-object-linear-id-lookup** (both medium / small / low) — independent, high value-per-effort. The id-lookup fix (secondary structure maintained at spawn/delete) is a prerequisite that also cheapens finding #6's parent resolution.
5. **camera-viewport-recompute-every-frame** (low / small / low) — nearly free once you wire the existing `needsProjUpdate` flag plus the perspective-changed check; do opportunistically.
6. **pending-delete-linear-erase** (low / small / low) — self-contained single-pass rewrite.
7. **obj-state-change-full-rescan** (medium / medium / medium) — do after the id-lookup fix (step 4) and the `HAS_CHILDREN` machinery (step 3), which it depends on; its own per-object dirty tracking is the riskier part.
8. **empty-drawlayer-state-setup** (low / small / medium) — verify with an `RSPQ_PROFILE` build; watch the light-mode/fog state consistency.
9. **object-fragmented-allocation** (medium / large / medium) — last: highest effort and risk, and its measured win overlaps the loop-level fixes already landed above, so re-measure after steps 1-8 before committing to the arena allocator.
