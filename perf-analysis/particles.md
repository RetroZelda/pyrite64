# Particles Performance Analysis

_Date: 2026-07-06_

The runtime particle system is structurally sound for N64: a fixed 16-slot emitter pool, a cached SoA simulation, a u16 index sort, pre-recorded RDP setup blocks, and single-command tpx draws. Its steady-state CPU cost, however, is dominated by a small number of per-particle-scaling issues. The single largest steady cost at high particle counts is a back-to-front `std::sort` whose comparator recomputes squared camera distances on every comparison. The worst intermittent cost is `EmitterInstance::teardown()` calling `rspq_wait()` — a full RSP+RDP pipeline drain — in the middle of `Scene::update` every time any emitter finishes, which lands a multi-ms hitch on exactly the frames where fire-and-forget effects complete. Beyond these two, the tpx buffer fill issues nine narrow uncached RDRAM stores per particle, the write loop recomputes loop-invariant divisions the `-Os` compiler cannot hoist past `uint8_t` stores, and several per-emitter / per-component overheads (full SRT-euler matrix rebuild each frame, `atan2f`+`sinf`+`cosf` per particle component each frame, per-frame `rdpq_sprite_upload` command rebuilds) add steady tens of microseconds that dirty-flagging removes with zero behavior risk. None of the recommendations below change observable behavior or determinism, and none require edits to the editor-shared `particleSim.h` that would break preview parity except where explicitly flagged.

## Architecture & Frame Cost Model

**Per-frame update flow** (`Scene::update`, [scene.cpp:199](../n64/engine/src/scene/scene.cpp#L199) → `EmitterManager::update`):

1. **Lazy reclaim pass** ([emitterManager.cpp:101-103](../n64/engine/src/renderer/particles/emitterManager.cpp#L101)): every slot that is `used && !playing && refCount==0` is freed via `freeSlot` → `EmitterInstance::teardown`, which begins with a full `rspq_wait()` drain. This runs at the very top of update, while the previous triple-buffered frame's draw is still in flight.
2. **Per playing slot** (up to 16), `EmitterInstance::update`:
   - `ParticleSim::spawnCount` + `spawn()` per new particle;
   - `ParticleSim::step()` per alive particle with swap-pop cull over a cached `malloc`'d array of 44-byte `ParticleSim::Particle` structs;
   - a back-to-front `std::sort` of a `uint16` `drawOrder` index array whose comparator computes squared camera distance from `Particle.pos` on every comparison;
   - a field-by-field rewrite of every alive particle into a single `malloc_uncached` `TPXParticleS16` buffer (24 B per particle pair; ~9 narrow uncached stores per particle via `tpx_buffer_s16_get_*` accessors);
   - a full `T3DMat4FP` rebuild every frame (`t3d_mat4fp_from_srt_euler` = 6 `fm_sinf/cosf` + 12 float→fixed conversions + 8 uncached u64 stores, or `t3d_mat4fp_identity` = 64 uncached bytes for world-space anchors).

**Per-frame render flow** (`DrawLayer::drawPtx`, [drawLayer.cpp:118](../n64/engine/src/renderer/drawLayer.cpp#L118) → `EmitterManager::draw`): per emitter, `rspq_block_run(setupDPL)` (mode set + combiner + single-texture TMEM upload), `tpx_state_set_base_size`, a per-frame `rdpq_sprite_upload` for swap emitters (`texCount>1`), then `System::draw` → tpx matrix push, `tpx_particle_draw_tex_s16` (RSP DMA in 228-particle batches, 12 B/particle), matrix pop. This is a separate pipeline stage from update; the CPU runs ahead of the GPU (`SwapChain::nextFrame` blocks on a free framebuffer via VI interrupt, not on `rspq_wait`), which is why the mid-update `rspq_wait` collapses that pipelining.

**Per-object component flow** (`Comp::Particle::update`, [particle.cpp:67](../n64/engine/src/scene/components/particle.cpp#L67)): per object per frame, `handle.setPos` + `objectYaw` (quat math + `atan2f`) + `setYaw` (`sinf`+`cosf`, [emitterInstance.h:76](../n64/engine/include/renderer/particles/emitterInstance.h#L76)) + optional parent anchor. Note: `LIBDRAGON_FAST_MATH` is **not** defined for the engine build, so these are the accurate libm `sinf/cosf/atan2f`, not the `fm_` fast approximations.

**`particleSim.h` is shared verbatim** with the PC editor and must stay bit-identical on both sides; any change touching it is called out explicitly below.

**Existing instrumentation.** `EmitterManager::ticksUpdate` already wraps `update()` with `get_ticks` ([emitterManager.cpp:98,120](../n64/engine/src/renderer/particles/emitterManager.cpp#L98)), but **no overlay surfaces it** — the CPU overlay ([ovlCPU.cpp](../n64/engine/src/debug/overlay/ovlCPU.cpp)) lumps particle update into "Upd. Misc" (`ticksGlobalUpdate`, together with `AnimController` and global scripts). The component-side cost does appear in the `ovlComponents` "Comp Update" table under component ID 13, with per-object averaging. The first recommended instrumentation step for every finding below is to add a dedicated `ticksUpdate` row (and ideally sub-phase brackets around sort / fill / matrix) to `ovlCPU` so the numbers are visible on hardware.

## Findings Overview

| ID | Title | Impact | Effort | Risk | Status |
|----|-------|--------|--------|------|--------|
| particles-sort-recomputes-distance-per-comparison | Sort recomputes squared camera distance inside the comparator | high | small | low | confirmed |
| particles-teardown-rspq-wait-hitch | Emitter reclaim stalls CPU on full RSP+RDP drain mid-update | high | medium | medium | confirmed |
| particles-component-yaw-trig-every-frame | `atan2f`+`sinf`+`cosf` per component per frame even when unrotated | medium | small | low | adjusted |
| particles-matrix-rebuilt-every-frame | Anchor matrix fully rebuilt every frame even when static | medium | small | low | confirmed |
| particles-swap-tex-upload-rebuilt-per-frame | Swap emitters rebuild `rdpq_sprite_upload` commands every frame | medium | small | low | confirmed |
| particles-write-loop-invariant-recompute | Loop-invariant float div / cfg reloads inside the write loop | medium | small | low | confirmed |
| particles-duplicate-def-state-rerun | Same-def emitters re-run identical setup + TMEM upload | medium | small | medium | confirmed |
| particles-uncached-buffer-narrow-stores | tpx fill does 9 narrow uncached stores per particle | medium | medium | low | adjusted |
| particles-no-emitter-frustum-cull | Off-screen emitters burn full sim + fill + RSP transform | medium | medium | medium | confirmed |
| particles-spawn-invariant-normalize | `spawn()` renormalizes constant base velocity each spawn | low | small | low | confirmed |
| particles-sprites-add-narrow-stores | `Sprites::add` writes 9 narrow uncached stores per sprite | low | small | low | confirmed |
| particles-frame-anim-integer-div-per-particle | Animated emitters pay a 37-cycle integer modulo per particle | low | medium | low | confirmed |
| particles-dead-rotation-fields-in-hot-struct | 8 dead bytes (`angle0`/`spin`) inflate Particle D-cache footprint | low | medium | medium | confirmed |

## Detailed Findings

### Sort recomputes squared camera distance inside the comparator

[emitterInstance.cpp:245](../n64/engine/src/renderer/particles/emitterInstance.cpp#L245)

**Claim.** `std::sort`'s comparator computes **two** 3D squared distances (6 subs, 6 muls, 4 adds, plus two 12-byte loads at 44-byte stride) on every comparison. For n=128 particles that is ~900 comparisons ≈ 1800 dot products with cache-hostile random access into a 5.6 KB `Particle` array (70% of the 8 KB direct-mapped D-cache), instead of 128 dots computed once. At 100-250 particles this sort is plausibly the single largest CPU cost of the system (several hundred µs), and it scales n·log n with alive count. Verification: at n=128, ~900 comparisons × ~60 cy ≈ 54000 cy ≈ 576 µs; even at the default `maxParticles=64` cap this is ~245 µs. This sort is the only super-linear term in an otherwise O(n) per-particle pipeline.

**Evidence.**

```cpp
const bool doSort = haveCam && system->count > 1;
if(doSort) {
  ParticleSim::Vec3 camLocal{camPos.x - follow.x, camPos.y - follow.y, camPos.z - follow.z};
  for(uint32_t i = 0; i < system->count; ++i) drawOrder[i] = (uint16_t)i;
  std::sort(drawOrder, drawOrder + system->count, [&](uint16_t a, uint16_t b) {
    ParticleSim::Vec3 da = parts[a].pos - camLocal;
    ParticleSim::Vec3 db = parts[b].pos - camLocal;
    return ParticleSim::dot(da, da) > ParticleSim::dot(db, db); // farthest first
  });
}
```

`std::sort` invokes the lambda separately per `(a,b)` pair, so `-Os` cannot cache a per-particle key across distinct comparator calls — this is a genuine algorithmic change, not something the compiler already does.

**Recommendation.** Precompute the squared distance once per particle into a per-emitter float key array (allocate `maxParticles` floats next to `drawOrder`; fill `sortKey[i] = dot(parts[i].pos - camLocal, …)` in the same loop that writes `drawOrder[i]=i`), then sort comparing `sortKey[a] > sortKey[b]`. The key expression is the identical float computation, so keys are bit-identical, `sortKey[a] > sortKey[b]` yields identical booleans for every pair, and `std::sort` output (including unstable-sort tie handling) is a bit-identical permutation. The key array is engine-side next to `drawOrder` and does **not** touch `particleSim.h`, so editor parity is untouched. A further (slightly riskier w.r.t. tie ordering) option packs the non-negative float bits with the index into a `uint64` and sorts integers.

**How to measure.** Bracket the sort block with `get_ticks` and print via the debug overlay; stress a single emitter at 200+ alive particles; compare µs before/after (expect ~2-4x reduction of the sort phase). `EmitterManager::ticksUpdate` gives the aggregate.

**Risk & functionality preservation.** Risk low. Both reviewers confirmed the precomputed-key permutation is bit-identical to the current comparator's output. The only nuance: at the default 64-particle cap the saving is ~150-200 µs rather than the headline "several hundred," which applies at the cited 100-250 range — consistent with the finding's own scaling framing.

### Emitter reclaim stalls the CPU on a full RSP+RDP pipeline drain mid-update

[emitterInstance.cpp:175](../n64/engine/src/renderer/particles/emitterInstance.cpp#L175)

**Claim.** `EmitterInstance::teardown()` calls `rspq_wait()`, which (per libdragon `rspq.c:1725`) issues an `rdpq_fence` and blocks until the RSP queue **and** RDP are fully drained. It is invoked from `EmitterManager::update`'s lazy-cleanup pass ([emitterManager.cpp:101-103](../n64/engine/src/renderer/particles/emitterManager.cpp#L101)), i.e. in the middle of `Scene::update` while the triple-buffered `DrawLayer` queues from the previous frame are still executing. Every time any fire-and-forget emitter drains (or a component destroys its emitter), the next update blocks for potentially several milliseconds — an intermittent but severe frame hitch. The freed `System` is then immediately re-allocated on the next `play()` (`malloc_uncached` + `sys_hw_memset` + rspq block rebuild in `init()`).

**Evidence.**

```cpp
void P64::PTX::EmitterInstance::teardown()
{
  // The RSP/RDP may still be reading our buffers/block from a prior triple-buffered frame.
  rspq_wait();
  if(setupDPL) { rspq_block_free(setupDPL); setupDPL = nullptr; }
  if(system) { delete system; system = nullptr; }   // frees the uncached tpx buffer
  ...
```

And the caller, run before the sim loop every frame:

```cpp
// lazy cleanup: reclaim idle, unreferenced slots
for(auto &s : slots) {
  if(s.used && !s.playing && s.refCount == 0) freeSlot(s);
}
```

Both reviewers confirmed the CPU genuinely runs ahead of the GPU: `SwapChain::nextFrame` blocks on a free framebuffer via VI interrupt (`kirq_wait`), **not** `rspq_wait`, then kicks `drawTask` asynchronously — so at update time the previous frame's RSP/RDP work is still executing, and the `rspq_wait` collapses that pipelining into a hard sync. The wait is genuinely necessary in the current immediate-free design: the reclaimed emitter's uncached tpx buffer and `setupDPL` block are referenced by the still-in-flight previous frame's draw, so freeing without a sync is a use-after-free for RSP DMA.

**Recommendation.** Defer reclamation instead of syncing: when an emitter finishes, move its slot to a pending-free list stamped with a frame counter (or an `rspq_syncpoint_new()` taken after its last draw) and run `teardown` only once 3 frames have elapsed / `rspq_syncpoint_check` passes — no `rspq_wait` needed because nothing can still reference the buffers. Keep `rspq_wait` only as a fallback when `alloc()` finds the pool full of pending-free slots. Optionally also cache/reuse `System` buffers per def to avoid the `malloc_uncached`+memset+block rebuild on the next `play()`.

**How to measure.** On hardware, log `get_ticks` around `freeSlot`/`teardown` while repeatedly playing a one-shot emitter (e.g. every 2 s); observe multi-ms spikes in `EmitterManager::ticksUpdate` (surface it in `ovlCPU` — currently computed but never displayed) exactly on the frame each emitter drains; verify they disappear after the change.

**Risk & functionality preservation.** Risk medium. Teardown only frees memory; deferral changes nothing observable and does not touch `particleSim.h`, so determinism and editor/hardware parity are untouched. Magnitude is bounded by the in-flight frame's remaining GPU work — near-zero in a CPU-bound scene, up to several ms when GPU-bound — so the "potentially several milliseconds" wording is correctly hedged. It is intermittent (fires only on reclaim frames; multiple frees in one pass pay only the first drain) but a full-pipeline drain is a genuinely severe frame-pacing hitch that tends to land on busy combat/burst frames. `-Os` cannot perform this architectural change.

### `Comp::Particle::update` runs `atan2f`+`sinf`+`cosf` per object per frame even when the object has not rotated

[particle.cpp:70](../n64/engine/src/scene/components/particle.cpp#L70)

**Claim (adjusted).** `particle.cpp update()` runs `atan2f` + `sinf` + `cosf` (**standard libm, not the `fm_` fast versions** — `LIBDRAGON_FAST_MATH` is undefined in the engine build) per particle-component per frame regardless of whether `obj.rot` changed, and even for emitters that were `create()`d but never played. These are ~1000-2000 cycles/object (the original ~250-400 cy estimate assumed fast-math), so the waste for static/non-rotating emitters is larger than a fast-math estimate suggests. Yaw only affects future spawns. Cost scales linearly with the number of particle-bearing objects.

**Evidence.** `objectYaw` does quaternion-forward math + `atan2f`; `Handle::setYaw` → `EmitterInstance::setYaw` ([emitterInstance.h:76](../n64/engine/include/renderer/particles/emitterInstance.h#L76)) does `cfg.yawSin = sinf(yawRad); cfg.yawCos = cosf(yawRad);`. `update()` calls `setPos` + `setYaw(objectYaw(obj))` unconditionally every frame.

**Recommendation.** Cache the last quaternion (or last `fwdX`/`fwdZ` pair) in `Comp::Particle` and skip `objectYaw`/`setYaw` entirely when `obj.rot` is bit-identical to the cached value (the overwhelmingly common case). When it did change, the recomputed `sinf(atan2f(...))` values are the same expressions on the same inputs, so spawn behavior stays bit-exact. Do **not** replace the trig with the algebraic fwd/len normalization — that changes `yawSin`/`yawCos` LSBs and hence spawn determinism.

**How to measure.** Debug overlay "Comp Update" table, component ID 13 (already instrumented per-component): compare per-object µs with N static emitter-bearing objects before/after.

**Risk & functionality preservation.** Risk low. The cache-and-skip is bit-identical because only the observable output (`yawSin`/`yawCos` fed to future spawns) is preserved, from bit-identical inputs. No `particleSim.h` change.

### Anchor matrix fully rebuilt with `t3d_mat4fp_from_srt_euler` every frame per emitter, even when static

[emitterInstance.cpp:283](../n64/engine/src/renderer/particles/emitterInstance.cpp#L283)

**Claim.** `update()` unconditionally rebuilds the `T3DMat4FP` each frame: `SPACE_EMITTER`/`SPACE_PARENT` emitters call `t3d_mat4fp_from_srt_euler` with constant scale `{1,1,1}` and rotation `{0,0,0}` — which evaluates 6 `fm_sinf/fm_cosf` calls, ~20 float muls, 12 float→s16.16 conversions and 8 uncached 64-bit stores — and `SPACE_WORLD` emitters rewrite a constant identity (64 uncached bytes) every single frame. For an emitter whose anchor did not move (torches, fountains, world-space effects) this is all redundant; cost is roughly 600-900 cycles (~7-10 µs) per emitter per frame, times up to 16 pool slots. (The `mat` buffer is `malloc_uncached`, [emitterInstance.cpp:163](../n64/engine/src/renderer/particles/emitterInstance.cpp#L163).)

**Evidence.** At the end of every `update()`:

```cpp
if(mat) {
  if(cfg.simSpace == ParticleSim::SPACE_WORLD) {
    t3d_mat4fp_identity(mat);
  } else {
    t3d_mat4fp_from_srt_euler(mat,
      (float[]){1.0f, 1.0f, 1.0f}, (float[]){0, 0, 0}, (float[]){follow.x, follow.y, follow.z});
  }
```

**Recommendation.** Write the identity once in `init()`/`reset()` and never again for `SPACE_WORLD`. For the translation-only case, keep a cached `fm_vec3_t lastFollow`: skip when `follow` is bit-identical; when it changed, skip the euler path entirely — the matrix is identity+translation, so update only row 3 via `t3d_mat4fp_set_pos(mat, ...)` ([t3dmath.h:380](../../../tiny3d/src/t3d/t3dmath.h#L380)) or hand-pack the three s16.16 values into two u64 stores. `T3D_F32_TO_FIXED` of the same floats produces identical fixed-point bytes, so the RSP sees a bit-identical matrix.

**How to measure.** `get_ticks` around the matrix block with 8-16 static emitters playing; expect ~50-100 µs/frame reclaimed in `EmitterManager::ticksUpdate` at full pool.

**Risk & functionality preservation.** Risk low. Bit-identical matrix bytes. Note the scoping: for `SPACE_EMITTER` emitters bound to a moving object (`follow=pos`, updated via the component `setPos` path), `follow` changes each frame so the cache only helps static emitters — which is exactly the torch/fountain/world case the finding targets. No `particleSim.h` change.

### Swap-texture emitters rebuild `rdpq_sprite_upload` commands on the CPU every frame; `lastSwapIdx` dead

[emitterInstance.cpp:309](../n64/engine/src/renderer/particles/emitterInstance.cpp#L309)

**Claim.** For `def->texCount > 1`, `draw()` calls `uploadTexTile` → `rdpq_sprite_upload` every frame, re-parsing the sprite and re-emitting tile/load commands into the command stream on the CPU each time (a few µs per emitter per frame), even though the set of possible uploads is fixed at init. The member `lastSwapIdx` ([emitterInstance.h:45](../n64/engine/include/renderer/particles/emitterInstance.h#L45)) that was evidently meant to skip redundant work is written in `reset()` ([emitterInstance.cpp:192](../n64/engine/src/renderer/particles/emitterInstance.cpp#L192)) and never read — confirmed dead. The TMEM upload itself must still happen each frame (other emitters clobber TILE0), but the CPU-side command building need not.

**Evidence.** In every `draw()`: `if(def->texCount > 1) { uint32_t si = ParticleSim::swapIndexForTime(cfg, emitterTime); ... uploadTexTile(texSprites[si], ..., cfg.texFrames); }`. Grep shows `lastSwapIdx` has no readers.

**Recommendation.** At `init()`, pre-record one rspq block per swap texture (same `uploadTexTile` contents — identical commands, so RDP state is bit-identical) and in `draw()` just `rspq_block_run(texBlocks[si])`. Remove or actually use `lastSwapIdx`. Memory cost: `texCount` small blocks per emitter.

**How to measure.** `get_ticks` around `EmitterManager::draw` with a `texCount=4` swap emitter; compare CPU µs per frame before/after (the RDP-side TMEM load time is unchanged).

**Risk & functionality preservation.** Risk low. Pre-recorded block emits identical commands, so the RDP sees bit-identical state; the TMEM load still runs every frame. Only affects swap emitters (`texCount>1`), which are less common. No `particleSim.h` change.

### Loop-invariant float division and cfg/tint reloads inside the per-particle buffer-write loop

[emitterInstance.cpp:264](../n64/engine/src/renderer/particles/emitterInstance.cpp#L264)

**Claim.** `hwSizeByte` ([particleSim.h:239-245](../n64/engine/include/renderer/particles/particleSim.h#L239)) computes `sizeAt(cfg,t) * (127.0f / sizeQuantMax(cfg))` — the `127/sizeQuantMax` term contains a `div.s` (~29 cy) and two compares that are constant across the whole loop but are recomputed per particle. Because the loop stores through `uint8_t*`/`int8_t*` pointers (which alias everything under TBAA), gcc at `-Os` cannot prove `cfg`/`tint` members unchanged and must reload/recompute them each iteration. The same applies to the four `tint[]` member loads ([:269-272](../n64/engine/src/renderer/particles/emitterInstance.cpp#L269)) and the `cfg.colorStart/End` loads inside `colorAt`. At 200 particles this is roughly 8-15 µs of pure recomputation. Additionally, when the emitter has no `EMAT_PRIM` material color, `tint` is exactly `1.0f` yet 4 `mul.s` per particle are still executed.

**Evidence.** `*tpx_buffer_s16_get_size(buff, i) = (int8_t)ParticleSim::hwSizeByte(cfg, t);` with `hwSizeByte = "float s = sizeAt(cfg, t) * (127.0f / sizeQuantMax(cfg));"`, and `rgba[0] = (uint8_t)(col[0] * tint[0] * 255.0f); ...` inside the same loop as the `uint8_t` stores.

**Recommendation.** Before the loop, hoist into locals: `const float sizeScale = 127.0f / ParticleSim::sizeQuantMax(cfg);` (add an overload/param so `hwSizeByte` can take the precomputed scale — same multiply, bit-identical; mirror the tiny helper in the editor since `particleSim.h` is shared), local copies of `tint[4]` and the 8 color endpoints, and a `bool hasTint` fast path that skips the tint multiplies when all four are `1.0f` (multiplying by `1.0f` is IEEE-exact, so the skip is bit-identical). Also hoist the `doSort` ternary out by pointing at an identity-filled `drawOrder` when not sorting.

**How to measure.** `get_ticks` around the write loop at 200 particles; expect ~10 µs saved; verify byte-identical buffer contents by checksumming the tpx buffer before/after in an emulator run with a fixed seed.

**Risk & functionality preservation.** Risk low. Hoisting produces the same multiplies; the `hasTint` fast-path is IEEE-exact. If the `sizeScale` overload is added to `particleSim.h`, it must be mirrored identically on the editor side (same expression) to preserve parity.

### Consecutive emitters sharing a def re-run identical mode/combiner/TMEM-upload setup blocks

[emitterManager.cpp:125](../n64/engine/src/renderer/particles/emitterManager.cpp#L125)

**Claim.** `EmitterManager::draw` runs each playing instance's `setupDPL` block ([emitterInstance.cpp:297](../n64/engine/src/renderer/particles/emitterInstance.cpp#L297)) which re-sends `SET_OTHER_MODES`, the combiner, and — for single-texture emitters — a full `rdpq_sprite_upload` TMEM load ([emitterInstance.cpp:121](../n64/engine/src/renderer/particles/emitterInstance.cpp#L121)). Several instances of the same effect (e.g. 5 torches from one def) therefore force per-emitter RDP pipe/tile/TMEM syncs and re-upload the exact same texture 5 times per frame, stalling the RDP pipeline between emitters for zero state change.

**Evidence.** `for(auto &s : slots) { if(s.used && s.playing) s.inst.draw(camBaseSize); }` with `draw()` starting `rspq_block_run(setupDPL);` unconditionally; the block contains `rdpq_mode_*` plus `uploadTexTile` for `texCount<=1`.

**Recommendation.** Track the set of `EmitterDef*` already set up within one `EmitterManager::draw` pass and skip `rspq_block_run` when the next emitter has a def already set up this pass AND both are single-texture (swap emitters upload outside the block and already differ) — the RDP state at that point is bit-identical to what the block would set, since setup blocks are pure functions of the def and tpx draws do not modify mode/tile state. Reset the cache at the start of each `draw()` call. Per-emitter base-size/tex-params writes stay as-is.

**How to measure.** rdpq profiler / overlay RDP-busy bar with 5+ same-def emitters on screen; count TMEM load commands per frame via `rdpq` trace in an emulator before/after.

**Risk & functionality preservation.** Risk medium. Skipping is valid because tpx draws don't touch rdpq mode/tile, so state persists between emitters. Caveat from verification: slots iterate in fixed pool order, so a simple "last-drawn def" cache elides only pool-adjacent duplicates — track the **set** of defs already set up this pass to catch non-adjacent duplicates too. No `particleSim.h` change.

### tpx buffer fill does 9 narrow uncached stores per particle instead of 3 doubleword stores per pair

[emitterInstance.cpp:253](../n64/engine/src/renderer/particles/emitterInstance.cpp#L253)

**Claim (adjusted).** The per-particle write loop ([emitterInstance.cpp:254-276](../n64/engine/src/renderer/particles/emitterInstance.cpp#L254)) fills a `malloc_uncached` `TPXParticleS16` buffer (uncached confirmed at [ptxSystem.cpp:16](../n64/engine/src/renderer/particles/ptxSystem.cpp#L16)) with ~9 narrow stores per particle: 3× int16 pos, 1× int8 size, 4× uint8 rgba, 1× uint8 texOffset, each through a branchy `(idx&1)` accessor. The struct is `aligned(8)` with three 8-byte-aligned groups per pair (offsets 0/8/16), so a two-at-a-time doubleword rewrite (3× `sd` per pair) is layout-valid, and `-Os` will not coalesce these scattered, branch-separated stores on its own. The cost and the coalescing win are real, but the byte savings are tempered by the VR4300 write buffer partially absorbing back-to-back uncached stores, so the realistic steady-state saving is a few to low-tens of µs/frame and scales with alive count, not the upper bound implied. (Impact corrected from high to **medium**; the "6x" figure is per-pair — 18 narrow stores → 3 `sd`; per-particle it is 9 → ~1.5, i.e. ~3x fewer store instructions.)

**Evidence.** The write loop stores field-by-field (see the excerpt under the write-loop-invariant finding). `TPXParticleS16` ([tpx.h:61-70](../../../tiny3d/src/t3d/tpx.h#L61)) is `aligned(8)`, `sizeof==24`, with groups `{posA,sizeA,texOffA}` @0, `{posB,sizeB,texOffB}` @8, `{colorA,colorB}` @16.

**Recommendation.** Process particles two at a time and issue three 64-bit stores per `TPXParticleS16` pair, **but preserve the exact byte layout the current accessors produce**. Critically, `tpx_buffer_s16_get_tex_offset` ([tpx.h:310-311](../../../tiny3d/src/t3d/tpx.h#L310)) is **inverted** versus pos/size/rgba: even idx → `texOffsetB` (byte 15), odd idx → `texOffsetA` (byte 7). So for a pair (A=even, B=odd): `dword@0 = posA|sizeA|(B's texOffset in byte 7)`; `dword@8 = posB|sizeB|(A's texOffset in byte 15)`; `dword@16 = colorA|colorB`. The naive "each particle's own texOffset in its own dword" formula **swaps** the two frame offsets and is not bit-identical for textured/animated emitters. With the cross-placement corrected, RSP-visible bytes are exact. Handle the odd tail by writing the partner with `size=0` (matching `System::draw` [ptxSystem.cpp:31-37](../n64/engine/src/renderer/particles/ptxSystem.cpp#L31), which already zeroes the tail size).

**How to measure.** `get_ticks` around the buffer-write loop at 200+ alive particles (expect tens of µs saved, scaling with count); also watch RDP busy % on the overlay since fewer RDRAM write transactions reduce bus contention during the parallel RDP work. Verify by checksumming the tpx buffer before/after in an emulator with a fixed seed, especially on a `TEX_A_S16` animated emitter.

**Risk & functionality preservation.** Risk low **only if** the tex_offset cross-placement is reproduced exactly — the original "bytes are identical" claim was false as written for textured animated emitters, which is why this finding was downgraded to `adjusted`. The change touches only the engine-side write loop, not `particleSim.h`; the sim values feeding the buffer are unchanged, so preview parity is unaffected.

### Off-screen emitters still burn full CPU sim + buffer write + RSP transform + draw submission

[emitterManager.cpp:123](../n64/engine/src/renderer/particles/emitterManager.cpp#L123)

**Claim.** Neither `update()` nor `draw()` considers the camera frustum: an emitter entirely behind the camera still pays the whole per-particle CPU pipeline (sort + uncached writes) and dispatches all its particles to the RSP, which transforms and rejects them one by one (tpx ucode clips per particle). The CPU sim itself must keep running for determinism, but the sort, buffer fill and tpx submission produce nothing visible for a fully-culled emitter.

**Evidence.** `void P64::EmitterManager::draw(float camBaseSize) { for(auto &s : slots) { if(s.used && s.playing) s.inst.draw(camBaseSize); } }` — no visibility test anywhere; `update()` likewise sorts and fills the tpx buffer unconditionally ([emitterInstance.cpp:241-276](../n64/engine/src/renderer/particles/emitterInstance.cpp#L241)).

**Recommendation.** Track a cheap world-space bounding sphere during the existing per-particle write loop (running min/max of the three position components — 6 compares per particle, no extra pass), inflate it by the max on-screen particle size, and test it with `t3d_frustum_vs_sphere` in `draw()`; skip `rspq_block_run`/`System::draw` when fully outside (keep the sim + buffer write so re-entry is seamless and determinism is untouched). To also skip the sort+buffer-fill, use the **previous** frame's bounds with a one-frame-late conservatism margin.

**How to measure.** Point the camera away from a 200-particle emitter: `EmitterManager::ticksUpdate` should drop by the sort+fill share, and the RSP-busy overlay bar should drop by the tpx transform share; verify no popping when panning back.

**Risk & functionality preservation.** Risk medium (popping on re-entry if the conservatism margin is wrong). The sim keeps running for determinism; only sort/fill/submit are gated. Net win is situational — the recommendation adds a small per-particle bounding-sphere cost (6 compares) to the common on-screen case, and many emitters are on-screen at gameplay locations. No `particleSim.h` change.

### `spawn()` renormalizes the constant base velocity and re-divides `spread/90` for every spawned particle

[particleSim.h:184](../n64/engine/include/renderer/particles/particleSim.h#L184)

**Claim.** Every spawn computes `normalize(cfg.velocity)` (dot + `sqrt.s` ~29 cy + `div.s` ~29 cy) and `cfg.spread / 90.0f` (`div.s` ~29 cy) even though both depend only on the immutable `Config` — roughly 100+ cycles of FPU stall per spawn that is pure recomputation. Negligible for 20/sec continuous emitters, but a 64-particle one-shot burst wastes ~7K cycles (~75 µs total spawn-cost spike including the unavoidable shape/direction sqrts) in a single frame.

**Evidence.** `Vec3 base = normalize(cfg.velocity); float spreadAmt = cfg.spread / 90.0f;` inside `spawn()`, called once per emitted particle in the loop at [emitterInstance.cpp:223-226](../n64/engine/src/renderer/particles/emitterInstance.cpp#L223).

**Recommendation.** Add precomputed fields (`velocityNorm`, `spreadAmt`) to `Config`, filled by a shared `finalizeConfig()` helper in `particleSim.h` that both the engine's `configFromDef` and the editor's config builder call — normalize/divide once on the same inputs yields bit-identical values, so every spawn result and the RNG sequence are unchanged (these computations don't consume rng).

**How to measure.** `get_ticks` around the spawn loop while triggering a 64-count one-shot; compare the spike before/after; verify determinism by dumping the first N spawned particles for a fixed seed.

**Risk & functionality preservation.** Risk low. Same inputs → bit-identical precomputed values; RNG stream untouched. Because this adds a shared `finalizeConfig()` in `particleSim.h`, both platforms must call it identically to preserve parity.

### `PTX::Sprites::add` writes 9 narrow uncached stores per sprite

[ptxSprites.cpp:84](../n64/engine/src/renderer/particles/ptxSprites.cpp#L84)

**Claim.** The jam25 sprite path performs the identical narrow-uncached-store pattern per `add()`: 3 int16 pos, 1 int8 size, 1 uint8 texOffset, 4 uint8 color stores into the `malloc_uncached` `TEX_A_S16` buffer, plus 4 branchy accessor index computations — ~9 RDRAM write transactions per call, invoked per game-spawned sprite per frame by user code.

**Evidence.** `auto p = tpx_buffer_s16_get_pos(buff, system.count); p[0..2] = ...; *tpx_buffer_s16_get_size(...) = ...; *tpx_buffer_s16_get_tex_offset(...) = offset; auto c = tpx_buffer_s16_get_rgba(...); c[0..3] = ...`.

**Recommendation.** Assemble the 8-byte `{pos,size,texOffset}` group and the 4-byte color in registers and issue one `uint64` + one `uint32` store (both naturally aligned within the 24-byte pair struct) — identical bytes written, 2 transactions instead of 9. The same tex_offset A/B accessor-swap caveat as the emitter fill finding applies ([tpx.h:310](../../../tiny3d/src/t3d/tpx.h#L310) inverts A/B) — packing must reproduce the exact byte placement.

**How to measure.** `get_ticks` around the caller's `add()` burst (e.g. N adds per frame) before/after; scales with sprite count.

**Risk & functionality preservation.** Risk low, provided the tex_offset byte placement is reproduced. This is a usage-dependent user/jam25 path, so cost is fully caller-driven. No `particleSim.h` change.

### Animated emitters pay a 37-cycle integer modulo per particle per frame

[particleSim.h:281](../n64/engine/include/renderer/particles/particleSim.h#L281)

**Claim.** `frameForParticle` for `ANIM_LOOP` executes `f % frames` and `ANIM_RANDOM_STATIC` executes `p.seed % frames`, where `frames` is a runtime `uint32` — MIPS integer DIV is ~37 cycles, per alive particle per frame (200 particles ≈ 7400 cycles ≈ 80 µs). This is paid **only** for animated textured emitters: the function early-returns when `frames<=1 || ANIM_OFF`, so `ANIM_OFF` emitters pay nothing. The `float t = ParticleSim::lifeT(p)` at [emitterInstance.cpp:256](../n64/engine/src/renderer/particles/emitterInstance.cpp#L256) additionally costs a `div.s` (~29 cy, `p.age / p.lifetime`) per particle. The modulo divisor is loop-invariant per emitter (`frames` is 2..16), so it can be replaced with an exact multiply-shift reciprocal.

**Evidence.** `case ANIM_RANDOM_STATIC: return p.seed % frames; case ANIM_LOOP: default: { uint32_t f = (uint32_t)((p.age * cfg.frameRate)); return f % frames; }` — `frames = logicalFrameCount(cfg)`, constant for the emitter's lifetime.

**Recommendation.** Precompute a magic multiply-shift reciprocal for `frames` once per `Config` (libdivide-style u32 division is exact for all inputs, so results are bit-identical) in a shared config-finalize helper inside `particleSim.h` so editor and engine stay in lockstep; use it in `frameForParticle`. Leave `lifeT` as-is — replacing that division with a stored reciprocal is **not** bit-exact and would break editor/hardware parity; note it as an accepted per-particle cost.

**How to measure.** `get_ticks` around the write loop with an `ANIM_LOOP` emitter at 200 particles vs `ANIM_OFF`; the delta isolates the animation-path cost; expect it to drop by roughly half after the reciprocal change.

**Risk & functionality preservation.** Risk low; the reciprocal is exact and bit-identical. Effort is medium because the reciprocal must live in the shared `particleSim.h` and be finalized identically on both platforms.

### 8 dead bytes (`angle0`/`spin`) in every 44-byte Particle inflate D-cache footprint

[particleSim.h:144](../n64/engine/include/renderer/particles/particleSim.h#L144)

**Claim.** `ParticleSim::Particle` carries `angle0` and `spin`, but the engine never consumes them: `angleAt()` ([particleSim.h:204](../n64/engine/include/renderer/particles/particleSim.h#L204)) has zero callers in `n64/engine` (grep-verified) and tpx has no per-particle rotation, so 8 of 44 bytes per particle are dead on hardware — ~18% extra D-cache pressure in the step/sort/fill loops (128 particles: 5.6 KB vs 4.6 KB in an 8 KB direct-mapped cache) and 8 extra bytes copied on every swap-pop death ([emitterInstance.cpp:230-234](../n64/engine/src/renderer/particles/emitterInstance.cpp#L230)).

**Evidence.** `struct Particle { Vec3 pos; Vec3 vel; float age; float lifetime; uint32_t seed; float angle0; float spin; };`, with `spawn()` filling them via `p.angle0 = rng.range(cfg.rotationMin, cfg.rotationMax); p.spin = rng.range(cfg.spinMin, cfg.spinMax);` — only the editor preview reads them.

**Recommendation.** Since `particleSim.h` is shared, either (a) guard the two fields with a platform define (editor keeps them; engine drops them) while **keeping both `rng.range` calls in `spawn()` unconditionally** so the RNG stream and all other spawned values stay bit-identical on both platforms, or (b) at minimum document them as editor-only. Option (a) needs a matching editor-side check that it never compiles the engine flavor.

**How to measure.** `get_ticks` on the step+sort+fill phases at 200 particles before/after; expect a few percent from reduced cache misses; verify parity by comparing a fixed-seed particle dump (pos/vel/age/lifetime/seed) between editor and engine builds.

**Risk & functionality preservation.** Risk medium — this edits the shared single-source-of-truth header, so the RNG-stream preservation (keep both `rng.range` calls) and the editor-side guard are load-bearing for parity. The win is marginal (a few percent; step/sort/fill mostly touch pos/vel), so this is correctly the lowest-priority item.

## Considered & Rejected

No findings were refuted during verification. All 13 findings survived review: 11 confirmed as written, and 2 adjusted (`particles-uncached-buffer-narrow-stores` corrected to medium impact with a mandatory tex_offset cross-placement fix, and `particles-component-yaw-trig-every-frame` corrected to note standard libm rather than fast-math trig — which raises, not lowers, its cost). No finding was contested (no split reviewer verdict).

## Suggested Order of Attack

1. **Instrument first.** Surface `EmitterManager::ticksUpdate` as its own row in `ovlCPU` (currently computed but never displayed), and add optional `get_ticks` brackets around the sort / fill / matrix sub-phases. Every measurement below depends on this, and it is zero-risk.
2. **Sort key precompute** (`particles-sort-recomputes-distance-per-comparison`) — highest impact, small effort, low risk, engine-only, bit-identical. This is the biggest steady-state win and should go first among the real changes.
3. **Deferred emitter reclaim** (`particles-teardown-rspq-wait-hitch`) — highest-impact intermittent hitch; medium effort, medium risk (touches the pool lifetime and needs a syncpoint/frame-counter fallback path). Do it after the sort so the steady-state overlay numbers are stable before you start chasing multi-ms spikes.
4. **Cheap per-emitter dirty-flag wins**, all small effort / low risk / engine-only, batchable together: matrix rebuild caching (`particles-matrix-rebuilt-every-frame`), component yaw caching (`particles-component-yaw-trig-every-frame`), write-loop invariant hoisting (`particles-write-loop-invariant-recompute`), and swap-texture block pre-recording (`particles-swap-tex-upload-rebuilt-per-frame`).
5. **Duplicate-def setup skip** (`particles-duplicate-def-state-rerun`) — small effort but medium risk (RDP-state reasoning); pairs naturally with the swap-texture block work since both live in the draw path.
6. **tpx doubleword fill** (`particles-uncached-buffer-narrow-stores`) and the mirrored **sprite fill** (`particles-sprites-add-narrow-stores`) — do these together, and only after implementing the tex_offset A/B cross-placement carefully; validate with a fixed-seed buffer checksum on a `TEX_A_S16` animated emitter before trusting either.
7. **Frustum cull** (`particles-no-emitter-frustum-cull`) — medium effort/risk; benefits from steps 2 and 6 already having reduced the per-emitter cost, so measure whether it is still worth the popping risk in your actual scenes.
8. **Shared-header `particleSim.h` changes**, lowest priority and highest parity risk, done last as a batch so the editor is re-validated once: spawn-invariant precompute (`particles-spawn-invariant-normalize`), animation reciprocal (`particles-frame-anim-integer-div-per-particle`), and the dead-field guard (`particles-dead-rotation-fields-in-hot-struct`). Several of these want a shared `finalizeConfig()` helper, so land that scaffolding once and hang all three off it.
