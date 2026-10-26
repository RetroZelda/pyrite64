# Collision System Performance Analysis

**Date:** 2026-07-06
**Target:** N64 hardware (VR4300 @ 93.75 MHz, libdragon + tiny3d)
**Scope:** `n64/engine/src/collision/*` — narrowphase, sweeps, raycasts, scene/broadphase, pair management.

## Executive summary

The collision pipeline is algorithmically sound — a Box2D/Bullet-style scene with dynamic AABB trees, cached warm-started constraints, island sleeping, analytical fast paths for sphere/box/capsule pairs and sphere/box-vs-triangle, warm-started GJK with cached separating axes, and a min-heap EPA. What it leaks is VR4300 time in *plumbing*: per-step STL heap allocations, O(colliders) hash-map lookups with integer-division bucket math, narrowphase run on fattened-AABB false positives and on pairs that cannot move, a whole-list constraint scan per island body, ~400-byte `CollEvent` copies materialized for every contact even when nothing listens, ~900 bytes of stack zero-init per EPA call, and dozens of hoistable `sqrtf`/`div` on the hot capsule-sweep triangle path. The single heaviest per-contact path is capsule/cylinder/cone/pyramid colliders vs mesh, which still run full GJK+EPA per candidate triangle with no analytical shortcut. Two findings are high impact; the bulk are medium/low and are individually small, bit-identical code motion that adds up. Every item below is directly measurable with the stage tick counters already exported to the CPU debug overlay.

## Architecture & Frame Cost Model

**Frame → steps.** `Scene::update` ([scene.cpp:269](../n64/engine/src/scene/scene.cpp#L269)) runs a fixed-dt accumulator (default 50 Hz, `MAX_PHYSICS_STEPS = 5`) calling `CollisionScene::step()` ([collisionScene.cpp:1809](../n64/engine/src/collision/collisionScene.cpp#L1809)) once per tick. **Every per-step cost below can occur up to 5× per rendered frame.**

**Per step,** `step()` runs these stages, each already wrapped in a `get_ticks()` counter that is displayed in the overlay:

1. Mesh-collider world-state update + `wakeBodiesTransformedExternally`.
2. Per-collider world sync: `findRigidBodyByOwner` hash lookup + quat→matrix rebuild when moved + `AABBTree::moveNode`.
3. Velocity integration.
4. `detectAllContacts`: mark all cached constraints inactive; `detectSweptCollisions` (CCD substeps for fast bodies); then for **every** collider query the collider AABB tree (`queryBounds`, fattened leaf bounds = 10% extent margin + up to 10× displacement extension), dedupe pairs through a per-frame `unordered_set<int32_t>`, and call `collideDetectObjectToObject` / `collideDetectObjectToMesh`. Constraints not re-detected are removed by `removeInactiveContacts`.
5. `refreshContacts` (version-gated), `rebuildSolverConstraints`, `dispatchCollisionCallbacks` (builds `CollEvent` value copies per active constraint).
6. `preSolve` / `warmStart` / randomized Gauss-Seidel velocity solver (Fisher-Yates shuffle with `%` each iteration).
7. Position NGS solver.
8. `finalize`: `applyPositionConstraints` + `updateWorldInertia` for **every** body incl. sleeping; re-sync colliders; rebuild body AABBs.
9. `updateSleepStates`: island discovery scanning the full constraint list per stack-popped body.

**Per narrowphase pair.** `collideDetectObjectToObject` ([collide.cpp:1358](../n64/engine/src/collision/collide.cpp#L1358)) uses analytical closed forms for sphere/sphere, sphere/box, sphere/capsule, capsule/capsule; otherwise GJK (≤24 iterations, 2 support calls/iter) + EPA (≤8 iterations). Box-box has **no** analytical path (TODO at collide.cpp:1419) and always runs GJK+EPA.

**Per collider-vs-mesh triangle.** `collideDetectObjectToTriangle` gates analytical paths only on `ShapeType::Box` (SAT) and `ShapeType::Sphere` (closest-point); all other shapes fall through to GJK+EPA per candidate triangle, up to `MAX_CANDIDATES = 20`.

**Per gameplay frame,** `CharacterBody::moveAndSlide` ([characterBody.cpp:98](../n64/engine/src/collision/characterBody.cpp#L98)) issues up to `settings.maxSlides` capsule sweeps + up to 3 zero-displacement depenetration sweeps + 1 floor-probe raycast. `CollisionScene::capsuleSweep` transforms every candidate triangle to world space before calling `capsuleSweepTriangle`.

**Cost primitives on this hardware.** `fm_vec3_norm` (libdragon `fgeom.h:170`) = `sqrtf` + 3 divs. `div.s`/`sqrt.s` ≈ 29 cy, integer `DIVU` ≈ 37 cy, doubles ≈ 2× float. Vector helpers in `vecMath.h` are header-inline, but `matrix3Vec3Mul`/`matrix3Mul`/`quatToMatrix3` live out-of-line in `rigidBody.cpp` and `MeshTriangle::localVertex`/`gjkSupport` out-of-line in `meshCollider.cpp`; the build is `-Os` with **no `-flto`** (engine Makefile line 13; `n64.mk` adds `-ffast-math` but no LTO), so none of these inline across translation units. The 8 KB direct-mapped write-back D-cache (16 B lines) means the ~484 B `ContactConstraint` deque nodes and ~392 B `CollEvent` copies thrash easily.

**Data sizes.** Collider ~200 B; `ContactConstraint` ~484 B (3× ~120 B `ContactPoint`); `CollEvent` ~392 B; `PendingPairDispatch` ~800 B; `AABBTreeNode` 36 B (spans 3 D-cache lines).

**Instrumentation already present.** `src/debug/overlay/ovlCPU.cpp` pushes and prints, in the "Collision" table ([ovlCPU.cpp:199](../n64/engine/src/debug/overlay/ovlCPU.cpp#L199)): `Wake / World / Int.Vel / Det.Body / Det.Mesh / Refresh / PreSolve / WarmStart / Vel.Solve / Integrate / Pos.Solve / Finalize` from `collScene.ticksWakePrep … ticksFinalize`. Per-component timing (CollBody) appears in the component table ([ovlCPU.cpp:217](../n64/engine/src/debug/overlay/ovlCPU.cpp#L217) onward). These stage timers make every finding below directly measurable on real hardware; for finer resolution, add a temporary `get_ticks()` wrapper or narrowphase-call counter as noted per finding.

## Findings Overview

Sorted by impact (high first), then effort (small first). Refuted findings are dropped from this table (see Considered & Rejected). "Adjusted" means the claim, recommendation, or impact was corrected during verification — the corrected text is used below.

| ID | Title | Impact | Effort | Risk | Status |
|----|-------|--------|--------|------|--------|
| coll-broad-dispatch-event-copies | `dispatchCollisionCallbacks` materializes ~2.4 KB of `CollEvent` copies per active contact per step, even with no listeners | High | Medium | Low | Confirmed |
| coll-narrow-capsule-mesh-gjk-epa-per-triangle | Capsule/cylinder/cone/pyramid colliders vs mesh run full GJK+EPA per candidate triangle — no analytical fast path | High | Medium | Medium | Confirmed |
| coll-broad-no-tight-aabb-prefilter | Narrowphase runs on fattened-bounds false positives; no tight AABB overlap pre-check | Medium | Small | Low | Adjusted |
| coll-broad-sleeping-vs-static-narrowphase | Sleeping-vs-static and static-vs-static collider pairs run full narrowphase every step | Medium | Small | Medium | Adjusted |
| coll-narrow-sweep-zero-disp-mesh-waste | Zero-displacement depenetration sweeps transform the whole mesh candidate set, then every triangle test returns false | Medium | Small | Low | Adjusted |
| coll-narrow-sweep-per-triangle-world-transform | Capsule sweep re-evaluates mesh transform state per vertex of every candidate triangle | Medium | Small | Low | Adjusted |
| coll-narrow-cross-tu-call-overhead | Hot GJK/EPA support helpers are out-of-line in other TUs (no LTO), defeating inlining at `-Os` | Medium | Small | Low | Confirmed |
| coll-narrow-capsule-sweep-invariant-recompute | `capsuleSweepTriangle` recomputes sweep- and triangle-invariant sqrt/normalizations per sub-test | Medium | Small | Low | Confirmed |
| coll-narrow-epa-simplex-zero-init | `epaSolve` zero-initializes a ~900 B `ExpandingSimplex` per call; GJK `Simplex` zero-inits ~100 B per triangle | Medium | Small | Medium | Confirmed |
| coll-broad-per-step-heap-allocs | `detectAllContacts`/`detectSweptCollisions` heap-allocate temporaries every step | Medium | Small | Low | Confirmed |
| coll-broad-finalize-inertia-sleeping | Finalize runs `updateWorldInertia` for every body, including sleeping/disabled ones | Medium | Small | Low | Confirmed |
| coll-broad-rigidbody-owner-hash-lookups | `findRigidBodyByOwner`/`findCollidersForOwner` map lookups repeated per collider per stage | Medium | Medium | Low | Confirmed |
| coll-broad-island-scan-per-body | Sleep/wake island discovery scans the whole constraint list per island body (O(bodies × constraints)) | Medium | Medium | Low | Confirmed |
| coll-broad-deque-contact-storage | `cachedConstraints_` `std::deque` of ~484 B elements: one malloc/free per contact + double indirection per access | Medium | Medium | Medium | Confirmed |
| coll-narrow-localnormal-reciprocal-divs | `MeshCollider::localNormalToWorld` pays 3 divs + normalize even for identity-scale meshes | Low | Small | Low | Adjusted |
| coll-narrow-box-sat-normalizes-tri-normal | `analyticalBoxTriangle` normalizes the triangle normal although the SAT framework is designed for unnormalized axes | Low | Small | Low | Adjusted |
| coll-narrow-pair-double-hash-lookup | Colliding non-analytical pairs do two `unordered_map` constraint-cache lookups per frame | Low | Small | Low | Confirmed |
| coll-narrow-raycast-dead-normalize | `ray_capsule`/`ray_cylinder` discard the result of `vec3NormalizeOrFallback` — a wasted sqrt+3div per hit | Low | Small | Low | Confirmed |
| coll-narrow-cone-trig-normal | `ray_cone_intersection` uses `atanf` + `sinf` per hit to build the side normal | Low | Small | Low | Confirmed |
| coll-narrow-double-fmin-friction | Double-precision `fmin` used for combined friction | Low | Small | Low | Confirmed |
| coll-broad-solver-shuffle-modulo | Velocity-solver shuffle uses integer modulo per swap (~37 cy DIVU) | Low | Small | Medium | Adjusted |
| coll-broad-readmask-cross-tu-call | `Collider::readsCollider`/`readsMeshCollider` out-of-line, paying a call per broadphase candidate | Low | Small | Low | Confirmed |
| coll-broad-displacement-multiplier | `AABB_DISPLACEMENT_MULTIPLIER = 10` over-fattens moving leaves, inflating candidate counts | Low | Small | Low | Confirmed |
| coll-broad-collbody-scale-rewrite | `CollBody::update` recomputes and rewrites shape dimensions from object scale every frame | Low | Small | Low | Confirmed |
| coll-broad-aabbtree-node-layout | `AABBTreeNode` hot fields straddle 3 D-cache lines with cold fields interleaved | Low | Small | Low | Confirmed |
| coll-narrow-raycast-no-range-shrink | Scene raycast never shrinks range after a closer hit; later candidates tested against original maxDistance | Low | Medium | Medium | Confirmed |

## Detailed Findings

### coll-broad-dispatch-event-copies — CollEvent copy traffic per contact, even with no listeners

**Location:** [collisionScene.cpp:626](../n64/engine/src/collision/collisionScene.cpp#L626)
**Impact:** High · **Effort:** Medium · **Risk:** Low · **Status:** Confirmed (two independent reviewers)

**Claim.** For every active constraint the dispatch loop zero-initializes and copies multiple ~392 B `CollEvent` structs (`CollEvent` holds `std::array<ContactPoint,3>` = 360 B) plus pushes ~800 B `PendingPairDispatch` elements into a per-step vector and `unordered_map`. This is pure memset/memcpy traffic on the 8 KB write-back D-cache — evicting hot solver data and stealing RDRAM bandwidth shared with the RDP — and it runs even when no component on either object implements `onColl`. The only per-iteration gate is `collisionEventMasksOverlap` (collision-layer masks); the `onColl` listener check happens much later inside `dispatchObjectCollisionEvent` (scene.cpp:72), *after* the full event is already built.

**Evidence.**

```cpp
// collisionScene.cpp:626
for(int i = 0; i < cachedConstraintCount_; ++i) {
  const ContactConstraint &constraint = cachedConstraints_[i];
  if(!constraint.isActive || constraint.pointCount <= 0) continue;
  if(!constraint.objectA || !constraint.objectB) continue;
  if(!constraint.objectA->isEnabled() || !constraint.objectB->isEnabled()) continue;
  // ... dispatchLookup.find / emplace (one malloc per unique pair) ...
  PendingPairDispatch &dispatch = pendingDispatches[lookupIt->second];
  const CollEvent event = makeCollisionEvent(constraint);            // CollEvent event{}  -> zero ~392B
  captureDirectionalEvent(dispatch, event);                          // full-struct copy
  captureDirectionalEvent(dispatch, makeMirroredCollisionEvent(event)); // 2nd full build + copy
}
```

`makeCollisionEvent` (line 557) does `CollEvent event{}` (zeroing all ~392 B); `makeMirroredCollisionEvent` (line 80) builds a second full copy; `captureDirectionalEvent` (lines 613-622) copies the full struct into `dispatch.firstEvent`/`secondEvent`. `pendingDispatches` is a per-step vector reserved to `cachedConstraintCount_`, plus `dispatchLookup.emplace` mallocs one `unordered_map` node per unique object-pair. `dispatchCollisionCallbacks` runs unconditionally once per physics step ([collisionScene.cpp:1866](../n64/engine/src/collision/collisionScene.cpp#L1866)), up to 5×/frame. So a mask-overlapping pair pays ~2 × 392 B zero+copy + an ~800 B `PendingPairDispatch` write-allocate + one libdragon malloc per unique pair (heap lock + walk), all fully wasted in a pure-physics scene (crate piles with no `onColl`).

**Recommendation.** Gate the loop on a cached per-`Object` "has `onColl` component" flag computed once at object init (`COMP_TABLE[type].onColl` is static per component type; components are fixed per object at spawn) so pairs with no listener skip event construction entirely. For pairs that do listen, check `collisionEventMasksOverlap` and which object is self *before* constructing, build only the direction(s) actually needed, construct in place in the dispatch slot instead of via temporaries, and drop the unconditional mirrored copy. Keep `pendingDispatches`/`dispatchLookup` as persistent members cleared per step. Touches no numerical/solver state and no `particleSim.h`.

**How to measure.** `ticksRefreshCallbacks` (overlay "Refresh" row) in a scene with 10+ colliding bodies and zero `onColl` components; expect it to drop to near the `refreshContacts` baseline. Compare at 5 physics substeps.

**Risk & functionality preservation.** Low. `-Os` cannot elide this (listener presence is runtime `COMP_TABLE` data; construction feeds virtual dispatch through `SceneManager`). Behavior/determinism preserved exactly: skipping construction for listener-less pairs is a no-op relative to today. Magnitude is contact-count- and listener-density-dependent — in scenes that *do* have listeners the win shrinks to eliminating only the unneeded-direction construction; the stack-local `event` reuses the same address each iteration so it mostly stays cache-hot after the first, and the dominant unavoidable-at-`-Os` costs are the two ~392 B store bursts and the per-pair malloc.

### coll-narrow-capsule-mesh-gjk-epa-per-triangle — no analytical capsule-vs-triangle path

**Location:** [collide.cpp:1221](../n64/engine/src/collision/collide.cpp#L1221)
**Impact:** High · **Effort:** Medium · **Risk:** Medium · **Status:** Confirmed (two independent reviewers)

**Claim.** `collideDetectObjectToTriangle` gates analytical fast paths only on `ShapeType::Box` (SAT) and `ShapeType::Sphere` (closest-point); capsule/cylinder/cone/pyramid fall through to `gjkCheckForOverlap` (≤24 iterations, 2 support calls/iter) + `epaSolve` (≤8 iterations) per candidate triangle, for up to `MAX_CANDIDATES = 20` triangles per collider-vs-mesh pair. Each support eval pays a capsule `fm_vec3_norm` (sqrt + 3 div) plus cross-TU `matrix3Vec3Mul` (compounds coll-narrow-cross-tu-call-overhead). The triangle GJK is **not** warm-started (`firstDir` is recomputed geometrically at collide.cpp:1223), so the cached-separating-axis amortization used on the object-object path does not apply — each triangle pays a cold solve. At ~15-25 µs/triangle over the genuinely-overlapping candidates, a single resting capsule/cylinder/cone/pyramid rigid body on a mesh consumes ~0.15-0.4 ms (upper bound >0.5 ms with dense geometry), scaling linearly with body count.

**Evidence.**

```cpp
// collide.cpp:1174 / 1199 gate the only analytical paths:
if(colliderProxyMeshSpace->collider->shapeType() == ShapeType::Box && !isTriggerContact) { ... }
if(colliderProxyMeshSpace->collider->shapeType() == ShapeType::Sphere && !isTriggerContact) { ... }

// collide.cpp:1221 — everything else falls through, per triangle:
Simplex simplex;
fm_vec3_t firstDir = ((tri.localVertex(0)+tri.localVertex(1)+tri.localVertex(2))/3.0f)
                     - colliderProxyMeshSpace->worldCenter;   // cold seed, no warm start
if(fm_vec3_len2(&firstDir) < FM_EPSILON*FM_EPSILON) firstDir = VEC3_RIGHT;
bool gjkOverlap = gjkCheckForOverlap(simplex, colliderProxyMeshSpace, colliderProxyGjkSupport,
                                     &tri, meshTriangleGjkSupport, firstDir);
if(!gjkOverlap) return false;
// ...
bool epaSuccess = epaSolve(simplex, colliderProxyMeshSpace, colliderProxyGjkSupport,
                           &tri, meshTriangleGjkSupport, epaResult);
```

`CapsuleShape::support` (shapes.h:62-69) calls `fm_vec3_norm` every invocation. `collideDetectObjectToMesh` runs per dynamic collider × candidate mesh per physics step (`ticksDetectMeshPairs`, collisionScene.cpp:1063) plus CCD substeps.

**Recommendation.** Add an analytical capsule-vs-triangle path mirroring `analyticalSphereTriangle`: closest point between the capsule segment and the triangle (segment machinery exists in `analyticalCapsuleCapsule`, collide.cpp:290; `closestPointOnTriangle` at collide.cpp:76), then generate the contact with the same support-based convention `analyticalSphereTriangle` uses (collide.cpp:729-748) so EPA output conventions are matched. Keep GJK+EPA for cylinder/cone/pyramid. Does not touch `particleSim.h`.

**How to measure.** `ticksDetectMeshPairs` (overlay "Det.Mesh" row) in a scene with capsule rigid bodies resting on a collision mesh; add a temporary counter of `epaSolve` invocations per frame.

**Risk & functionality preservation.** Medium — this is an algorithmic addition (`-Os` cannot synthesize it), and correctness rests on matching the already-accepted sphere fast path's contact convention so contact output stays physically equivalent and deterministic. Two non-defect caveats: (1) the >0.5 ms figure is an upper bound (20 × 30-60 µs) — a resting capsule has only a few genuinely-overlapping triangles running full EPA while the rest terminate GJK earlier, so typical cost is lower but still material on the ~93.75 MHz budget; (2) the most common capsule users, `CharacterBody`, use the separate `capsuleSweep` path, **not** this GJK+EPA path, so the win rests on non-character dynamic capsule/cylinder/cone/pyramid rigid bodies resting on/colliding with static mesh geometry.

### coll-broad-no-tight-aabb-prefilter — narrowphase on fattened-bounds false positives

**Location:** [collisionScene.cpp:1032](../n64/engine/src/collision/collisionScene.cpp#L1032)
**Impact:** Medium *(adjusted from High)* · **Effort:** Small · **Risk:** Low · **Status:** Adjusted

**Claim (corrected).** Broadphase candidates come from fattened tree-leaf bounds (10% extent margin at aabbTree.cpp:108/128, plus up to 10× displacement extension at :133), but the body-pair loop calls `collideDetectObjectToObject` (collisionScene.cpp:1032) without a tight-world-AABB overlap test, and `collide.cpp:1358-1475` has none either. Candidate pairs whose tight AABBs do **not** overlap (they lie within the fattening shell) pay narrowphase work every step. Cost per false positive is shape-dependent: box-box pairs (the only common combo with no analytical path) run a `findCachedConstraint` hash lookup + full GJK (~hundreds to >1000 cy); sphere/box/capsule analytical pairs cost only tens of cycles. Fast movers inflate this via the 10× displacement fattening. The false-positive population for stationary objects is limited to the ~10% fattening shell, **not** all adjacent objects (well-separated objects are tree-culled and never reach narrowphase).

**Evidence.** After the mask check, `collisionScene.cpp:1023-1032` calls `collideDetectObjectToObject(collider, rbA, collB, rbB, true)` with no `aabbOverlap(collider->worldAabb_, collB->worldAabb_)` first. `worldAabb_` is the tight AABB (colliderShape.cpp:112). Fattening at aabbTree.cpp:108 (`*0.1f`) and :133 (`disp = displacement * AABB_DISPLACEMENT_MULTIPLIER`, 10×). The CCD loops at 910-924 / 936-947 also lack the check.

**Recommendation (corrected).** Insert `if(!aabbOverlap(collider->worldAabb_, collB->worldAabb_)) continue;` (inline, 6 float compares from aabb.h:26) before `collideDetectObjectToObject` in the body-pair loop (collisionScene.cpp:1023-1032) and in the CCD body loop (910-924). **Skip or deprioritize the mesh-loop variant** the original finding proposed: `MeshCollider::worldAabb_` is the whole-mesh bound (meshCollider.cpp:250), so the tight test is almost always true and `collideDetectObjectToMesh` already does per-triangle AABB culling internally — negligible benefit.

**How to measure.** `ticksDetectBodyPairs` in a scene with many stationary adjacent colliders (a row of crates one gap apart) or fast movers passing a cluster; add a temporary counter of narrowphase calls per step — expect it to drop to the true-overlap count. Note a row of crates whose gap exceeds ~10% of extent shows *zero* win (those are tree-culled).

**Risk & functionality preservation.** Low, and strictly bit-identical: a contact requires true shape overlap, which implies tight-AABB overlap, so no real contact is ever skipped. The `cachedSeparatingAxis` concern from the original finding is moot — a separated pair records no constraint and `removeInactiveContacts` (collisionScene.cpp:817-826) deletes any existing one that same step, so no future GJK seed is affected. Impact corrected to medium: worst-case dense box-box clusters can approach ~1 ms/frame, but typical scenes are far less, and analytical pairs save only tens of cycles.

### coll-broad-sleeping-vs-static-narrowphase — resting-on-static pairs never stop narrowphase

**Location:** [collisionScene.cpp:1026](../n64/engine/src/collision/collisionScene.cpp#L1026)
**Impact:** Medium *(adjusted from High)* · **Effort:** Small · **Risk:** Medium · **Status:** Adjusted

**Claim (corrected).** The both-sleeping skip (collisionScene.cpp:1026, mirrored at collide.cpp:1367) requires **both** endpoints to have a RigidBody, so a sleeping body resting on a static primitive Collider (`rbB == nullptr`), and static-vs-static non-trigger pairs, re-run narrowphase every step. The cost is real — box-box has no analytical path (collide.cpp:1419 TODO) so full GJK+EPA runs, up to 5 steps/frame. **However** the dominant "body resting on level geometry" case is already optimized because level geometry is typically a `MeshCollider` and the mesh loop already skips sleeping bodies (collisionScene.cpp:1047). The residual cost only bites when static resting surfaces are primitive Colliders without a rigidbody (e.g. static `CollBody` platforms) — a narrower scenario than the original "high" implied.

**Evidence.** `collisionScene.cpp:1026`: `if((rbA && rbA->isSleeping_) && (rbB && rbB->isSleeping_)) { if(!collider->isTrigger_ && !collB->isTrigger_) { continue; } }` — `rbB == nullptr` makes the condition false. Contrast the mesh loop at collisionScene.cpp:1047: `if(!collider->isTrigger_ && rigidBodyA && rigidBodyA->isSleeping_) continue;`.

**Recommendation (corrected).** Do **not** simply skip/drop sleeping-vs-static pairs as originally proposed — it breaks observable behavior three ways:

1. The persisted sleeping-vs-static `ContactConstraint` is the mechanism that wakes a sleeping body when its support is removed: `removeCollider` (collisionScene.cpp:352) and `removeMeshCollider` harvest wake candidates from existing constraints via `removeCachedConstraints` (lines 532-533), and `CollBody::onDisable` routes through `removeCollider` (collBody.cpp:96). Skip narrowphase and that constraint is deactivated (line 974) and deleted, so destroying/disabling a static platform under a sleeping body no longer wakes it — the body floats. The finding's cited "safe" `disableRigidBody` AABB-overlap wake (line 309) does **not** cover rigidbody-less colliders.
2. "Missing RigidBody" is **not** "immobile": colliders without rigidbodies are moved kinematically every step via `syncWorldState` (line 1831).
3. Static-vs-static non-trigger pairs currently fire `onColl` every step; a blanket skip silently stops that.

To keep the win safely: skip only the expensive GJK/EPA while **re-activating** the existing cached constraint if one is present for sleeping-vs-immobile non-trigger pairs (sleeping bodies are never solved, so stale contact points are harmless and the constraint stays available for removal-wake and `onColl`). Alternatively gate the skip on an explicit static/immobile flag that excludes kinematic movers *and* add an AABB-overlap wake to `removeCollider` mirroring `disableRigidBody`.

**How to measure.** `ticksDetectBodyPairs` with N dynamic boxes asleep on top of static box colliders (not meshes): currently stays high after sleep; after a correct change it falls to near zero. Confirm bodies still wake on impact and still fall when their static support is destroyed.

**Risk & functionality preservation.** Medium — the naive version regresses wake correctness and static-static callbacks; the safe re-activation variant preserves behavior. `-Os` cannot perform this algorithmic skip.

### coll-narrow-sweep-zero-disp-mesh-waste — zero-displacement depenetration sweep wastes the whole mesh pass

**Location:** [capsuleSweep.cpp:279](../n64/engine/src/collision/capsuleSweep.cpp#L279)
**Impact:** Medium *(adjusted from High)* · **Effort:** Small · **Risk:** Low · **Status:** Adjusted

**Claim (corrected).** `CharacterBody` fires a zero-displacement `capsuleSweep` every frame (depenetration probe, characterBody.cpp:300-313, at least once per `moveAndSlide`, up to 3×). `capsuleSweepTriangle` early-returns false for any displacement below `SWEEP_EPS` (capsuleSweep.cpp:279-280, `SWEEP_EPS = 1e-6f`), so with `displacement == VEC3_ZERO` every mesh-triangle test is a guaranteed false and the mesh block never writes `hit`. Yet `CollisionScene::capsuleSweep` still pays the mesh broadphase query, per-mesh `worldAabbToLocal`, and per-triangle work (collisionScene.cpp:1689-1695) before each doomed call. Cost qualifier: the ~300-500 cy/triangle transform applies only when `mesh->hasTransform()` is true; static origin-baked meshes take the cheap `wv = mesh->vertex()` branch, leaving the tree query plus one `len2`+`sqrtf` per doomed triangle. Net waste is on the order of tens of microseconds per grounded character per frame (persistent, since a grounded character's AABB always overlaps its floor mesh), scaling with character count and candidate-triangle count.

**Evidence.** `capsuleSweep.cpp:279-281`: `float dist = sqrtf(fm_vec3_len2(&displacement)); if(dist < SWEEP_EPS) return false;`. `characterBody.cpp:300`: `constexpr fm_vec3_t depProbe = VEC3_ZERO;` feeding `scene.capsuleSweep` at :308. `collisionScene.cpp:1689-1692` transforms `wv0/wv1/wv2/wn` per triangle before the call at :1695. The doBodies GJK/EPA block (:1708) is what actually detects the overlap the probe exists for. The main swept loop already guards `dispLen2` at characterBody.cpp:168, so only the depenetration probe passes zero.

**Recommendation (corrected).** In `CollisionScene::capsuleSweep`, guard the `doMesh` block on `fm_vec3_len2(displacement) < SWEEP_EPS*SWEEP_EPS` and skip it — provably bit-identical because every `capsuleSweepTriangle` returns false at that threshold. The collider-bodies GJK/EPA section **must** still run (it is the only path that detects the zero-displacement overlap). Implementation caveat: the guard threshold must be exactly `SWEEP_EPS^2` (or smaller); a coarser threshold would skip displacements the per-triangle path would not reject and change behavior. `SWEEP_EPS` is file-static in `capsuleSweep.cpp`, so share/duplicate the exact constant rather than substituting `FM_EPSILON`. Does not touch `particleSim.h`.

**How to measure.** `ovlCPU` per-component timing for the character's `CollBody`, or wrap the `scene.capsuleSweep` call at characterBody.cpp:308 with `get_ticks()`; compare standing-next-to-wall scenes before/after.

**Risk & functionality preservation.** Low — mechanism confirmed by two reviewers as bit-identical (zero displacement makes the mesh block a no-op today). Cannot be done by the compiler: no `-flto`, and `capsuleSweepTriangle` is in a separate TU from the caller. Best combined with coll-narrow-sweep-per-triangle-world-transform.

### coll-narrow-sweep-per-triangle-world-transform — per-triangle re-derivation of loop-invariant mesh transform state

**Location:** [collisionScene.cpp:1689](../n64/engine/src/collision/collisionScene.cpp#L1689)
**Impact:** Medium *(adjusted from High)* · **Effort:** Small · **Risk:** Low · **Status:** Adjusted

**Claim (corrected).** For each candidate triangle in the capsule sweep's mesh loop, the code re-evaluates `mesh->hasTransform()` 4× and calls `mesh->toWorldSpace()` 3× + `mesh->localNormalToWorld()` 1×. All three are out-of-line cross-TU const methods (declared in meshCollider.h, defined in meshCollider.cpp); the build is `-Os` with no `-flto`, so the compiler cannot hoist or CSE them across the triangle loop. The transform state (owner pos/rot/scale, identity checks) is loop-invariant per mesh (snapshotted once per step via `syncOwnerTransform`), so 3 of the 4 `hasTransform` calls and the redundant pos/rot/scale reloads + identity checks inside the `toWorldSpace` calls are pure recomputation. **The world-space transform arithmetic itself (rotate/scale 3 verts + normalize 1 normal) is irreducible and NOT what this fix removes**; the realistic saving is the redundant state re-evaluation and cross-TU call overhead, roughly ~100-200 cycles per triangle. `capsuleSweep` runs up to `maxSlides` + 3 times per character per frame. In typical single-character scenes with broadphase-narrowed candidate counts (well below the 64 cap) the win is tens of microseconds per frame; "hundreds of microseconds" is an upper bound reached only with dense geometry and/or many characters.

**Evidence.** `collisionScene.cpp:1689-1692`: `fm_vec3_t wv0 = mesh->hasTransform() ? mesh->toWorldSpace(mesh->vertex(tri.indices[0])) : mesh->vertex(...)` repeated for `wv1`, `wv2`, and `wn = mesh->hasTransform() ? mesh->localNormalToWorld(...)`. `meshCollider.cpp:107`: `fm_vec3_t position = owner_ ? owner_->pos * getInvGfxScale() : VEC3_ZERO;` executed per call; :111 `if(!quatIsIdentical(&rotation, &QUAT_IDENTITY))` per call.

**Recommendation (corrected).** Hoist per mesh before the triangle loop: `const bool xform = mesh->hasTransform();` and snapshot owner position (`owner->pos * getInvGfxScale()`), rotation, and scale once, then transform the 3 vertices + normal inline using the cached snapshot (also eliminating the 3 cross-TU `toWorldSpace` calls per triangle). Identical arithmetic in identical order → bit-identical. Best combined with coll-narrow-sweep-zero-disp-mesh-waste (which skips the mesh block entirely for the 3 depenetration sweeps). Does not touch `particleSim.h`.

**How to measure.** `get_ticks()` around the `doMesh` block of `capsuleSweep`; count triangles processed; compare µs/triangle before/after on a character sliding along a collision mesh.

**Risk & functionality preservation.** Low — same arithmetic, same order, evaluated once per mesh instead of per triangle. Alternative rec (transform the capsule into mesh-local space once, valid when `!hasScale()`) is a larger refactor, not "small".

### coll-narrow-cross-tu-call-overhead — hot GJK/EPA support helpers not inlinable across TUs

**Location:** [rigidBody.cpp:60](../n64/engine/src/collision/rigidBody.cpp#L60)
**Impact:** Medium · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** `matrix3Vec3Mul` (9 mul + 6 add) is defined in rigidBody.cpp, `MeshTriangle::localVertex`/`gjkSupport` in meshCollider.cpp:35-80, and `Collider::support` in colliderShape.cpp:13 — all called from collide.cpp/gjk.cpp/epa.cpp hot loops. With `-Os` and no `-flto`, every GJK/EPA support evaluation pays: indirect function-pointer call → `colliderProxyGjkSupport` → 2 cross-TU `matrix3Vec3Mul` calls → cross-TU `Collider::support` switch; the mesh side pays `meshTriangleGjkSupport` → cross-TU `gjkSupport` → 3 cross-TU `localVertex` calls (each re-branching on `vertices == nullptr` because collide.cpp:1168 sets `tri.vertices = nullptr`). On the single-issue VR4300 with no branch prediction, ~50-100 cy of call/branch overhead plus lost CSE per support call, × ~20-40 support calls per solve, × every non-analytical pair and triangle.

**Evidence.** `collide.cpp:63-65`: `matrix3Vec3Mul(proxy->shapeToSpaceTranspose, direction); ... proxy->collider->support(localDir); ... matrix3Vec3Mul(proxy->shapeToSpace, localSupport)`. `matrix3x3.h:17` declares `matrix3Vec3Mul` without definition; definition at rigidBody.cpp:60. `meshCollider.cpp:35-39`: `localVertex` branches on `if(vertices)` each call; collide.cpp:1168 sets `tri.vertices = nullptr`.

**Recommendation.** Move `matrix3Vec3Mul` (and `matrix3Transpose`) into `matrix3x3.h` as inline; move `MeshTriangle::localVertex` and `gjkSupport` into `meshCollider.h` as inline; in `collideDetectObjectToTriangle` set `tri.vertices` to the mesh vertex array (add an accessor) so `localVertex` takes the direct-array branch. All pure code motion, bit-identical. **Caveat:** inlining the *full* `Collider::support` switch into every call site risks 16 KB I-cache pressure — the clear wins are the small helpers (`matrix3Vec3Mul`, `localVertex`); leave `Collider::support` alone or measure carefully.

**How to measure.** libdragon function-level profiler on `gjkCheckForOverlap`/`epaSolve`; or `ticksDetectBodyPairs`/`ticksDetectMeshPairs` before/after in a scene with capsule/cylinder colliders overlapping meshes.

**Risk & functionality preservation.** Low — pure code motion; results identical.

### coll-narrow-capsule-sweep-invariant-recompute — invariant sqrt/normalizations recomputed per sub-test

**Location:** [capsuleSweep.cpp:279](../n64/engine/src/collision/capsuleSweep.cpp#L279)
**Impact:** Medium · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** Per triangle call: (a) displacement length + direction (`sqrtf` + 3 divs) recomputed although constant for the entire sweep (same displacement passed for every triangle, collisionScene.cpp:1695); (b) `cylinderVertexTest` (capsuleSweep.cpp:220-224) recomputes `d1_len = sqrtf(len2(P2-P1))` and `d1hat = d1/d1_len` for each of 3 vertices, although `d1hat` is exactly the already-normalized `axisUp` parameter (unit per API) and `d1_len = 2*innerHalfHeight`; (c) `sphereEdgeTest` (capsuleSweep.cpp:83-93) normalizes each triangle edge (`sqrtf` + div) and is called for the same 3 edges from both capsule endpoints, so 3 of 6 edge normalizations duplicate. Total avoidable: ~7 `sqrtf` + ~15 divs per triangle on the hottest gameplay path.

**Evidence.** capsuleSweep.cpp:279-281 `float dist = sqrtf(...); fm_vec3_t dir = displacement / dist;` per call. :220-224 `float d1_len = sqrtf(d1_len2); fm_vec3_t d1hat = d1 / d1_len;` inside `cylinderVertexTest`, invoked 3× per triangle at :325-328. :86-88 `float elen = sqrtf(elen2); fm_vec3_t u_hat = edge / elen;` for the same 3 edges from both endpoints at :305-312. `-fno-associative-math` + the per-triangle function-call boundary prevent the compiler from CSE-ing these.

**Recommendation.** Compute `dir`/`dist` once in `CollisionScene::capsuleSweep` and pass down (or cache in a small struct); pass `axisUp` and `2*innerHalfHeight` into the cylinder tests instead of re-deriving; precompute per-edge `{edge, elen2, elen, u_hat}` once per triangle and share across the two sphere endpoints and the cylinder edge test. Differences are at most 1-ulp reassociation, and the `axisUp` substitution is exact when `axisUp` is unit (the API already requires it). File is engine-only, so no `particleSim.h` parity constraint.

**How to measure.** `get_ticks()` around the triangle loop in `capsuleSweep` with a character sliding along a mesh wall; µs/triangle before/after.

**Risk & functionality preservation.** Low — the header API can stay; only the internal structure of `capsuleSweepTriangle` changes.

### coll-narrow-epa-simplex-zero-init — ~900 B stack zero-init per EPA call

**Location:** [epa.cpp:366](../n64/engine/src/collision/epa.cpp#L366)
**Impact:** Medium · **Effort:** Small · **Risk:** Medium · **Status:** Confirmed

**Claim.** `ExpandingSimplex` (epa.cpp:50-59) with 8 iterations is `points[12]` (12 B each) + `aPoints[12]` + `triangles[20]` (28 B each) + two 20-byte heap arrays ≈ 900 B, all with `{}` NSDMI, and `ExpandingSimplex es{};` (epa.cpp:366) value-initializes the whole struct — GCC emits a full ~900 B zeroing per `epaSolve` call; on the direct-mapped D-cache, write-allocate misses can add materially. Every field actually read is overwritten first by `initExpandingSimplex`/`addTriangle`/`expandPolytope`. Similarly `Simplex simplex;` (collide.cpp:1222, gjk.h:22-24 NSDMI `{}`) zeroes ~100 B per candidate triangle even though `gjkCheckForOverlap` resets `nPoints` and writes points before reading.

**Evidence.** epa.cpp:366 `ExpandingSimplex es{};` followed by `initExpandingSimplex` which sets counts/flags and re-adds all points/triangles. epa.cpp:51-57: every member has a `{}` initializer. gjk.h:22-24: `fm_vec3_t points[GJK_MAX_SIMPLEX_SIZE]{}; ... short nPoints{0};`.

**Recommendation.** Remove the NSDMI `{}` initializers from `ExpandingSimplex`'s arrays (keep counters) and declare `ExpandingSimplex es;`. Same for `Simplex`'s point arrays. `initExpandingSimplex` already establishes all invariants; `simplexAddPoint` writes before any read (including the slot-3 scratch write in `simplexCheck`).

**How to measure.** libdragon profiler on `epaSolve`, or a `get_ticks()` microbenchmark calling `epaSolve` in a loop; also visible in `ticksDetectBodyPairs` for stacked rigid bodies.

**Risk & functionality preservation.** Medium — removing the init requires verifying no read-before-write across all code paths (heap slots, triangle adjacency). The finding states this was verified for current paths, but the change is fragile against future edits; document the invariant at the declaration.

### coll-broad-per-step-heap-allocs — STL temporaries allocated every physics step

**Location:** [collisionScene.cpp:982](../n64/engine/src/collision/collisionScene.cpp#L982)
**Impact:** Medium · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** Each step allocates: `tested_pairs` (`unordered_set` — one node malloc per inserted pair, inserted *before* the cheap self/same-owner rejects so every collider mallocs its own self-pair node), `candidateColliders.resize` (malloc + zero), `candidateMeshColliders.resize`, and in `detectSweptCollisions` two more vectors sized to collider counts even when no body needs CCD. malloc/free through libdragon plus zeroing traffic runs up to 5×/frame and fragments the heap. The dominant allocation is actually `tested_pairs` node-per-unique-pair (dozens/step), so the "5-8 allocs/step" estimate is conservative.

**Evidence.** collisionScene.cpp:982 `std::unordered_set<int32_t> tested_pairs;`; 985-989 `candidateColliders.resize(colliders_.size());`; 1039 `candidateMeshColliders.resize(...)`; 843/846 (CCD) unconditional `candidates`/`meshCandidates` resize before the body loop. Insert-before-reject confirmed: key computed and inserted at 1013-1014 before the self/same-owner rejects at 1018-1021 and the mask reject at 1023.

**Recommendation.** Hoist all four buffers to `CollisionScene` members reused across steps. For `tested_pairs`, either a persistent set cleared per step or (option c, preserving first-encounter semantics) move the self/same-owner/mask rejects **before** the insert so self-pairs and rejected pairs never allocate a node. In `detectSweptCollisions`, defer buffer setup until the first body actually exceeds the CCD threshold.

**How to measure.** `ticksDetectBodyPairs` + `ticksDetect` deltas; instrument malloc call count per step — should go from dozens to 0 in steady state.

**Risk & functionality preservation.** Low with option (c), which keeps first-encounter iteration semantics exactly. (The alternative `aabbTreeNodeId_` ordering trick reorders which iteration tests a pair and changes constraint-creation order — avoid it.)

### coll-broad-finalize-inertia-sleeping — updateWorldInertia recomputed for sleeping/disabled bodies

**Location:** [collisionScene.cpp:1915](../n64/engine/src/collision/collisionScene.cpp#L1915)
**Impact:** Medium · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** The end-of-step finalize loop calls `body->updateWorldInertia()` unconditionally. `rigidBody.cpp:452-476` shows this does 2× `quatToMatrix3`, 2× `matrix3Mul` + 1 transpose, `matrix3Vec3Mul`, 3× `matrix3Equals`, plus `refreshAngularConstraintProjection`/`refreshConstrainedInertiaTensor` — several hundred FPU cycles per body — with no early-out for unchanged rotation. Sleeping/disabled bodies are excluded from integration (1849-1853) and position integration (1891-1897), so this recomputes bit-identical matrices, `transformChanged` is false, no version bump. The earlier world-update loop at 1842 already applies exactly this skip (`if(!body->isEnabled_ || body->isSleeping_) continue;`).

**Evidence.** collisionScene.cpp:1911-1916: `for(RigidBody *body : rigidBodies_) { if(!body || !body->owner_) continue; body->applyPositionConstraints(); body->updateWorldInertia(); ... }` — no `isEnabled_`/`isSleeping_` guard, unlike line 1842.

**Recommendation.** Skip `updateWorldInertia` (and `applyPositionConstraints`, which clamps against `previousStepPosition_` that equals `position_` for sleeping bodies) when `!body->isEnabled_ || body->isSleeping_`. External transform writes are already handled at the start of the next step by `wakeBodiesTransformedExternally`.

**How to measure.** `ticksFinalize` in a scene with ~20 sleeping bodies: expect a drop of roughly (500-800 cy × sleeping bodies) per step, i.e. tens of microseconds.

**Risk & functionality preservation.** Low — recomputed matrices are bit-identical for an unchanged rotation, so `transformChanged` is false and no version bump occurs today either; behavior preserved exactly.

### coll-broad-rigidbody-owner-hash-lookups — repeated owner→body/collider map lookups

**Location:** [collisionScene.cpp:1829](../n64/engine/src/collision/collisionScene.cpp#L1829)
**Impact:** Medium · **Effort:** Medium · **Risk:** Low · **Status:** Confirmed

**Claim.** Every step performs ~3 hash-map lookups per collider plus one per candidate pair plus one per body in finalize. Each `unordered_map::find` does a modulo by a prime bucket count (MIPS `DIVU` ~37 stall cycles) followed by two dependent RDRAM loads (bucket then node, likely D-cache misses). With 30 colliders that is 100+ lookups/step, several thousand cycles of pure lookup overhead, up to 5×/frame — for a mapping static between add/remove.

**Evidence.** Call sites: collider refresh `findRigidBodyByOwner(collider->owner_)` (1829); detect body loop (998) + per-candidate (1025); mesh loop (1045); CCD (856, 918); finalize `findCollidersForOwner(body->owner_)` (1918); `updateCompoundProperties` (202). Implementation: `findRigidBodyByOwner` (184-188) and `findCollidersForOwner` (190-195) both `unordered_map::find`.

**Recommendation.** Cache the owning RigidBody pointer directly on `Collider` (`RigidBody *ownerBody_`), maintained in `addCollider`/`removeCollider`/`addRigidBody`/`removeRigidBody` (`addRigidBody` must back-fill colliders already registered for that owner), and cache the collider list pointer (or an intrusive list) on `RigidBody` for the finalize/CCD/compound paths. All per-step hash lookups become single pointer loads.

**How to measure.** `ticksWorldUpdate` + `ticksDetectBodyPairs` + `ticksFinalize` before/after in a scene with 30+ colliders; temporary counter of `ownerRigidBodies_.find` calls should reach 0.

**Risk & functionality preservation.** Low — behavior-preserving pointer caching; the only correctness requirement is back-filling on `addRigidBody` (medium effort).

### coll-broad-island-scan-per-body — O(bodies × constraints) island discovery

**Location:** [collisionScene.cpp:718](../n64/engine/src/collision/collisionScene.cpp#L718)
**Impact:** Medium · **Effort:** Medium · **Risk:** Low · **Status:** Confirmed

**Claim.** `updateSleepStates` and `collectConnectedIsland` find a body's neighbors by iterating **all** cached constraints for **every** body popped off the island stack — cost (awake bodies) × (total constraints) per step, each constraint access touching a ~484 B deque element (guaranteed D-cache misses). Plus an `unordered_set<RigidBody*> visited` node-malloc per awake body every step, plus island/stack vectors.

**Evidence.** updateSleepStates, collisionScene.cpp:718-732: inside `while(!stack.empty())`, `for(int i = 0; i < cachedConstraintCount_; ++i) { const ContactConstraint &cc = cachedConstraints_[i]; ... }`. Same pattern in `collectConnectedIsland` (493-508). Per-step containers: `std::unordered_set<RigidBody*> visited;` (696) plus `std::vector` island/stack per seed (704-707). Common in prop piles (15+ awake stacked bodies).

**Recommendation.** After `rebuildSolverConstraints` (which already walks constraints once/step), build a per-body adjacency edge list into reused scratch arrays (counting-sort style: count edges per body, prefix sum, fill), then island traversal touches only each body's own edges. Replace `visited` with a `uint16` frame-stamp on `RigidBody` compared against a per-step counter — zero allocation, O(1). Traversal order preservable.

**How to measure.** `ticksFinalize` (updateSleepStates runs in the finalize window, 1910-1956) with 15+ awake stacked bodies; or time `updateSleepStates` alone with `get_ticks`. Expect quadratic→linear scaling when doubling body count.

**Risk & functionality preservation.** Low — traversal order can be kept identical to preserve behavior exactly.

### coll-broad-deque-contact-storage — one malloc/free per contact + double indirection per access

**Location:** [collisionScene.h:134](../n64/engine/include/collision/collisionScene.h#L134)
**Impact:** Medium · **Effort:** Medium · **Risk:** Medium · **Status:** Confirmed

**Claim.** `sizeof(ContactConstraint)` ≈ 484 B (key ~20 + 8 pointers 32 + 4 vec3 48 + misc 24 + 3× ~120 B `ContactPoint` = 360). libstdc++'s `__deque_buf_size` is `(size < 512 ? 512/size : 1)`; `512/484 = 1`, so exactly **one** element per deque buffer node — a malloc per `createCachedConstraint`, a free per pop, and every indexed `cachedConstraints_[i]` (six per-step loops: deactivate-all 974, refreshContacts 776, rebuildSolverConstraints 832, dispatch 627, island scans 494/719, removeInactiveContacts 818) is a two-level pointer chase into a scattered 484 B block. `removeCachedConstraintAt` also memcpys 484 B per swap-remove (549). Per-triangle mesh contact keys turn this into steady malloc/free churn.

**Evidence.** collisionScene.h:134 `std::deque<ContactConstraint> cachedConstraints_{};`; removeCachedConstraintAt (collisionScene.cpp:548-553) `cachedConstraints_[index] = cachedConstraints_[lastIndex]; ... pop_back();`.

**Recommendation.** Replace with a fixed-capacity pool (`std::vector` reserved once at reset, or a plain array + free list) sized to a project max (`MAX_OBJ_COLLISION_CANDIDATES` precedent exists). Because collide.cpp holds `ContactConstraint*` from `createCachedConstraint` during per-triangle creation, either reserve capacity so a vector never reallocates mid-step, or convert callers to index-based handles. Swap-remove and the index-based lookup map stay unchanged.

**How to measure.** `ticksDetect` + `ticksRefreshCallbacks` + `ticksFinalize` with a body sliding across a triangle mesh (per-triangle churn); count mallocs/step before/after. Iteration loops should also speed up from contiguous layout.

**Risk & functionality preservation.** Medium — the reference-stability requirement (live `ContactConstraint*` mid-step) means a naive `vector` that reallocates would corrupt; reservation or handle conversion is mandatory.

### coll-narrow-localnormal-reciprocal-divs — unconditional reciprocal-scale divs + renormalize

**Location:** [meshCollider.cpp:83](../n64/engine/src/collision/meshCollider.cpp#L83)
**Impact:** Low *(adjusted from Medium)* · **Effort:** Small · **Risk:** Low · **Status:** Adjusted

**Claim.** `localNormalToWorld` unconditionally multiplies by `vec3ReciprocalScaleComponents(owner_->scale)` (3 divs) then always `vec3NormalizeOrFallback`, with no `hasScale()` guard. Called per raycast hit (collisionScene.cpp:1571), per swept candidate triangle (:1692, overlaps coll-narrow-sweep-* findings), and per contact via `localResultToWorld` (meshCollider.cpp:95).

**Evidence.** meshCollider.cpp:85-91: `worldNormal = worldNormal * vec3ReciprocalScaleComponents(owner_->scale); if(hasRotation()) ...; return vec3NormalizeOrFallback(worldNormal, VEC3_UP);` — no `hasScale()` guard. vecMath.h:17-23 shows the three `1.0f / scale.n` divisions.

**Recommendation (corrected).** Guard the `vec3ReciprocalScaleComponents` multiply on `hasScale()` — for unscaled meshes `1.0f/1.0f` and `x*1.0f` are exact, so this part is strictly bit-identical and saves 3 `div.s` (~87 cy)/call. **Do NOT** unconditionally skip the final `vec3NormalizeOrFallback`: input normals are only ~unit at float precision, so skipping renormalization is **not** bit-identical and changes observable output — gate it separately and document as behavior-sensitive, or cache reciprocal-scale + `hasScale_` in `syncOwnerTransform` for the scaled path.

**How to measure.** `get_ticks()` around the sweep triangle loop and `CollisionScene::raycast`; scenes with unscaled collision meshes show the delta directly.

**Risk & functionality preservation.** Low — the strictly-safe portion is only the ~3-div `hasScale()` guard; impact downgraded to low because the larger renormalize-skip is not behavior-preserving and the safe part partly overlaps the coll-narrow-sweep-* hoisting.

### coll-narrow-box-sat-normalizes-tri-normal — redundant triangle-normal normalization in box SAT

**Location:** [collide.cpp:518](../n64/engine/src/collision/collide.cpp#L518)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Adjusted

**Claim.** The triangle-face-normal SAT axis is obtained via `triangleNormalFromVertices`, which normalizes (`vec3NormalizeOrFallback` → sqrt + 3 divs), then the code computes `l2 = len2(localN)` (≈1.0) to feed `satAxisTest` — whose whole design (`axisLen2` parameter, "cross-multiplied comparison avoids sqrtf" comment at collide.cpp:451) exists to avoid exactly this normalization. One sqrt + 3 divs (~120 cy) wasted per box-vs-triangle SAT test, per candidate triangle for box bodies on a mesh.

**Evidence.** collide.cpp:517-523: `fm_vec3_t localN = ...triangleNormalFromVertices(v0,v1,v2); ... float l2 = fm_vec3_len2(&localN); if(!satAxisTest(boxHalf, triMin, triMax, l2, ...))`. Raw edge-cross axes at :546/:563/:580 are already used unnormalized; single final normalize at :590.

**Recommendation (corrected).** Use raw `cross(v1-v0, v2-v0)` (`e0` already computed at :492) with its true `len2` — `satAxisTest`'s cross-multiplied comparison and the single final normalize already handle unnormalized axes. **Flag that this is NOT bit-identical:** under `-fno-associative-math` the scaled comparison `depth^2 * len2` differs by float rounding from the ~1.0 normalized form, so the best-axis winner can flip in near-exact ties (e.g. a box resting flat where the face-normal and a box-face axis tie), changing the contact normal. Present as a behavior-sensitive micro-opt, not pure code motion.

**How to measure.** `ticksDetectMeshPairs` with box rigid bodies resting on a mesh; or instruction-count diff of `analyticalBoxTriangle`.

**Risk & functionality preservation.** Low performance risk but behavior-sensitive: can alter observable contact normals in near-degenerate ties. Requires sign-off if bit-exact contact output is required.

### coll-narrow-pair-double-hash-lookup — two constraint-cache lookups per overlapping non-analytical pair

**Location:** [collide.cpp:1433](../n64/engine/src/collision/collide.cpp#L1433)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** `collideDetectObjectToObject` builds a `ContactConstraintKey` and calls `scene->findCachedConstraint` (unordered_map hash + bucket pointer chase, ~40-70 cy + possible RDRAM miss) to fetch the cached separating axis; on overlap, `collideCacheContactConstraint` (collide.cpp:875-886) rebuilds the same key and calls `findCachedConstraint` again. Overlapping non-analytical pairs (box-box has no analytical path, TODO collide.cpp:1419) pay two lookups per frame.

**Evidence.** collide.cpp:1433-1434 `makeColliderPairConstraintKey` + `findCachedConstraint`; collide.cpp:876-886 rebuilds the key and calls `findCachedConstraint(key)` again inside `collideCacheContactConstraint`.

**Recommendation.** Thread the already-found `ContactConstraint*` from `collideDetectObjectToObject` into `collideCacheContactConstraint` (optional param defaulting to nullptr) so the second lookup is skipped. Passing the resolved pointer sidesteps the `shouldSwapColliderPairOrder` key-ordering nuance (collide.cpp:870).

**How to measure.** `ticksDetectBodyPairs` with a stack of boxes; or a debug counter of `findCachedConstraint` calls per frame.

**Risk & functionality preservation.** Low — behavior identical; the key ordering is deterministic for a given pair.

### coll-narrow-raycast-dead-normalize — discarded normalize result in ray_capsule/ray_cylinder

**Location:** [raycast.cpp:156](../n64/engine/src/collision/raycast.cpp#L156)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** `vec3NormalizeOrFallback(cylinderNormal, (-localRay.dir));` at raycast.cpp:156 and :249 returns by value with the result dropped, so the call is a pure no-op (the function takes a const ref and returns by value; `cylinderNormal` is never updated). Because the build enables `-ftrapping-math` (n64.mk:76, overriding `-ffast-math`'s `-fno-trapping-math`), GCC must preserve the FP ops (len2 mul/add, `sqrtf`, divisions) as potentially trap-raising even though the result is unused — so it is **not** dead-code-eliminated and the ~len2+sqrt+3div actually execute per cylinder/capsule-side ray hit.

**Evidence.** raycast.cpp:155-157: `cylinderNormal = {hitPoint.x, 0.0f, hitPoint.z}; vec3NormalizeOrFallback(cylinderNormal, (-localRay.dir));` — return value dropped (vecMath.h:25 returns `fm_vec3_t`, does not mutate its argument). Identical pattern at raycast.cpp:248-249.

**Recommendation.** Delete the no-op call. **Note for the author:** the surrounding code (e.g. ray_sphere:46) normalizes hit normals, so the likely *intended* fix is `cylinderNormal = vec3NormalizeOrFallback(...)` — that changes observable output (the normal is currently unnormalized) and should be decided as a bug fix, not silently folded into an optimization.

**How to measure.** Not separately measurable; batch with other raycast micro-fixes and verify via instruction count / `ovlCPU` CollBody component time.

**Risk & functionality preservation.** Low — deleting the call is strictly behavior-preserving (result already unused; no trap is observable on N64).

### coll-narrow-cone-trig-normal — atanf + sinf per cone-side ray hit

**Location:** [raycast.cpp:347](../n64/engine/src/collision/raycast.cpp#L347)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** For each accepted cone-side intersection the code calls `atanf(radius/h)` then `sinf(coneAngle)` — two libm transcendentals (hundreds of cy each on VR4300) inside the two-root loop, up to twice per ray, although `radius`/`h` are shape-invariant. GCC will not reliably hoist these (LICM of libm calls at `-Os` is not guaranteed, and they still execute once per ray).

**Evidence.** raycast.cpp:346-353: `axisToHit = axisToHit / axisToHitLen; float coneAngle = atanf(coll->coneShape().radius / h); sideNormal = { axisToHit.x, sinf(coneAngle), axisToHit.z }; fm_vec3_norm(&sideNormal, &sideNormal);`.

**Recommendation.** Replace with the exact algebraic form `sin(atan(r/h)) = (r/h)/sqrt(1+(r/h)^2)`, computed once (it is ray-invariant); the final `fm_vec3_norm` renormalizes anyway, so the direction is identical with only a `sqrt`. Removes both transcendentals.

**How to measure.** Only matters for cone-collider raycasts (rare). Microbenchmark `ray_cone_intersection` (1000 calls, `get_ticks`).

**Risk & functionality preservation.** Low — final normal differs only by float rounding.

### coll-narrow-double-fmin-friction — double-precision fmin on float args

**Location:** [collide.cpp:1516](../n64/engine/src/collision/collide.cpp#L1516)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** `float combinedFriction = fmin(colliderA->friction(), colliderB->friction());` calls double `fmin` on float args, forcing `CVT.S.D` on both operands, a double compare, and `CVT.D.S` back. `-Wdouble-promotion` does not catch argument promotion into a double-typed library function. Runs per colliding pair per frame in the `recordConstraints` path. The adjacent line :1517 uses `fmaxf`, and the mesh path collide.cpp:1164 uses `fminf`, confirming the double call is unintended.

**Evidence.** collide.cpp:1516-1517: `float combinedFriction = fmin(...); float combinedBounce = fmaxf(...);`.

**Recommendation.** Change `fmin` to `fminf`. Results identical (float→double→float min round-trip is exact).

**How to measure.** Assembly diff (`cvt.d.s` disappears); too small to time in isolation — batch with other fixes.

**Risk & functionality preservation.** Low — bit-identical.

### coll-broad-solver-shuffle-modulo — integer modulo per swap in the velocity-solver shuffle

**Location:** [collisionScene.cpp:1328](../n64/engine/src/collision/collisionScene.cpp#L1328)
**Impact:** Low *(adjusted from Medium)* · **Effort:** Small · **Risk:** Medium · **Status:** Adjusted

**Claim.** The Fisher-Yates shuffle runs inside the velocity iteration loop (up to `velocitySolverIterations_` = 8), doing `solverRand() % i` for every constraint each iteration. 32-bit `DIVU` is ~37 stall cycles; with ~20 constraints × 8 iters that is ~150 DIVUs/step (~5.5 kcy ≈ 60 µs), up to 5×/frame. The cost is genuine but is a modest fraction of the FPU-heavy per-contact-point velocity solve it sits inside.

**Evidence.** collisionScene.cpp:1327-1330: `for(std::size_t i = constraintCount; i > 1; --i) { std::size_t j = solverRand() % i; std::swap(solverConstraints_[i-1], solverConstraints_[j]); }`, inside the `for(uint8_t iter...` loop at 1321.

**Recommendation (corrected).** The DIVU cost is real, but **the recommended Lemire multiply-shift AND the hoist-out-of-loop alternative both change which permutation is drawn each iteration**, altering Gauss-Seidel solve order and thus last-bit velocity results — an observable/deterministic behavior change the project forbids. There is no drop-in reduction that yields the identical remainder sequence, so this is effectively **not actionable** without an explicit sign-off to re-baseline determinism. Do not apply as written; treat as a knowingly behavior-shifting micro-opt requiring approval.

**How to measure.** `ticksVelocitySolve` with 20+ active contacts; verify stack stability visually and determinism across runs if ever applied.

**Risk & functionality preservation.** Medium — any change here alters the drawn permutation and therefore simulation output; forbidden under the exact-behavior/determinism constraint without sign-off.

### coll-broad-readmask-cross-tu-call — readsCollider/readsMeshCollider out-of-line

**Location:** [colliderShape.cpp:135](../n64/engine/src/collision/colliderShape.cpp#L135)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** `readsCollider`/`readsMeshCollider` are two-load-AND-test one-liners (`readMask_ & other->writeMask_`) but are defined out-of-line in colliderShape.cpp, so at `-Os` without LTO every broadphase candidate pair pays a full `jal`/`jr` call twice (once per direction) in the hottest pair-rejection path.

**Evidence.** colliderShape.cpp:135-141 defines them; header colliderShape.h:96-97 only declares. Callers: collisionScene.cpp:1023 (twice per candidate pair), CCD 916/941, mesh 1061, and re-checked redundantly inside `collideDetectObjectToObject` 1362-1363.

**Recommendation.** Move both into the header as inline (`MeshCollider::writeMask()` is already a public accessor used at :140). Behavior identical. Optionally drop the duplicate mask re-check inside `collideDetectObjectToObject` when called from `detectAllContacts`.

**How to measure.** `ticksDetectBodyPairs` with 30 colliders in a cluster; verify via `mips64-elf-objdump` that the call disappears.

**Risk & functionality preservation.** Low — pure inlining; results identical.

### coll-broad-displacement-multiplier — 10× displacement fattening inflates candidate counts

**Location:** [aabbTree.h:19](../n64/engine/include/collision/aabbTree.h#L19)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** `moveNode` extends a reinserted leaf's bounds by 10× the per-step displacement (aabbTree.cpp:133). At 50 Hz a body moving v units/s gets its leaf extended by v/5 units — for fast movers the leaf becomes several times larger than the shape, so every nearby object becomes a broadphase candidate (hash insert + mask check + narrowphase attempt) each step until the body slows. Box2D ships 2-4× for the same scheme.

**Evidence.** aabbTree.h:19 `constexpr float AABB_DISPLACEMENT_MULTIPLIER = 10.0f;`, used at aabbTree.cpp:133-134 after recomputing the 10% fattened bounds (128-130).

**Recommendation.** Reduce the multiplier to 2-4 (tune on hardware). Broadphase-only: narrowphase still decides all contacts, so simulation is unchanged; CCD re-queries with its own substep AABBs (906-908). Becomes less critical if coll-broad-no-tight-aabb-prefilter lands first.

**How to measure.** Count broadphase candidates and `moveNode` reinsertion rate per step (temporary counters) + `ticksDetectBodyPairs`, with one fast projectile passing a cluster; sweep multiplier 2/4/10.

**Risk & functionality preservation.** Low — simulation unchanged; only a hardware-tuning knob.

### coll-broad-collbody-scale-rewrite — shape dimensions recomputed from scale every frame

**Location:** [collBody.cpp:101](../n64/engine/src/scene/components/collBody.cpp#L101)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** `CollBody::update` unconditionally computes `scaledHalfExtend = data->orgScale * obj.scale`, 3× `fabsf` × `getInvGfxScale()`, then a switch that writes into the collider shape union every rendered frame regardless of whether `obj.scale` changed. The writes dirty the collider struct the collision scene reads (false cache-line sharing).

**Evidence.** collBody.cpp:99-131: `fm_vec3_t scaledHalfExtend = data->orgScale * obj.scale; ... switch(...) { ... data->collider.boxShape().halfSize = scaledHalfExtend; ... }` — unconditional, per-frame.

**Recommendation.** Cache the last-applied `obj.scale` in the component and early-out when unchanged (exact float compare is safe: same value in, same value out). Only run the switch when scale actually differs.

**How to measure.** Component timing table (ovlCPU "CollBody") in a scene with 30+ CollBody objects; expect it to drop to near-zero when scales are static.

**Risk & functionality preservation.** Low — same input produces the same output; exact-float early-out preserves animated scales.

### coll-broad-aabbtree-node-layout — hot query fields straddle 3 D-cache lines

**Location:** [aabbTree.h:27](../n64/engine/include/collision/aabbTree.h#L27)
**Impact:** Low · **Effort:** Small · **Risk:** Low · **Status:** Confirmed

**Claim.** `AABBTreeNode` is 36 B: `bounds` (24 B) then `parent`, `left`, `right`, `next` (2 B each) then `data` (4 B). A `queryBounds` visit reads `bounds`+`left`+`right`, but `parent` sits between `bounds` and `left`/`right` and `next` is free-list-only, so traversal drags cold bytes through the 16 B-line direct-mapped 8 KB D-cache; nodes are not 16-aligned (36 % 16 ≠ 0), so successive pool entries shift phase and many visits cost 3 line fills instead of 2.

**Evidence.** aabbTree.h:27-34: `struct AABBTreeNode { AABB bounds{}; NodeProxy parent{...}; NodeProxy left{...}; NodeProxy right{...}; NodeProxy next{...}; void *data{nullptr}; };`. Traversal reads `bounds`, `.left`, `.right` (aabbTree.cpp:378-386).

**Recommendation.** Reorder to `bounds, left, right` first, then `data`, then `parent`/`next` in the tail (or split `parent`/`next` into a parallel array used only by insert/remove); consider padding/aligning to 40/48 B so the query working set is 2 lines. Pure index-accessed layout change, behavior-identical.

**How to measure.** `ticksDetectBodyPairs`/`DetMesh` and raycast-heavy timing on hardware (cache effects don't show in emulators); best with a deep tree and repeated queries per step.

**Risk & functionality preservation.** Low — all access is by index through the pool; behavior identical. The reorder alone barely shrinks the ~30 B query span; the alignment/padding is the real (hard-to-measure) lever.

### coll-narrow-raycast-no-range-shrink — ray range never shrunk after a closer hit

**Location:** [collisionScene.cpp:1577](../n64/engine/src/collision/collisionScene.cpp#L1577)
**Impact:** Low · **Effort:** Medium · **Risk:** Medium · **Status:** Confirmed

**Claim.** `CollisionScene::raycast` keeps testing all candidate triangles/colliders against the full original `maxDistance` even after a near hit is recorded. Per candidate triangle the loop runs `ray_triangle_intersection` then unconditionally `toWorldSpace` + `localNormalToWorld` (3 div + sqrt) + `fm_vec3_len` (:1570-1573) before the `< hit.distance && <= ray.maxDistance` compare (:1577); `hit.distance` is never fed back into `localRay.maxDistance`. Shrinking the working `maxDistance` after a hit lets `ray_triangle_intersection` reject farther candidates on its internal `t <= maxDistance` test, skipping the per-hit world transforms.

**Evidence.** collisionScene.cpp:1566-1580: per triangle, full intersection + world-space transform + `sqrtf` distance, then the distance compare; `hit.distance` never fed back into `localRay.maxDistance`. Same for the collider loop at :1600.

**Recommendation.** After accepting a hit, clamp the working ray's `maxDistance` to the accepted distance. **Correctness caveat:** local `t` vs world distance only match for unscaled meshes, so the clamp must be **skipped for scaled meshes** (conservatively). Closest-hit semantics and outputs are unchanged.

**How to measure.** `ovlCPU` CollBody component time / a `get_ticks()` wrapper around `CollisionScene::raycast` in a scene with dense overlapping collision meshes along the character's floor probe.

**Risk & functionality preservation.** Medium — multi-hit-per-ray is uncommon outside dense overlapping meshes (floor probes are short and `queryRay` already bounds candidates), and the scaled-mesh caveat must be handled or the closest hit can change.

## Considered & Rejected

- **coll-narrow-ray-sphere-redundant-math** — "ray_sphere_intersection recomputes dot(dir,dir) and divides twice by 2a" ([raycast.cpp:18](../n64/engine/src/collision/raycast.cpp#L18)). **Refuted.** `2.0f*a` is a common subexpression GCC already computes once (value-numbering, no associativity needed), and `-ffast-math` enables `-freciprocal-math` (`n64.mk:76` re-enables only `-ftrapping-math`), which already converts the two divisions by the reused value into one reciprocal + multiplies — exactly the finding's recommendation. Assuming `a == 1` is not compile-time provable (`dir` is runtime; `ray_sphere` is reachable for colliders) and removing the finiteness/degenerate-`a` guard changes behavior. The residual safe win (half-b form) is negligible, which the finding itself concedes. No actionable win beyond what `-Os` + `-ffast-math` already perform.

## Suggested Order of Attack

Ordered by value-per-risk, respecting dependencies. Batch the trivial bit-identical micro-fixes into one commit to amortize testing.

1. **coll-broad-dispatch-event-copies** (High, medium effort, low risk). Biggest per-step win in listener-free physics scenes, behavior-preserving via a per-object `onColl` flag. Do first.
2. **coll-broad-no-tight-aabb-prefilter** (Medium, small, low, strictly bit-identical). A 6-compare guard on the body-pair and CCD loops; do the body-pair variant only (skip the mesh variant). Land this **before** tuning coll-broad-displacement-multiplier, since it absorbs most of the fattening false positives.
3. **Batch the free, bit-identical narrowphase/sweep cleanups** — coll-narrow-sweep-zero-disp-mesh-waste, coll-narrow-sweep-per-triangle-world-transform (do these two together — the zero-disp skip removes 3 sweeps' worth of the transform cost), coll-narrow-cross-tu-call-overhead (small helpers only, watch I-cache), coll-narrow-capsule-sweep-invariant-recompute, coll-broad-finalize-inertia-sleeping, coll-broad-per-step-heap-allocs (option c), coll-narrow-pair-double-hash-lookup, coll-narrow-raycast-dead-normalize, coll-narrow-double-fmin-friction, coll-broad-readmask-cross-tu-call. All Medium/Low, small effort, low risk, provably behavior-preserving. High cumulative payoff for one test cycle.
4. **coll-narrow-capsule-mesh-gjk-epa-per-triangle** (High, medium effort, medium risk). The heaviest per-contact path, but it is an algorithmic addition requiring careful contact-convention matching against `analyticalSphereTriangle` — schedule its own review/test cycle. Higher raw ceiling than most items but gated behind non-character capsule/cylinder/cone/pyramid usage.
5. **coll-broad-sleeping-vs-static-narrowphase** (Medium, small effort, medium risk). Do **only** the safe re-activation variant (or the flagged static/immobile variant with the added `removeCollider` wake); the naive skip regresses wake-on-support-removal and static-static callbacks. Requires a wake-correctness test.
6. **coll-broad-deque-contact-storage** (Medium, medium effort, medium risk) then **coll-broad-island-scan-per-body** and **coll-broad-rigidbody-owner-hash-lookups** (Medium, medium effort, low risk). Data-structure changes best done as a group once the cheap wins are banked; the pool/reservation must preserve live-pointer stability, and the adjacency-list rebuild can reuse the pool's contiguity.
7. **coll-narrow-epa-simplex-zero-init** (Medium, small, medium risk). Worth it but audit read-before-write carefully and document the invariant.
8. **Low-value layout/tuning items** — coll-broad-aabbtree-node-layout, coll-broad-displacement-multiplier (after item 2), coll-broad-collbody-scale-rewrite, coll-narrow-localnormal-reciprocal-divs (safe `hasScale()` guard only), coll-narrow-cone-trig-normal, coll-narrow-raycast-no-range-shrink. Fill-in work; hardware-measure before/after.
9. **Do not apply without explicit sign-off:** coll-broad-solver-shuffle-modulo (changes drawn permutation → determinism) and coll-narrow-box-sat-normalizes-tri-normal (can flip contact normals in ties). Both are knowingly behavior-shifting.
