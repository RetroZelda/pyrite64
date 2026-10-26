# Animation System Performance Analysis

_Date: 2026-07-06_

The Pyrite64 `AnimModel` component is a thin wrapper that delegates nearly all skeletal-animation CPU cost to tiny3d. Per frame, per animated model, the engine runs `t3d_anim_update` (keyframe streaming through a libc `FILE` plus per-channel lerp/nlerp), an optional whole-skeleton blend, and `t3d_skeleton_update` (per-bone quat→mat4, a full 4x4 float multiply, and an s16.16 conversion written into uncached RDRAM). This is easily 50-200us for a 20-30 bone character on the VR4300. The dominant engine-side wastes are structural rather than micro-inefficiencies: skeletons are fully updated for objects that end up culled or hidden (culling only gates the draw pass, never update), blend animations are fully decoded even when their output is discarded at alpha≈0, and keyframes are streamed through buffered newlib `FILE`s with blocking mid-frame ROM reads. Secondary costs come from every static `Model` paying an indirect empty-`update` call wrapped in per-frame map allocation, and from object matrices being rebuilt into uncached RAM every frame (and once per camera) even for transforms that never change. None of the eleven findings were refuted in verification; one high-impact structural fix (culled/hidden skeleton work) plus a cluster of small, low-risk engine-only wins account for most of the recoverable time.

## Architecture & Frame Cost Model

**Per-frame update flow** (`n64/engine/src/scene/scene.cpp`): `Scene::update` first calls `AnimController::update` (only increments a global frame counter, `animController.cpp:21-23`), then loops all enabled objects. For every component it calls `COMP_TABLE[type].update` with `get_ticks()` instrumentation on each side of the call and accumulates into a `std::unordered_map<uint8_t,ComponentTicks> ticksComponents[type]` (`scene.cpp:305-325`). That map is `clear()`ed every frame (`scene.cpp:295`), so the first touch of each component type re-allocates a node from the heap each frame. The update loop gates only on `!isEnabled()` / `isPendingRemove()` (`scene.cpp:301`) — it has no `IS_CULLED`/`HIDDEN` check.

**Per-frame draw flow**: `Scene::draw` loops per camera, then per object, calling `compDef.draw` (with a `if(compDef.draw)` guard) unless `obj->flags` has `IS_CULLED | HIDDEN` (`scene.cpp:388`, which `break`s the component loop). `IS_CULLED` is set only during the draw pass by the `Culling` component (`culling.cpp:27-46`) and reset immediately after each object draws (`scene.cpp:414`), so the update pass structurally can never observe it. The whole per-object draw loop is wrapped in `for(auto &cam : cameras)` (`scene.cpp:362-415`), so per-object matrix fills repeat once per camera.

**`AnimModel::update`** (`animModel.cpp:245-292`): a stagger early-out (`shouldUpdate`, default `staggerRate=1` = every frame; nothing in the engine calls `setAnimStagger`), then `t3d_anim_update` on the main anim; if a blend anim is set, `t3d_anim_update` on **both** anims (phase-locked dt computed with 2 float divides), `t3d_skeleton_blend` when `alpha>0.001`, and finally `t3d_skeleton_update(&skelMain)` **unconditionally**.

**`AnimModel::draw`** (`animModel.cpp:294-313`): `RingMat4FP::getNext()` (3-slot ring inside a globally shared uncached buffer, `matrixManager.cpp:22 bufferPtr = unached(buffer)`) + `t3d_mat4fp_from_srt` writing into uncached RAM, then `t3d_skeleton_use`, `t3d_matrix_set`, and `rspq_block_run` of the pre-recorded whole-model block. There is no frustum culling in `AnimModel` at all — `Model` has `FLAG_CULLING` + a BVH; `AnimModel` does not.

**tiny3d cost, per animated model per frame**: `t3d_anim_update` (`t3danim.c:190-229`) walks every channel, streams keyframes via `fread(8..10 bytes)` from a `funopen`'d `FILE` (`t3danim.c:159-162`; `.sdata` is written raw by the exporter and is not `mkasset`-compressed, so this is libdragon's `cookie_none` path — newlib `FILE` locking plus buffered refills via blocking mid-frame PI/ROM reads), one float `div.s` per channel for interp, `t3d_quat_nlerp` (`sqrt.s` + `div.s`) per rotation channel, and sets every channel's bone `hasChanged` flag every frame. `t3d_skeleton_update` (`t3dskeleton.c:62-112`) then, for every changed bone (i.e. all animated bones every frame): `t3d_mat4_from_srt` (quat→mat + `t3d_mat4_scale`), a full 4x4 `t3d_mat4_mul` with the parent (64 `mul.s` + 48 `add.s`, `t3dmath.h:503-513`), and `t3d_mat4_to_fixed` writing 64 bytes as 8x 64-bit stores directly into `malloc_uncached` bone-matrix memory. `skelMain` is triple-buffered (`t3d_skeleton_create_buffered(model,3)`), so bones keep rewriting matrices for 3 frames after they stop changing.

**Existing instrumentation**: the debug overlay CPU page (`n64/engine/src/debug/overlay/ovlCPU.cpp:217-302`) renders per-frame **Comp Update** / **Comp Draw** tables with per-component ticks and instance counts, including AnimModel (ID 10) and Model (ID 1); a **General** table shows `Upd. Obj` / `Draw Obj` totals. An `RSPQ_PROFILE` dump exists at `scene.cpp:441-448` for RSP-side timing. These are the measurement surfaces referenced by each finding below.

> Side note (correctness, not perf, surfaced during analysis): `MatrixManager::free` (`matrixManager.cpp:49`) tests `mat > &bufferPtr[MATRIX_COUNT]`; a `malloc_uncached` fallback pointer located below the pool would corrupt `usedFlags`. This should be a proper range check. Out of scope for this report but worth tracking.

## Findings Overview

| ID | Title | Impact | Effort | Risk | Status |
|----|-------|--------|--------|------|--------|
| animation-skeleton-update-runs-for-culled-hidden-objects | Full skeleton matrix rebuild runs every frame for culled/hidden objects | high | medium | medium | confirmed |
| animation-keyframes-streamed-through-libc-file-per-frame | Keyframe decode does an 8-10 byte `fread` through a locked newlib FILE per keyframe | medium | small | low | confirmed |
| animation-empty-model-update-dispatched-with-per-frame-map-alloc | Empty `Model::update` indirectly called every frame with per-frame map allocation | medium | small | low | confirmed |
| animation-object-matrix-rebuilt-every-frame-for-static-transforms | Object SRT matrix recomputed into uncached RAM every frame for unchanged transforms | medium | small | low | confirmed |
| animation-blend-anim-fully-decoded-at-alpha-zero | Blend animation fully decoded every frame even when blendFactor≈0 and output unused | medium | medium | medium | confirmed |
| animation-bone-matrix-math-full-4x4-instead-of-affine | Per-bone pipeline uses full 4x4 multiply/convert for affine SRT matrices | medium | medium | low | confirmed |
| animation-animmodel-has-no-frustum-culling | AnimModel submits whole skinned model to RSP every frame with no frustum check | medium | medium | medium | adjusted |
| animation-matrix-refilled-once-per-camera | Identical object matrix recomputed once per camera per frame | low | small | low | confirmed |
| animation-interp-div-per-channel-per-frame | `t3d_anim_update` does a float divide per channel per frame for interp | low | small | medium | adjusted |
| animation-blend-duration-ratios-recomputed-per-frame | Two float divides for blend duration ratios recomputed every frame | low | small | low | confirmed |
| animation-skeleton-clone-allocated-per-animation | A full bone-array clone allocated per animation, but only the blend slot is used | low | small | medium | confirmed |

## Detailed Findings

### Full skeleton matrix rebuild runs every frame for objects that are culled or hidden

Location: [animModel.cpp:291](../n64/engine/src/scene/components/animModel.cpp#L291)

**Claim.** `t3d_skeleton_update` (and `t3d_skeleton_blend`) run in the update pass for every enabled `AnimModel` regardless of visibility. Per animated bone this costs a quat→mat4 build, a full 4x4 float multiply (64 `mul.s` @ ~5cy + 48 `add.s` @ ~3cy on the VR4300 FPU), and a 64-byte s16.16 conversion written to uncached RDRAM — roughly 50-200us per 20-30 bone skeleton per frame. `IS_CULLED` is only evaluated and honored in the draw pass (`scene.cpp:388`, set by `Culling::draw`, reset at `scene.cpp:414`), so off-screen characters pay the entire cost for matrices nobody reads. `HIDDEN`-but-enabled objects (`object.h` `isVisible` is distinct from `isEnabled`) pay it too, with no authoring required.

**Evidence.** In `AnimModel::update`, `t3d_skeleton_update` runs unconditionally after the anim decode; the update loop in `Scene::update` has no visibility gate:

```cpp
// animModel.cpp:277-291
      if (alpha > 0.001f) {
        t3d_skeleton_blend(
          &data->skelMain, &data->skelMain,
          &data->skelAnim[data->animIdxBlend], alpha);
      }
    }
    else { t3d_anim_update(&data->anims[data->animIdxMain], dt); }
    t3d_skeleton_update(&data->skelMain);   // <-- unconditional, every enabled AnimModel
```

```cpp
// scene.cpp:301-310  (update loop: only gate is enabled/pending-remove)
    if(!obj->isEnabled() || obj->isPendingRemove()) continue;
    ...
      componentTicksStart = get_ticks();
      compDef.update(*obj, dataPtr, deltaTime);   // no IS_CULLED / HIDDEN check
```

`t3dskeleton.c:94-102` does `t3d_mat4_from_srt` + `t3d_mat4_mul` + `t3d_mat4_to_fixed` per changed bone, and `t3d_anim_update` (`t3danim.c:218`) marks every animated bone changed every frame — so the expensive per-bone path is hit for all bones every frame. Verification measured the 4x4 multiply alone at ~464 cy/bone (~5us), making a 20-30 bone character's `skeleton_update` order 100-160us/frame.

**Recommendation.** Keep `t3d_anim_update` in `AnimModel::update` (so animation time and keyframe streaming stay deterministic), but move `t3d_skeleton_blend` + `t3d_skeleton_update` into `AnimModel::draw` behind a "skeleton dirty" flag stamped with `AnimController::getFrameCount()` (run once per frame, on the first camera). Draw is already skipped for culled/hidden objects, so the matrix math is only done for models actually rendered; bone SRT values still advance identically for scripts. `t3d_skeleton_use` after the move still points the RSP at the freshly written buffer, and triple-buffering stays intact. This is engine-only and does not touch `particleSim.h`. `-Os` cannot perform a visibility-gating transform, so this is not compiler-reachable.

**How to measure.** Debug overlay → CPU → Components: the AnimModel "Comp Update" row (per-object avg us) in a scene where several animated models are behind the camera with Culling components attached — cost should drop to near `t3d_anim_update`-only for culled models. Also compare the `Upd. Obj` bar before/after.

**Risk & functionality preservation (medium).** The only observable change: user code reading `bone->matrix` or `boneMatricesFP` (e.g. `t3d_skeleton_get_bone_pos_model_space`) between update and draw would see one-frame-stale matrices. If that matters for any content, gate the move behind an `obj HIDDEN` check in `update()` first, which still captures the no-authoring-required HIDDEN win. Two win paths are valid: HIDDEN-but-enabled objects (default-path, no authoring) and culled objects (require a `Culling` component ordered before AnimModel, per the frustum finding below). Impact `high` holds under the stated scenario (several off-screen/hidden animated models): each reclaimed character is ~100-160us, so 4-6 of them is multiple percent of a frame. The residual for culled models is anim-decode-only, as the finding honestly notes.

### Keyframe decode does an 8-10 byte `fread` through a locked newlib FILE per keyframe, with mid-frame ROM reads

Location: [animModel.cpp:34](../n64/engine/src/scene/components/animModel.cpp#L34)

**Claim.** Every keyframe crossed by `t3d_anim_update` issues `fread(&kf, 8..10, 1, file)` through libdragon's `funopen` cookie (FILE lock + dispatch + buffered memcpy, ~1-3us each) and periodically a blocking PI/ROM read to refill the 1KB stdio buffer mid-frame, stealing bus bandwidth from RDP/RSP. A character with 40-60 channels sampled at 15-30Hz crosses ~10-30 keyframes per 60Hz frame per anim (x2 while blending), i.e. tens of microseconds per model per frame plus a full-file re-stream at every loop rewind.

**Evidence.** `activateAnim` (`animModel.cpp:33-35`) does `anims[idx].file = asset_fopen(...)`; `t3danim.c:160-161` calls `fread(&kf, anim->nextKfSize, 1, anim->file)` in a per-channel `while` loop (`t3danim.c:212-214`), and `rewind(anim->file)` on every loop wrap (`t3danim.c:45-46,197`). The `.sdata` is written raw by the exporter (`writer.cpp:464`) and not passed through `mkasset`, so this is libdragon `asset.c`'s `cookie_none` `funopen` path with per-call locking.

**Recommendation.** In `activateAnim`, load the anim's `.sdata` into RAM once (`asset_load` / read-whole-file) and give tiny3d a `FILE*` over that buffer via `fmemopen(buf, size, "rb")` — `t3danim` only uses `fread`/`rewind`/`fclose`, all supported by `fmemopen` with identical semantics, so decoded keyframes are byte-identical. `deactivateAnim` frees the buffer. This removes all mid-frame ROM PI DMA and buffer-refill stalls; the per-call FILE lock and a buffered memcpy remain (verification confirmed `fmemopen` FILEs still take the newlib lock), so the win is eliminating the mid-frame ROM stalls and cookie dispatch, not the lock. FILE-slot usage (the 64 libc-lock limit) is unchanged since only selected anims are open. Cache the buffer per unique `filePath` when main and blend share one.

**How to measure.** `get_ticks()` around `t3d_anim_update` for a looping anim (visible in the overlay AnimModel Comp Update row once skeleton work is isolated); watch for the loop-rewind spike disappearing. RAM cost = `.sdata` size per active anim.

**Risk & functionality preservation (low).** `fmemopen` matches `fread`/`rewind`/`fclose` semantics exactly, so keyframe bytes and decoded poses are identical. The only cost is added RDRAM proportional to active-anim `.sdata` size.

### Empty `Model::update` is indirectly called for every static model every frame, wrapped in timing + per-frame-allocating map lookups

Location: [model.h:43](../n64/engine/include/scene/components/model.h#L43)

**Claim.** `Model` defines `static void update(...) {}` solely because `Scene::update` calls `compDef.update` without a null check. Every static Model therefore pays, every frame: an indirect call to an empty function, two `get_ticks()` reads, and a `std::unordered_map<uint8_t,ComponentTicks>::operator[]` on a map cleared each frame (so the first touch per component type re-allocates a heap node every frame). Node re-alloc is once per component *type* per frame, not per object. Per-object cost is ~50-90 cy realistically; with dozens of Models it accumulates, and the >100us/frame upper bound needs ~130+ static Models.

**Evidence.** `model.h:43`: `static void update(Object& obj, Model* data, [[maybe_unused]] float deltaTime) {}`. `scene.cpp:310` calls `compDef.update(*obj, dataPtr, deltaTime)` with no `(compDef.update)` guard — unlike `draw` at `scene.cpp:390` which has `if(compDef.draw)`. It is wrapped in `get_ticks()` and `ticksComponents[compRefs[i].type]` (`scene.cpp:313`), and `ticksComponents.clear()` runs every frame (`scene.cpp:295`). The instrumentation is compiled unconditionally (no `#ifdef`), which is what makes it always-on.

**Recommendation.** Delete the empty `Model::update` (`componentTable.cpp`'s `get_update<>` already yields `nullptr` for components without one) and add `if(!compDef.update) continue;` in `Scene::update`, mirroring the `fixedUpdate`/`draw` guards. Additionally replace `ticksComponents` with a fixed `std::array<ComponentTicks, COMP_TABLE_SIZE>` zeroed per frame to remove the hash lookups and per-frame node allocations around every remaining component (including AnimModel). Observable behavior and overlay data unchanged.

**How to measure.** Overlay General table `Upd. Obj` (avg) in a scene with many static Model objects before/after; the Model row disappears from Comp Update entirely.

**Risk & functionality preservation (low).** Behavior-neutral: the empty function does nothing, and the array swap produces identical overlay numbers. The ~100-150 cy/component figure in the original claim is on the high side (`get_ticks` is a single `mfc0`; realistic per-object cost is ~50-90 cy) — the medium tier holds in model-heavy scenes.

### Object SRT matrix is recomputed and rewritten to uncached RAM every frame even when pos/rot/scale did not change

Location: [model.cpp:163](../n64/engine/src/scene/components/model.cpp#L163)

**Claim.** `Model::draw` and `AnimModel::draw` call `matFP.getNext()` + `t3d_mat4fp_from_srt` unconditionally: quat→mat4 (~30 mul/add), 12 float→s16.16 conversions, and 8x 64-bit stores into the uncached MatrixManager pool — per object per frame. For scenes dominated by static decoration Models (the common case), this is ~150-250 cycles (~2-3us) per object per frame of pure recomputation; 50 static objects ≈ 100-150us/frame.

**Evidence.** `model.cpp:163-164`: `auto mat = data->matFP.getNext(); t3d_mat4fp_from_srt(mat, obj.scale, obj.rot, obj.pos);` (same pattern at `animModel.cpp:300-301`). `matrixManager.cpp:22 bufferPtr = unached(buffer);` makes every one of those stores an uncached write. `obj.rot` is `fm_quat_t` + `pos` + `scale` = 10 floats (`object.h:42-44`).

**Recommendation.** Cache the last-written SRT (10 floats, 40 bytes, cached memory) in the component; in draw, compare against `obj.scale`/`rot`/`pos` (10 cached loads + compares, ~30-40 cycles) and, when equal, skip both the ring rotation (keep using the current slot — its contents are already correct and no CPU write occurs, so there is no race with the RSP) and the matrix fill. Bitwise-equal inputs produce bitwise-equal matrices, so behavior is exact. First frame always writes.

**How to measure.** Overlay Comp Draw row for Model (per-object avg) in a static-heavy scene; also the Draw Obj bar. Verify no visual change when objects move (the comparison must be bitwise, not epsilon).

**Risk & functionality preservation (low).** The compare/skip is race-free because the reused slot is never CPU-written while the RSP may read it, and bitwise-equal SRT yields bit-identical matrices. Determinism preserved.

### Blend animation is fully decoded and interpolated every frame even when blendFactor is ~0 and its output is never read

Location: [animModel.cpp:275](../n64/engine/src/scene/components/animModel.cpp#L275)

**Claim.** When a blend anim is selected, `t3d_anim_update` runs on it every frame to keep phases locked, but when `alpha<=0.001` the skeleton blend is skipped, so all per-channel work — `t3d_quat_nlerp` per rotation channel (`sqrt.s` ~29cy + `div.s` ~29cy each) and a float `div` per channel for interp — computes values into `skelAnim[idxBlend]` that nothing reads. For a 40-60 channel anim that is roughly 2000-4000 wasted FPU cycles (~25-45us) per model per frame while a blend target is parked at alpha 0.

**Evidence.** `animModel.cpp:273-284`: comment `// Always update both so their phases never drift apart, even when alpha is 0 or 1`, then `t3d_anim_update(&data->anims[data->animIdxBlend], dtBlend);` followed by the `if (alpha > 0.001f)` gate on `t3d_skeleton_blend`. `t3danim.c:207-228` does the nlerp/lerp + `changedFlag` writes for every channel unconditionally when playing, and `skelAnim[blend]` is only ever read by `t3d_skeleton_blend`.

**Recommendation.** Dependency change (tiny3d): add an "advance-only" mode to `t3d_anim_update` (or a `T3DAnim` flag) that still advances time and streams keyframes (`kfCurr`/`kfNext` stay correct) but skips the per-channel interpolation and target/`changedFlag` writes; use it in `AnimModel::update` when `alpha<=0.001`. Values written on the frame alpha rises are recomputed from the same `kfCurr`/`kfNext`/time, so output is bit-identical because the skipped writes were never observable. Engine-only alternative (riskier): don't update the blend anim while `alpha<=0.001` and resync via `t3d_anim_set_time` when alpha rises — but that changes the observable `anim->time` of `getBlendAnim()` for scripts, and `set_time`'s rewind causes a re-stream spike.

**How to measure.** Overlay AnimModel Comp Update per-object time with a blend anim set and `blendFactor=0`, before/after; should drop by roughly the cost of one `t3d_anim_update`.

**Risk & functionality preservation (medium).** The tiny3d advance-only path is bit-exact because the discarded interpolation results were never read. Impact is conditional on a blend anim being assigned while `alpha<=0.001` (common in games that pre-assign a transition slot). The riskier engine-only variant changes observable `getBlendAnim()->time` and should be avoided if scripts read it.

### Per-bone matrix pipeline computes a full 4x4 multiply and 4x4 fixed conversion for affine SRT matrices (tiny3d)

Location: [animModel.cpp:291](../n64/engine/src/scene/components/animModel.cpp#L291)

**Claim.** `t3d_skeleton_update` multiplies parent x local with the generic full 4x4 `t3d_mat4_mul`: 16 of the 64 `mul.s` and 12 of the 48 `add.s` compute the last column, which for affine SRT matrices is exactly (0,0,0,1). `t3d_mat4_scale` in `t3d_mat4_from_srt` also multiplies the always-zero `m[i][3]` (3 wasted `mul.s`), and `t3d_mat4_to_fixed` converts/stores the constant column. That is ~100-150 wasted FPU cycles per bone per frame, ~15-25% of the dominant per-bone cost, on every animated bone of every rendered skeleton.

**Evidence.** `t3dskeleton.c:96-102`: `t3d_mat4_from_srt(&tmp,...); t3d_mat4_mul(&bone->matrix, &skeleton->bones[boneDef->parentIdx].matrix, &tmp); ... t3d_mat4_to_fixed(&matStackFP[i], &bone->matrix);`. `t3dmath.h:503-513` is the full 4x4 loop; `t3dmath.c:238 t3d_mat4_scale(mat, ...)` scales all 4 columns; `t3dmath.c:183-207` already provides `t3d_mat4_to_fixed_3x4` which emits the identical RSP memory layout with an implied (0,0,0,1) column. Verification confirmed the `m[j][i]=sum_k matA[k][i]*matB[j][k]` indexing reduces the last column to copying `matB`'s last column for affine parents.

**Recommendation.** Dependency change (vendored tiny3d): add an affine-specialized `t3d_mat4_mul` (skip the last-column dot products, write 0,0,0,1 directly — bit-exact since the inputs are exact `0.0f`/`1.0f`), use a 3-column scale in `t3d_mat4_from_srt`, and switch `t3d_skeleton_update` to `t3d_mat4_to_fixed_3x4`. Output matrices are bit-identical. Every AnimModel picks this up automatically.

**How to measure.** `get_ticks()` around `t3d_skeleton_update` for a fixed 20+ bone model (or the AnimModel Comp Update row with anim decode unchanged); expect ~15-25% reduction in the skeleton-update share.

**Risk & functionality preservation (low).** The specialization is bit-exact because the eliminated arithmetic operates on exact-zero/one constants; `t3d_mat4_to_fixed_3x4` already emits the identical RSP layout. Preserves observable output.

### AnimModel submits the whole skinned model to the RSP every frame with no frustum check

Location: [animModel.cpp:309](../n64/engine/src/scene/components/animModel.cpp#L309)

**Claim (corrected during verification).** `AnimModel::draw` performs no built-in frustum test, so by default (no authored `Culling` component) an off-screen skinned character costs full RSP T&L + skinning matrix loads for every vertex every frame. Note that the engine's existing `Culling` component already provides this saving when authored and ordered before AnimModel; the gap is the lack of a built-in/opt-in cull.

**Evidence.** `animModel.cpp:294-313`: draw only early-outs on the stagger bit (`if(data->flags & (1)) return;`) then unconditionally `t3d_skeleton_use(&data->skelMain); t3d_matrix_set(mat, true); rspq_block_run(data->model->userBlock);`. `model.cpp:175-186` shows the culled path exists for the static `Model` component only. Verification confirmed the RSP-time saving is already achievable today via the `Culling` component (`culling.cpp:27-46` sets `IS_CULLED`; `scene.cpp:388` breaks the component loop) when it is authored and ordered before AnimModel — so this is a usability/opt-in-default improvement, not a new capability.

**Recommendation.** Add a cheap object-level bounding-sphere frustum test in `AnimModel::draw` (`t3d_frustum_vs_sphere` against `t3d_viewport_get()->viewFrustum`, ~25 FPU ops) using the model AABB scaled by `obj.scale` plus a user-configurable animation slack radius, behind an opt-in component flag (mirroring `Model::FLAG_CULLING`) so authors control the bound. Skipping draw for a truly off-screen model preserves visible output exactly; the opt-in flag keeps existing content unchanged by default.

**How to measure.** `RSPQ_PROFILE` dump (`scene.cpp:441-448`) or rdpq frame time with N characters positioned behind the camera, before/after; RSP T3D ucode time should drop per culled character.

**Risk & functionality preservation (medium).** Static AABB culling of a skinned character can pop if animation extends bounds beyond the rest-pose AABB — the slack radius mitigates this and must be author-tunable. Because the same saving is already available through the `Culling` component, the win over the status quo is automatic/built-in culling for content that lacks one; keep the new path opt-in to avoid changing existing behavior.

### With multiple cameras, the identical object matrix is recomputed and rewritten once per camera per frame

Location: [animModel.cpp:300](../n64/engine/src/scene/components/animModel.cpp#L300)

**Claim.** `Scene::draw` invokes component draw once per camera (`scene.cpp:362-395`). `AnimModel::draw` and `Model::draw` rotate the matrix ring and re-run `t3d_mat4fp_from_srt` on identical inputs for each camera, doubling (or tripling) the quat→mat + fixed-conversion + uncached-store cost per object per frame in split-screen/multi-camera scenes, and burning ring slots (2 rotations/frame on a 3-slot ring shrinks the RSP-read safety margin).

**Evidence.** `scene.cpp:362 for(auto &cam : cameras)` wraps the per-object draw loop calling `compDef.draw` each pass; `animModel.cpp:300-301` and `model.cpp:163-164` unconditionally do `data->matFP.getNext(); t3d_mat4fp_from_srt(...)` per call.

**Recommendation.** Stamp the component with `AnimController::getFrameCount()` when the matrix is filled; on subsequent draw calls in the same frame, reuse `matFP.get()` without rotating or refilling (`t3d_matrix_set` can point at the same slot for every camera). Contents are identical, so rendering is unchanged for any camera count.

**How to measure.** Two-camera scene: Comp Draw per-object time for Model/AnimModel should halve for the matrix-fill portion; single-camera scenes unchanged.

**Risk & functionality preservation (low).** Behavior-identical (same matrix bytes reused across cameras). Only relevant to multi-camera/split-screen. Partially subsumed by the static-transform SRT cache above, which would already short-circuit the second camera's fill; the frame-stamp still helps objects that moved this frame.

### `t3d_anim_update` performs a float divide per channel per frame for keyframe interpolation (tiny3d)

Location: [animModel.cpp:274](../n64/engine/src/scene/components/animModel.cpp#L274)

**Claim.** Every channel of every playing animation computes `interp = (time - timeStart) / timeDiff` each frame: `div.s` is ~29 cycles on the VR4300, so a 40-60 channel character burns ~1200-1700 cycles (~13-18us) per frame in divides alone (doubled while blending), even though `timeDiff` only changes when a keyframe is loaded.

**Evidence.** `t3danim.c:216-217`: `float timeDiff = target->timeEnd - target->timeStart; float interp = (anim->time - target->timeStart) / timeDiff;` inside the per-channel loop; `timeStart`/`timeEnd` are only modified in `load_keyframe` (`t3danim.c:173-175`). The cost figure is factually correct.

**Recommendation (corrected during verification).** Leave the divide in place: there is no bit-exact way to remove it. The interp numerator `(time - timeStart)` changes every frame, so per-channel work cannot be hoisted; only `div`→reciprocal-multiply would help, and `x*(1/d)` differs from `x/d` in the last ulp, which breaks the last-ulp skeletal determinism the project requires. Only adopt the `invTimeDiff` cache (store `1.0f/(timeEnd-timeStart)` in `T3DAnimTargetBase` at `load_keyframe`, multiply in `t3d_anim_update`) if bit-exact skeletal determinism is explicitly waived.

**How to measure.** `get_ticks()` around `t3d_anim_update` for a high-channel-count model over a fixed clip; expect ~10-20% reduction in anim-decode time *if* the non-bit-exact substitution is adopted.

**Risk & functionality preservation (medium → effectively non-actionable).** The only available optimization violates the project's exact-determinism guarantee for skeletal poses (visually identical, but not bit-exact). Under the current constraint this finding documents a real cost with no safe fix — impact downgraded to low accordingly.

### Two float divides for blend duration ratios recomputed every frame though durations only change on anim selection

Location: [animModel.cpp:270](../n64/engine/src/scene/components/animModel.cpp#L270)

**Claim.** While a blend anim is active, `AnimModel::update` computes `durMain/durBlend` and `durBlend/durMain` every frame: 2x `div.s` ≈ 58+ FPU cycles per blending model per frame, plus the guard selects. Both durations are fixed between `setMainAnim`/`setBlendAnim` calls, so the ratios are loop-invariant across frames.

**Evidence.** `animModel.cpp:270-271`: `float dtMain = ((1.0f - alpha) + alpha * (durMain / durBlend)) * dt; float dtBlend = ((1.0f - alpha) * (durBlend / durMain) + alpha) * dt;` — `mainAnimDuration`/`blendAnimDuration` are only written in `setMainAnim`/`setBlendAnim` (`animModel.cpp:49-85`, and `swapModel:111-112`).

**Recommendation.** Cache `ratioMainOverBlend` and `ratioBlendOverMain` in the component, recomputed inside `setMainAnim`/`setBlendAnim` (including the `>0` fallback-to-1 clamping). Same divides, same operand values, just executed once per selection instead of per frame — results bit-identical.

**How to measure.** Micro: ~60 cycles/frame per blending model; batch with other `AnimModel::update` changes and verify via the AnimModel Comp Update row.

**Risk & functionality preservation (low).** Bit-identical: same operands, same division, same clamp, moved to selection time. Engine-only, no editor-parity concern.

### A full skeleton bone-array clone is allocated for every animation in the model, but only the single blend slot is ever used

Location: [animModel.cpp:203](../n64/engine/src/scene/components/animModel.cpp#L203)

**Claim.** `initDelete`/`swapModel` allocate `skelAnim[animCount]` and clone bones for each (`t3d_skeleton_clone` mallocs `boneCount x sizeof(T3DBone) = boneCount x 108 bytes`). Only `skelAnim[animIdxBlend]` can ever be attached (main always uses `skelMain`), so a model with e.g. 10 anims and 20 bones wastes ~19.5KB of RDRAM per instance in cold bone arrays and fragments the heap, plus `animCount` malloc/memcpy pairs at spawn.

**Evidence.** `animModel.cpp:203,214-215`: `data->skelAnim = static_cast<T3DSkeleton*>(malloc(sizeof(T3DSkeleton) * animCount)); ... data->skelAnim[i] = t3d_skeleton_clone(&data->skelMain, false);` while `setBlendAnim` (`animModel.cpp:73`) only ever uses `activateAnim(idx, &skelAnim[idx])` for the one active blend index. `t3dskeleton.c:23-37` shows each clone (with `useMatrices=false`) mallocs and memcpys only the bone array. `sizeof(T3DBone)=108` (matrix 64 + scale 12 + rotation 16 + position 12 + hasChanged 4).

**Recommendation.** Replace the per-anim clone array with a single `skelBlend` clone; `activateAnim` already calls `t3d_anim_attach` on every (re)selection, which rebinds all channel target pointers to whichever skeleton is passed, so attaching the newly selected blend anim to the shared `skelBlend` is functionally identical.

**How to measure.** Overlay Memory tab / `heap_stats` before-after in a scene with multi-anim characters; spawn-time cost via `get_ticks` around `initDelete`.

**Risk & functionality preservation (medium).** RDRAM pressure, not per-frame CPU — no determinism concern in the sim path. Risk is that the public `getSkelAnim()` accessor (`animModel.h:99`) exposes the array to user code; keep the accessor returning the shared skeleton or audit callers first.

## Considered & Rejected

No findings were refuted during verification. All eleven survived review; two (`animation-animmodel-has-no-frustum-culling` and `animation-interp-div-per-channel-per-frame`) were **adjusted** — the corrected claim/recommendation/impact is used above. There are no contested findings.

## Suggested Order of Attack

1. **Empty `Model::update` guard + `ticksComponents` array swap** (medium impact, small effort, low risk). Independent, mechanical, and touches only `Scene::update`/`model.h`. It also cleans up the instrumentation path that every other finding is measured through, so do it first to get stable overlay numbers.
2. **`.sdata` keyframe streaming → `fmemopen` buffer** (medium, small, low). Independent of the skeleton work, removes mid-frame ROM PI stalls that pollute every frame-time measurement. Bounded RAM cost.
3. **Static-transform SRT cache in `Model`/`AnimModel::draw`** (medium, small, low). Independent; also subsumes most of the per-camera matrix-refill finding (#8), so land it before that one.
4. **Culled/hidden skeleton work → move `skeleton_blend`+`skeleton_update` into draw behind a frame-stamped dirty flag** (high, medium, medium). The largest single win. Do it after the cheap wins so its measurement is clean, and start with the zero-authoring HIDDEN gate before the full draw-pass move if the one-frame-stale-matrix caveat is a concern. Pairs naturally with:
5. **Built-in opt-in frustum cull in `AnimModel::draw`** (medium, medium, medium). Complements #4 on the RSP side; the CPU skeleton move and the RSP draw cull together deliver the full off-screen-character saving. Keep it opt-in with an author-tunable slack radius.
6. **Blend-anim advance-only mode (tiny3d)** (medium, medium, medium) and **affine bone-matrix specialization (tiny3d)** (medium, medium, low). Both are vendored-tiny3d changes; batch them into one tiny3d checkout update. The affine specialization is bit-exact and low risk; the advance-only mode is bit-exact but only pays off when blend targets sit at alpha≈0.
7. **Blend duration-ratio cache** (low, small, low) and **per-camera matrix frame-stamp** (low, small, low). Cheap cleanups to batch with the `AnimModel::update`/draw edits already in flight.
8. **Skeleton-clone-per-anim → single shared `skelBlend`** (low, small, medium). Memory-pressure win; do after auditing `getSkelAnim()` callers.
9. **Do NOT implement the interp-divide reciprocal substitution (#9)** unless bit-exact skeletal determinism is explicitly waived — it is the only listed change that breaks the project's determinism guarantee.
