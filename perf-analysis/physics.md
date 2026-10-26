# Physics System Performance Analysis

_2026-07-06_

The physics step is already well-instrumented — 13 per-stage tick counters (`ticksWakePrep`..`ticksFinalize`, `ticksTotal`) are exported and displayed by the `ovlCPU` debug overlay — and it dirty-flags sensibly in several places (contact `transformVersion`, compound-property dirty bit, solver early-outs, sleeping islands). The remaining cost is fixed overhead that scales with body and contact count and multiplies on slow frames, because `step()` runs inside a fixed-dt accumulator that can tick multiple times per render frame. The highest-value work is in three places: sleep-island building rescans the entire cached-constraint list once per awake body (O(bodies × constraints)); collision-callback dispatch constructs and copies ~392-byte `CollEvent` structs 3-4x per contact pair into per-step-allocated containers even when no object has an `onColl` handler; and the character controller's `capsuleSweepTriangle` runs 20 sqrt-heavy sub-tests per candidate triangle with no cheap plane pre-rejection. A second tier of medium wins removes redundant world-inertia rebuilds, out-of-line quaternion rotations, per-step heap allocations, and repeated owner→body hash lookups. All recommendations below preserve observable behavior and determinism; none touch `particleSim.h`.

## Architecture & Frame Cost Model

Per render frame, `Scene::update` runs a fixed-timestep accumulator ([scene.cpp:270-287](../n64/engine/src/scene/scene.cpp#L270)): component `fixedUpdate` hooks, then `Coll::CollisionScene::step()` — possibly several times per frame at 50 Hz fixed dt. `step()` ([collisionScene.cpp:1809-1959](../n64/engine/src/collision/collisionScene.cpp#L1809)) runs, in order:

- `updateMeshColliderWorldStates` + `wakeBodiesTransformedExternally`.
- A collider world-state refresh loop, one `unordered_map` RigidBody lookup per collider.
- `syncCompoundProperties` + `updateWorldInertia` per **awake** body.
- `integrateVelocity` / `integrateAngularVelocity` per body.
- `detectAllContacts`: CCD substeps, broadphase AABB-tree queries, pair dedup via a per-step `unordered_set`, narrowphase in `collide.cpp` (another system's scope).
- `refreshContacts` (skipped per-constraint when `transformVersion` unchanged — good).
- `rebuildSolverConstraints`.
- `dispatchCollisionCallbacks`: builds + copies a `CollEvent` per active constraint every step.
- `preSolveContacts` (effective masses), `warmStart`, `solveVelocityConstraints` (up to 8 Gauss-Seidel iterations with a Fisher-Yates reshuffle of the constraint pointer array per iteration, early-out after ≥6 iterations, then one friction pass).
- `integratePosition` / `integrateRotation`.
- `solvePositionConstraints` (NGS, up to 7 iterations, re-derives world contact points per point per iteration).
- A finalize loop: `applyPositionConstraints` + `updateWorldInertia` for **all** bodies including sleeping, collider AABB re-sync, owner pos/rot writeback.
- `updateSleepStates` (island walk over all constraints per awake body), then the previous-step snapshot loop.

Contacts are cached in `std::deque<ContactConstraint>` ([collisionScene.h:134](../n64/engine/include/collision/collisionScene.h#L134)) keyed by an `unordered_map` for warm starting; `ContactConstraint` is ~484 bytes with 3 `ContactPoint`s of 7 `fm_vec3` each. `RigidBody` is ~470 bytes including 5 `Matrix3x3`. Against the 8 KB direct-mapped D-cache (16-byte lines, ~40-70 cy/miss) even ~15 bodies (7 KB) or ~17 constraints (>8 KB) exceed the cache, so the eight separate per-step body passes and the repeated full constraint scans re-miss lines each other just evicted.

`CharacterBody::moveAndSlide` ([characterBody.cpp:98-426](../n64/engine/src/collision/characterBody.cpp#L98)) is called from user scripts (the component update at [charBody.cpp:59-66](../n64/engine/src/scene/components/charBody.cpp#L59) is commented out). Per call it does an optional floor-attach carry, up to `settings.maxSlides` (default 4) `capsuleSweep` calls, up to 3 zero-displacement depenetration sweeps, and 1 floor-probe raycast. `CollisionScene::capsuleSweep` ([collisionScene.cpp:1638-1805](../n64/engine/src/collision/collisionScene.cpp#L1638)) queries the mesh AABB tree, world-transforms 3 vertices + normal per candidate triangle, and calls `capsuleSweepTriangle` ([capsuleSweep.cpp:269-338](../n64/engine/src/collision/capsuleSweep.cpp#L269)) which runs 20 sub-tests (2 sphere-face, 6 sphere-edge, 6 sphere-vertex, 3 cylinder-edge, 3 cylinder-vertex) per triangle; collider bodies use GJK+EPA. Quaternion-vector rotation everywhere (`RigidBody::rotateToWorld`, `contactUtils.h`) routes through libdragon's out-of-line `fm_vec3_rotate` ([fgeom.c:32](../../../libdragon/src/math/fgeom.c#L32)) via the `operator*` in `fgeom.h:641`, which `-Os` cannot inline.

**Instrumentation already present.** Every stage timing is pushed each frame and rendered by [ovlCPU.cpp:162-182](../n64/engine/src/debug/overlay/ovlCPU.cpp#L162): `sw_wake`, `sw_world`, `sw_intVel`, `sw_detBody`, `sw_detMesh`, `sw_refresh`, `sw_preSolve`, `sw_warmStart`, `sw_velSolve`, `sw_integrate`, `sw_posSolve`, `sw_finalize`, plus aggregate `Coll Det.`/`Coll Res.`. Most findings below validate directly against these counters on hardware; a few benefit from a dedicated `get_ticks()` bracket around the specific stage.

## Findings Overview

| ID | Title | Impact | Effort | Risk | Status |
|---|---|---|---|---|---|
| physics-sleep-island-constraint-scan | Sleep-island building rescans the entire constraint list per awake body | high | medium | low | Confirmed |
| physics-collision-event-copies | Collision-callback dispatch copies ~392-byte events 3-4x per pair, even with no listener | high | medium | low | Confirmed |
| physics-capsule-sweep-triangle-prereject | `capsuleSweepTriangle` runs 20 sqrt-heavy sub-tests per triangle with no plane pre-reject | high | medium | medium | Confirmed |
| physics-updateworldinertia-redundant-recompute | Full world-inertia rebuild runs unconditionally for sleeping/unchanged bodies | medium | small | low | Adjusted |
| physics-owner-hash-lookups | `findRigidBodyByOwner` hash lookups run per collider (and per pair) every step | medium | small | low | Confirmed |
| physics-quatrotate-out-of-line | Hot quaternion-vector rotations call libdragon's out-of-line `fm_vec3_rotate` | medium | small | low | Confirmed |
| physics-charbody-zero-displacement-mesh-pass | Depenetration probe world-transforms mesh triangles for a sweep that cannot hit | medium | small | low | Confirmed |
| physics-updateworldinertia-algebraic-waste | Redundant second quat→matrix conversion and dense mults where transpose + column-scale suffice | medium | small | low | Confirmed |
| physics-per-step-heap-allocs | Multiple containers are heap-allocated inside every physics step | medium | medium | low | Confirmed |
| physics-position-solver-redundant-refresh | Position solver re-derives world contact points every iteration for untouched constraints | medium | medium | medium | Confirmed |
| physics-solver-shuffle-modulo | Velocity-solver reshuffle does an integer DIVU per constraint per iteration | low | small | medium | Adjusted |
| physics-rigidbody-multipass-and-layout | `step()` makes ~8 separate passes over the ~470-byte RigidBody array | low | small | low | Confirmed |
| physics-dead-push-velocities | Split-impulse push velocities are zeroed every step but never read | low | small | low | Confirmed |
| physics-empty-component-updates | Empty `RigidBody::update`/`CharBody::update` are indirect-called per object per frame | low | small | low | Adjusted |
| physics-constraint-deque-layout | `ContactConstraint` cache is a `std::deque` of ~484-byte elements: one heap chunk each | low | medium | medium | Adjusted |

## Detailed Findings

### physics-sleep-island-constraint-scan — Sleep-island building rescans the entire constraint list per awake body

[collisionScene.cpp:718](../n64/engine/src/collision/collisionScene.cpp#L718)

**Claim.** `updateSleepStates()` builds contact islands by, for every visited awake body, iterating all `cachedConstraintCount_` constraints to find neighbours — O(awakeBodies × constraints) per step. Each constraint probe touches `isActive`/`isTrigger` (~offset 120) and `rigidBodyA`/`B` (~offset 20) in a ~484-byte struct that lives in its own 512-byte deque chunk, so each probe is 2-3 D-cache misses once the working set exceeds the 8 KB direct-mapped cache. 15 awake bodies × 30 constraints is ~450 scattered probes (~300 µs). `collectConnectedIsland` ([collisionScene.cpp:493](../n64/engine/src/collision/collisionScene.cpp#L493)) has the identical pattern and runs on every wake event.

**Evidence.** The inner scan makes each awake tracked body the DFS `current` exactly once (the `visited` set guarantees this), and for each it walks the whole constraint array:

```cpp
for(int i = 0; i < cachedConstraintCount_; ++i) {
  const ContactConstraint &cc = cachedConstraints_[i];
  if(!cc.isActive || cc.isTrigger) continue;
  RigidBody *other = nullptr;
  if(cc.rigidBodyA == current)      other = cc.rigidBodyB;
  else if(cc.rigidBodyB == current) other = cc.rigidBodyA;
  if(!other) continue;
  if(!shouldTrackSleepState(other)) continue;
  if(other->isSleeping_) continue;
  if(visited.find(other) == visited.end()) stack.push_back(other);
}
```

`cachedConstraints_` is confirmed `std::deque<ContactConstraint>` with `sizeof≈484 B`, so libstdc++ puts one element per 512-byte node; the working set (≈14.5 KB constraints + ≈7 KB RigidBodies for the `shouldTrackSleepState(other)` reads) far exceeds the 8 KB direct-mapped D-cache, so repeated full scans thrash. Called once per step from the finalize region ([collisionScene.cpp:1942](../n64/engine/src/collision/collisionScene.cpp#L1942)).

**Recommendation.** After `rebuildSolverConstraints()`, build a per-body adjacency structure once per step (a flat array of `(bodyIndex, otherBody)` edges sorted by body, or a small per-body edge list in scratch memory) from the active non-trigger constraints, then walk islands via the edge lists. This is an O(n×m)→O(n+m) restructure `-Os` cannot do.

**How to measure.** `ticksFinalize` (`sw_finalize`) with a stack/pile of 15+ awake bodies; or add a dedicated `get_ticks()` bracket around `updateSleepStates`.

**Risk & functionality preservation (low).** Island membership is connected-components and the sleep decision depends only on the *set* of island bodies, not visit order, so the restructure is even safer than visit-order preservation requires. Does not touch `particleSim.h`. Cost is chiefly a transient during active/settling piles of many connected awake bodies — once a pile sleeps, sleeping bodies are skipped and the cost collapses to O(n) — but that settling window is a legitimate physics-heavy moment, so the priority holds.

### physics-collision-event-copies — Collision-callback dispatch copies ~392-byte events 3-4x per pair, even with no listener

[collisionScene.cpp:626](../n64/engine/src/collision/collisionScene.cpp#L626)

**Claim.** For every active constraint, `makeCollisionEvent` returns a ~392-byte `CollEvent` by value (6 pointers + `std::array<ContactPoint,3>` = 360 B), `makeMirroredCollisionEvent` copies it again with swaps, and `captureDirectionalEvent` copies up to two of them into `PendingPairDispatch` entries (~800 B each) held in a vector `reserve()`d to `cachedConstraintCount_` each step, plus a per-step `unordered_map`. With 30 pairs that is ~36-48 KB of struct copying plus a ~24 KB vector allocation per step — pure bandwidth burn that evicts the whole 8 KB D-cache and steals RDRAM bandwidth from the RDP. It runs even when neither object has any component with an `onColl` handler; the `onColl` check happens only at the very end, inside `dispatchObjectCollisionEvent` ([scene.cpp:66-77](../n64/engine/src/scene/scene.cpp#L66)).

**Evidence.** Construction is unconditional per active constraint, with the per-step vector and map allocated up front:

```cpp
std::vector<PendingPairDispatch> pendingDispatches;
pendingDispatches.reserve(cachedConstraintCount_);
std::unordered_map<std::pair<const Object *, const Object *>, std::size_t, ObjectPairHash> dispatchLookup;
...
for(int i = 0; i < cachedConstraintCount_; ++i) {
  const ContactConstraint &constraint = cachedConstraints_[i];
  ...
  const CollEvent event = makeCollisionEvent(constraint);
  captureDirectionalEvent(dispatch, event);
  captureDirectionalEvent(dispatch, makeMirroredCollisionEvent(event));
}
```

`captureDirectionalEvent` gates the slot copy on layer-mask overlap (`collisionEventMasksOverlap`), *not* on `onColl` presence, so overlapping layers with zero `onColl` handlers still execute the ~392-byte slot copies. With no `onColl` component anywhere the entire pipeline is 100% wasted. `-Os` cannot elide it: `onColl` is a runtime/data-driven per-type function pointer in `COMP_TABLE`, so the compiler cannot prove no handler consumes the events.

**Recommendation.** Gate event construction on a cheap listener check: `onColl` is a static property of a component type, so an object's "has any `onColl`" bool is precomputable from its fixed component set — skip the pair entirely when neither `objectA` nor `objectB` has one. For pairs that do have listeners, build each directional event once directly into the pending slot (emplace / by-reference) instead of the value-copy chain, and hoist the containers into persistent members.

**How to measure.** `ticksRefreshCallbacks` (`sw_refresh`) in a scene with many touching bodies and no `onColl` components; expect it to collapse toward the `refreshContacts`-only cost. A dedicated `get_ticks()` bracket around `dispatchCollisionCallbacks` isolates it better, since `sw_refresh` also brackets `refreshContacts` + `rebuildSolverConstraints`.

**Risk & functionality preservation (low).** This is a post-solve callback stage that touches no simulation state, so determinism is fully preserved; gating on a precomputed static property only skips pairs where dispatch provably does nothing, giving bit-identical observable behavior. Does not touch `particleSim.h`. Magnitude is scenario-dependent (high at ~30 touching pairs where the copies blow the whole D-cache; medium for light scenes), but the no-`onColl` waste is 100% eliminable and common, which justifies the priority.

### physics-capsule-sweep-triangle-prereject — `capsuleSweepTriangle` runs 20 sqrt-heavy sub-tests per triangle with no plane pre-reject

[capsuleSweep.cpp:305](../n64/engine/src/collision/capsuleSweep.cpp#L305)

**Claim.** Every candidate triangle pays 2 sphere-face + 6 sphere-edge + 6 sphere-vertex + 3 cylinder-edge + 3 cylinder-vertex tests. The edge/vertex tests each contain 1-2 `sqrtf` (~29 cy) plus divides — `sphereEdgeTest` computes `sqrtf(elen2)` unconditionally (×6), `cylinderEdgeTest` `sqrtf(denom)` (×3), `cylinderVertexTest` `sqrtf(d1_len2)` (×3) — so even triangles that miss laterally burn ~10+ sqrt before returning `FLT_MAX`. A character sweeping over 10-30 broadphase triangles across up to 4 slide iterations is easily 100-500 µs per frame, the dominant character-controller cost. Triangles whose plane the swept capsule never approaches within radius `r` still run all 20 sub-tests (`sphereFaceTest`'s `sdist<-r` culls only its own face case).

**Evidence.** The full battery runs per triangle with no shared cheap pre-reject:

```cpp
for (const fm_vec3_t* S : {&P1, &P2}) {
  float t = sphereFaceTest(*S, radius, dir, dist, v0, v1, v2, triNormal, n, p, depth);
  update(t, n, p, depth);
  for (int i = 0; i < 3; ++i) { t = sphereEdgeTest(*S, radius, dir, dist, *verts[i], *verts[(i+1)%3], n, p, depth); update(t, n, p, depth); }
  for (const fm_vec3_t* V : verts) { t = sphereVertexTest(*S, radius, dir, dist, *V, n, p, depth); update(t, n, p, depth); }
}
for (int i = 0; i < 3; ++i) { float t = cylinderEdgeTest(P1, P2, radius, dir, dist, *verts[i], *verts[(i+1)%3], n, p, depth); update(t, n, p, depth); }
for (const fm_vec3_t* V : verts) { float t = cylinderVertexTest(P1, P2, radius, dir, dist, *V, n, p, depth); update(t, n, p, depth); }
```

`moveAndSlide` runs a slide loop up to `maxSlides` (default 4), each calling `capsuleSweep`, which loops broadphase candidates × triangles calling `capsuleSweepTriangle`. It is called once per frame per character from user scripts (jam25 `Player.cpp:197`, char_body `Controller.cpp:162`) — a genuine per-frame hot path.

**Recommendation.** Add an exact conservative pre-reject before the battery: compute signed distances of both capsule segment endpoints to the triangle plane at `t=0` and `t=dist` (4 dot products, no sqrt). Every possible contact point lies on the triangle, hence in its plane; if all four signed distances share sign with minimum magnitude > `r + SWEEP_EPS`, no sub-test can return a hit, so skip the triangle. Secondary: hoist `mesh->hasTransform()` out of the per-vertex ternaries in the caller ([collisionScene.cpp:1689-1692](../n64/engine/src/collision/collisionScene.cpp#L1689)), ideally per-mesh.

**How to measure.** Wrap `scene.capsuleSweep` in `get_ticks()` inside `moveAndSlide` (or use `ovlCPU` script/component timing for the calling script) while walking a character across dense collision mesh; count triangles rejected by the pre-test vs. total.

**Risk & functionality preservation (medium).** The plane signed-distance function is affine, so its extremes over the swept capsule segment occur at the 4 corners (`P1`, `P2`, `P1+disp`, `P2+disp`); the pre-reject only skips triangles for which all 20 sub-tests provably return no-hit, so it is bit-identical and deterministic. Touches only `capsuleSweep.cpp` + `collisionScene.cpp`, not `particleSim.h`. Realizable savings are geometry-dependent: floor triangles a character stands on have plane distance ~0 and are *not* rejected, so the win concentrates on nearby-but-untouched walls/ceilings and the loose far end of the swept AABB. Still, it is a free exact early-out on the dominant character-controller cost.

### physics-updateworldinertia-redundant-recompute — Full world-inertia rebuild runs unconditionally for sleeping/unchanged bodies

[collisionScene.cpp:1916](../n64/engine/src/collision/collisionScene.cpp#L1916)

**Claim (adjusted).** `updateWorldInertia()` is a heavy ~700-900 cycle call (2 `quatToMatrix3`, 2 full 3×3 mults via out-of-line struct-returning helpers, a transpose, a vec3 mult, 3 epsilon matrix comparisons, plus `refreshAngularConstraintProjection`/`refreshConstrainedInertiaTensor` and ~200 B of struct writes). It runs unconditionally for **all** bodies in the finalize loop ([collisionScene.cpp:1916](../n64/engine/src/collision/collisionScene.cpp#L1916)) with only a null/owner guard, and additionally for awake bodies in the world-update loop ([collisionScene.cpp:1843](../n64/engine/src/collision/collisionScene.cpp#L1843), which *does* skip sleeping via the `continue` at line 1842). So awake bodies pay it twice per step; sleeping bodies pay it once per step (finalize only) even though their position/rotation have not changed. Change detection is computed after all the work. The reliable saving is the sleeping-body finalize call: integration is skipped for sleeping bodies and the position solver only nudges bodies with penetration ≥ slop, so a settled sleeping body has bit-identical inputs and can be skipped entirely. Awake-but-resting bodies get gravity+solver micro-jitter each step, so exact-equality caching generally will not fire for them.

**Evidence.** The finalize loop has no sleep/change guard:

```cpp
for(RigidBody *body : rigidBodies_) {
  if(!body || !body->owner_) continue;
  body->applyPositionConstraints();
  body->updateWorldInertia();
  const std::vector<Collider *> *ownerColliders = findCollidersForOwner(body->owner_);
  ...
```

`updateWorldInertia` ([rigidBody.cpp:452-476](../n64/engine/src/collision/rigidBody.cpp#L452)) does the two `quatToMatrix3` conversions, the mult chain via out-of-line struct-returning matrix helpers ([rigidBody.cpp:94-134](../n64/engine/src/collision/rigidBody.cpp#L94)), the two refresh helpers, then the `matrix3Equals` change-detection *after* all the work.

**Recommendation (adjusted).** Cache the four inputs that fully determine the outputs (`rotation_` quat, `position_`, `invLocalInertiaTensor_`, `localCenterOfMass_`) from the last call and early-out with exact equality before recomputing anything. This is safe and deterministic: identical inputs produce bit-identical outputs, `transformVersion_` is not bumped either way, and the skipped `refreshAngularConstraintProjection`/`refreshConstrainedInertiaTensor` calls are idempotent under unchanged inputs (and are independently recomputed by `setConstraints`→`refreshConstraintCaches` whenever constraints change). Sleeping bodies then drop the ~800-cycle finalize rebuild to a ~13-float compare each step. `-Os` will not do this (it is cross-call input caching, not a local optimization).

**How to measure.** `ticksFinalize` and `ticksWorldUpdate` (`sw_finalize` / `sw_world`) with 10-20 resting/sleeping rigid bodies; expect both to drop sharply once bodies settle.

**Risk & functionality preservation (low).** The early-out reliably fires only for genuinely sleeping bodies, giving a clean but situational finalize-stage drop; awake-resting bodies get per-step micro-jitter that defeats exact-equality caching, and actively-simulating scenes (the case that dominates physics cost) get zero benefit plus a negligible added compare — hence the downgrade from high to medium. No parity concern.

### physics-owner-hash-lookups — `findRigidBodyByOwner` hash lookups run per collider (and per pair) every step

[collisionScene.cpp:1829](../n64/engine/src/collision/collisionScene.cpp#L1829)

**Claim.** The collider world-state refresh loop, both detection loops, the CCD path, and the finalize loop all resolve `owner→RigidBody` / `owner→colliders` through `unordered_map` hashing each step: at least 2-3 lookups per collider plus one per unique broadphase pair per frame. Each lookup is a hash plus 2 dependent RDRAM loads (bucket + node) — ~50-150 cy cold — totalling tens of microseconds with a few dozen colliders. Call sites confirmed at [collisionScene.cpp:1829](../n64/engine/src/collision/collisionScene.cpp#L1829) (per-collider refresh), 998 (per collider in pair detection), 1025 (per unique tested pair), 1045 (mesh loop), and `findCollidersForOwner` at 1918 (per body in finalize).

**Evidence.** `findRigidBodyByOwner` is an `unordered_map<const Object*, RigidBody*>` find ([collisionScene.h:131](../n64/engine/include/collision/collisionScene.h#L131)); the hot loops call it per collider and per unique pair every step.

**Recommendation.** Cache the owning `RigidBody*` directly on `Collider` (set in `addCollider`/`addRigidBody`, cleared in `removeRigidBody` — all central mutation points already exist in this file), and cache the collider-list pointer on `RigidBody` likewise. The maps remain for external queries; the hot loops read the cached pointers.

**How to measure.** `ticksWorldUpdate`, `ticksDetectBodyPairs`, `ticksDetectMeshPairs`, `ticksFinalize` in `ovlCPU` with ~30 colliders.

**Risk & functionality preservation (low).** Behavior identical — the cached pointers are populated and cleared at the same central mutation points the maps use. No parity concern.

### physics-quatrotate-out-of-line — Hot quaternion-vector rotations call libdragon's out-of-line `fm_vec3_rotate`

[rigidBody.cpp:487](../n64/engine/src/collision/rigidBody.cpp#L487)

**Claim.** `RigidBody::rotateToWorld`/`rotateToLocal`/`toWorldSpace`/`toLocalSpace` use `rotation_ * v`, which is `fgeom.h`'s `operator*` wrapping `fm_vec3_rotate`, compiled out-of-line in libdragon's archive ([fgeom.c:32](../../../libdragon/src/math/fgeom.c#L32)). Every call pays `jal`/`jr` overhead, argument marshalling through memory, and blocks CSE/scheduling. These run per contact point per position-solver iteration (via `refreshContactPointWorldState`), in contact-anchor computation, `integrateRotation` (2 calls), `updateCompoundProperties` (per collider), and `moveAndSlide` mesh vertex transforms.

**Evidence.** `rotateToWorld`/`rotateToLocal` ([rigidBody.cpp:486-492](../n64/engine/src/collision/rigidBody.cpp#L486)) use `rotation_ * localDir`; `fgeom.h:641` `inline operator*(fm_quat_t, fm_vec3_t)` just calls `fm_vec3_rotate`; `fgeom.c:32` defines it out-of-line (18 mul/add, takes pointers). Because the callee lives in the libdragon archive, `-Os` cannot inline it.

**Recommendation.** Add an `always_inline` header copy of `fm_vec3_rotate`'s exact arithmetic (same operation order — `qw*tx + (qy*tz - qz*ty)` form — bit-identical results) in `vecMath.h` and route the engine's quat×vec `operator*` uses in collision code through it. Building the collision TUs at `-O2` will not help because the callee is in the archive; the inline copy is the effective fix.

**How to measure.** `ticksPositionSolve`, `ticksPreSolve`, `ticksIntegration` in `ovlCPU`; `objdump` check that `fm_vec3_rotate` call sites disappear from the collision objects.

**Risk & functionality preservation (low).** Identical arithmetic order gives bit-identical results; the change removes only `jal`/`jr` and pointer marshalling. Per-call math is only ~18 flops so the per-call saving is call-overhead-sized, but call count is high. No parity concern.

### physics-charbody-zero-displacement-mesh-pass — Depenetration probe world-transforms mesh triangles for a sweep that cannot hit

[characterBody.cpp:308](../n64/engine/src/collision/characterBody.cpp#L308)

**Claim.** `moveAndSlide`'s depenetration block always runs at least one `capsuleSweep` with `displacement = VEC3_ZERO`. In `capsuleSweep` the mesh path then queries the mesh AABB tree, computes `localSweptBox`, queries triangle nodes, and world-transforms 3 vertices + normal per candidate triangle — before `capsuleSweepTriangle` immediately returns false because `dist = sqrtf(len2(displacement)) < SWEEP_EPS` ([capsuleSweep.cpp:279-280](../n64/engine/src/collision/capsuleSweep.cpp#L279)). For a grounded character this wastes a tree query plus ~4 transforms × N triangles under the capsule every frame (also true for idle characters whose slide loop broke immediately).

**Evidence.** `characterBody.cpp:300-314`: `depProbe = VEC3_ZERO` and the loop always executes `di=0`, calling `scene.capsuleSweep` with zero displacement; the mesh block ([collisionScene.cpp:1683-1704](../n64/engine/src/collision/collisionScene.cpp#L1683)) transforms `wv0`/`wv1`/`wv2`/`wn` per candidate triangle, then `capsuleSweepTriangle` returns false at the `dist < SWEEP_EPS` guard.

**Recommendation.** In `CollisionScene::capsuleSweep`, guard the entire `doMesh` block on `fm_vec3_len2(&displacement) < SWEEP_EPS*SWEEP_EPS` — `capsuleSweepTriangle` cannot report a hit for such displacements, so results are bit-identical. The GJK/EPA collider-body path (1708+), which genuinely handles zero-displacement start-position overlap, still runs. One-line guard.

**How to measure.** `get_ticks()` bracket around the depenetration loop in `moveAndSlide` (or script timing in `ovlCPU`) with a character standing on a rotated/scaled collision mesh.

**Risk & functionality preservation (low).** Bit-identical — the mesh path returns nothing for zero displacement anyway. No parity concern.

### physics-updateworldinertia-algebraic-waste — Redundant second quat→matrix conversion and dense mults where transpose + column-scale suffice

[rigidBody.cpp:454](../n64/engine/src/collision/rigidBody.cpp#L454)

**Claim.** `quatToMatrix3(quatConjugate(rotation_))` is element-for-element the transpose of `quatToMatrix3(rotation_)` with identical floating-point arithmetic (each term is a product of the same magnitudes with sign flips that cancel or move exactly), so the second conversion (~21 mul/add) is redundant. Likewise `matrix3Mul(R, diagonalMatrix(v))` multiplies through two structural zeros per element (27 muls + 18 adds where column scaling needs 9 muls). The same diagonal pattern repeats in `refreshAngularConstraintProjection` ([rigidBody.cpp:255](../n64/engine/src/collision/rigidBody.cpp#L255)). These run 2× per awake body per step.

**Evidence.** `rigidBody.cpp:453-457` builds `newRotationMatrix` and `newInverseRotationMatrix = quatToMatrix3(quatConjugate(rotation_))`, then `matrix3Mul(matrix3Mul(newRotationMatrix, localInv), matrix3Transpose(newRotationMatrix))`. For conjugate `q'=(w,-x,-y,-z)`, `xy'=(-x)(-y)=xy` and `wz'=w(-z)=-(wz)` are exact IEEE sign-only changes, so `quatToMatrix3(conj)` element `[0][1]=2(xy+wz)` equals the transpose element bit-identically (`a-(-v) == a+v`).

**Recommendation.** Set `inverseRotationMatrix_ = matrix3Transpose(newRotationMatrix)` (bit-identical), and add a `matrix3MulDiagRight(R, v)` helper computing `r[i][j] = R[i][j] * v[j]` (bit-identical except for `+0/-0` corner cases that cannot arise from finite rotation matrices). Apply the same to `refreshAngularConstraintProjection`.

**How to measure.** `ticksWorldUpdate` and `ticksFinalize` (`sw_world`/`sw_finalize`) with 10+ tumbling bodies.

**Risk & functionality preservation (low).** Sign flips are exact in IEEE and the diagonal mult only skips provably-zero terms, so results are bit-identical for finite rotation matrices. No parity concern. Small but real FP savings.

### physics-per-step-heap-allocs — Multiple containers are heap-allocated inside every physics step

[collisionScene.cpp:982](../n64/engine/src/collision/collisionScene.cpp#L982)

**Claim.** Each step performs several mallocs/frees on N64's single-threaded allocator: `detectAllContacts` creates a fresh `std::unordered_set<int32_t> tested_pairs` (one node allocation per inserted pair) plus `candidateColliders`/`candidateMeshColliders`; `detectSweptCollisions` unconditionally `resize()`s two vectors sized `colliders_`/`meshColliders_` *before* the per-body CCD early-continue, so they allocate even when no body qualifies; `updateSleepStates` allocates an `unordered_set` plus island/stack vectors; `dispatchCollisionCallbacks` allocates a vector + `unordered_map`; `wakeIsland` allocates a vector + set per wake. Steady-state that is dozens of malloc/free per step plus fragmentation, scaling with pair count.

**Evidence.** `collisionScene.cpp:982-989` (`tested_pairs` + candidate vectors), 842-846 (two `NodeProxy` vectors resized before the early-continue body loop at 849), 696 (`visited`), 602-604 (`pendingDispatches`/`dispatchLookup`), 663-664 (`wakeIsland` vector+set).

**Recommendation.** Hoist all of these into `CollisionScene` scratch members cleared per step (vectors keep capacity across frames). Replace `tested_pairs` with a persistent open-addressing set keyed on the int32 pair key (or a sorted flat vector deduped once), and `visited`-sets with a per-body `uint8` stamp/epoch counter in scratch arrays indexed by body index. Move the `detectSweptCollisions` vector setup below a first pass that checks whether any body needs CCD.

**How to measure.** `ticksDetect`/`ticksDetectBodyPairs` and `ticksFinalize`; optionally instrument malloc call count per frame (libdragon heap stats) before/after.

**Risk & functionality preservation (low).** All dedup/visit semantics stay identical — only the backing storage lifetime changes. No parity concern.

### physics-position-solver-redundant-refresh — Position solver re-derives world contact points every iteration for untouched constraints

[collisionScene.cpp:1416](../n64/engine/src/collision/collisionScene.cpp#L1416)

**Claim.** `solvePositionConstraints` (up to 7 iterations) calls `refreshContactPointWorldState(cp, cc, true)` per contact point per iteration — 2 out-of-line quat-vector rotations, a dot, and 2 reference-offset computations — *before* checking `penetration < slop`. Once an iteration applies no correction to either body of a constraint, subsequent refreshes recompute identical values; the only global early-out is when no constraint anywhere applied a correction, so one deep contact keeps re-refreshing every separated contact in the scene 7 times. 20 contacts × 3 pts × 7 iter × 2 rotations ≈ 840 out-of-line rotates ≈ hundreds of µs.

**Evidence.** `collisionScene.cpp:1412-1421` refreshes each active point before the `cp.penetration < slop` skip; the outer loop ([collisionScene.cpp:1902-1906](../n64/engine/src/collision/collisionScene.cpp#L1902)) runs up to `positionSolverIterations_ = 7` with the only early-out being "no correction applied anywhere". `refreshContactPointWorldState` ([contactUtils.h:88-106](../n64/engine/include/collision/contactUtils.h#L88)) does two `contactWorldPointFromLocalPoint` quat rotations.

**Recommendation.** Track a per-body "moved this position pass" flag (set whenever `position_` or `rotation_` is corrected, plus once initially). At the start of each constraint, skip the refresh (and the whole point loop) when neither body moved since that constraint was last refreshed — the recomputed values would be bit-identical and `penetration < slop` would still hold.

**How to measure.** `ticksPositionSolve` (`sw_posSolve`) in a scene with one penetrating stack plus many resting contacts.

**Risk & functionality preservation (medium).** Skipping preserves behavior exactly because unchanged bodies recompute identical values, but the flag bookkeeping must be exact across constraints solved earlier in the same iteration — a body corrected by an earlier constraint must invalidate later constraints that reference it. No parity concern (does not touch `particleSim.h`).

### physics-solver-shuffle-modulo — Velocity-solver reshuffle does an integer DIVU per constraint per iteration

[collisionScene.cpp:1328](../n64/engine/src/collision/collisionScene.cpp#L1328)

**Claim.** The Fisher-Yates reshuffle runs every velocity iteration (min 6, up to 8) and computes `solverRand() % i` per swap. Integer DIVU on the VR4300 is ~35-37 stall cycles; with N constraints that is ~(N-1)×iterations divisions — for N=20 and 8 iterations ~152 DIVU ≈ 5600 cy (~60 µs) of pure divide stall per step, but only at high constraint counts.

**Evidence.** `collisionScene.cpp:1327-1330`: `for(i=constraintCount; i>1; --i){ j = solverRand()%i; swap(solverConstraints_[i-1], solverConstraints_[j]); }` inside the velocity-iteration loop. `solverRand` ([collisionScene.cpp:1234-1241](../n64/engine/src/collision/collisionScene.cpp#L1234)) is a deterministic xorshift with static state.

**Recommendation (adjusted).** The DIVU-per-swap cost is real, but Lemire/any division-free reduction changes which permutation each deterministic `solverRand()` draw yields, altering solver processing order and producing bit-different body positions — a genuine observable/determinism change, which this task forbids. Only adopt it if a one-time break in replay/preview determinism is explicitly acceptable; otherwise keep the modulo. No bit-identical division-free replacement exists.

**How to measure.** `ticksVelocitySolve` (`sw_velSolve`) with ~20+ active constraints; expect a fixed ~30-60 µs drop *if* the modulo is replaced.

**Risk & functionality preservation (medium, gated).** The permutation sequence is part of observable simulation output (final body positions). Because there is no cheap division-free way to reproduce `solverRand() % i` bit-for-bit, the fix is only applicable if the project accepts a one-time determinism change — hence the downgrade to low impact.

### physics-rigidbody-multipass-and-layout — `step()` makes ~8 separate passes over the ~470-byte RigidBody array

[collisionScene.cpp:1840](../n64/engine/src/collision/collisionScene.cpp#L1840)

**Claim.** `rigidBodies_` is traversed separately for world update (1840), velocity integration (1849), push-velocity reset (1877), position/rotation integration (1891), finalize (1911), sleep update (698), external-transform wake (684), and previous-step snapshot (1944) — eight per-step traversals of the pointer vector, each dereferencing scattered ~470-byte structs. Each `RigidBody` spans ~30 16-byte D-cache lines with solver-hot fields interleaved with cold config ([rigidBody.h:171-218](../n64/engine/include/collision/rigidBody.h#L171)), so with >15 bodies each pass re-misses lines the previous pass evicted.

**Evidence.** `for(RigidBody *body : rigidBodies_)` at lines 684, 698 (via `updateSleepStates`), 1840, 1849, 1877, 1891, 1911, 1944.

**Recommendation.** Fuse the trivially-adjacent passes: fold `resetPushVelocities` (1877) into the `integrateVelocity` loop (1849), and fold the previous-step snapshot (1944) into the finalize loop (1911) — the snapshot reads `isSleeping_` set by `updateSleepStates` (1942, which precedes it), so snapshot after the sleep update inside one loop. Separately, reorder `RigidBody` so velocity-solver fields (`linearVelocity_`, `angularVelocity_`, `worldCenterOfMass_`, `constrainedInvWorldInertiaTensor_`/`invWorldInertiaTensor_`, `inverseMass_`, flags) are contiguous at the front.

**How to measure.** `ticksIntegrateVel + ticksIntegration + ticksFinalize` sum in `ovlCPU` with 20+ bodies; layout change validated via `ticksVelocitySolve`.

**Risk & functionality preservation (low).** The two fusions are adjacent and order-preserving; the layout reorder is behavior-neutral. Impact is genuinely low and scene-size dependent. No parity concern.

### physics-dead-push-velocities — Split-impulse push velocities are zeroed every step but never read

[collisionScene.cpp:1877](../n64/engine/src/collision/collisionScene.cpp#L1877)

**Claim.** `resetPushVelocities()` runs in its own pass over all bodies each step, but `pushLinearVelocity_`/`pushAngularVelocity_` are never written by the solver nor read by integration. This is dead per-step work plus 24 bytes of dead payload bloating the hot `RigidBody` struct.

**Evidence.** Repo-wide grep for `pushLinearVelocity`/`pushAngularVelocity` finds only the declaration+accessors ([rigidBody.h:164-166/217-218](../n64/engine/include/collision/rigidBody.h#L164)) and the single reset site at `collisionScene.cpp:1877-1879`; the position solver applies corrections directly to `position_`/`rotation_` (1449/1460).

**Recommendation.** Delete the reset loop, the fields, and the unused accessors (or keep them behind a future split-impulse implementation). Nothing consumes the values, so observable behavior is unchanged.

**How to measure.** `ticksWarmStart` (`sw_warmStart`, the reset is inside that timed region); mostly validated via code size and `RigidBody` `sizeof`.

**Risk & functionality preservation (low).** Behavior-neutral removal of dead code. No parity concern.

### physics-empty-component-updates — Empty `RigidBody::update`/`CharBody::update` are indirect-called per object per frame

[rigidBody.cpp:85](../n64/engine/src/scene/components/rigidBody.cpp#L85)

**Claim.** `componentTable.cpp` auto-detects `update` via template introspection; because both components define empty update bodies, every object carrying a `RigidBody` or `CharBody` component pays an indirect function call (no branch prediction on VR4300, plus I-cache pollution) per frame in the scene component-update loop for zero work.

**Evidence.** `rigidBody.cpp:85-87` is an empty body; `charBody.cpp:59-66` has a fully commented-out body; `componentTable.cpp` registers `.update = &T::update` whenever the member exists, so both get a non-null pointer indirect-called at [scene.cpp:310](../n64/engine/src/scene/scene.cpp#L310).

**Recommendation (adjusted).** Do **not** simply delete the empty update methods — the update dispatch loop ([scene.cpp:305-311](../n64/engine/src/scene/scene.cpp#L305)) has no null guard and all 14 components currently register a non-null `update`, so nulling two would call a null function pointer at scene.cpp:310 and crash. First add `if(!compDef.update) continue;` to the update loop to match the `fixedUpdate` loop ([scene.cpp:278](../n64/engine/src/scene/scene.cpp#L278)), then delete empty update bodies (RigidBody, CharBody, and any other components whose update is empty). Behavior identical, and it skips every empty-update component, not just these two.

**How to measure.** `scene.ticksActorUpdate` (`sw_updObj`) in a scene with many rigid-body objects; expect a small fixed drop per object.

**Risk & functionality preservation (low).** The guard is the real fix and must land before any body is deleted; with it in place the change is behavior-identical. No parity concern.

### physics-constraint-deque-layout — `ContactConstraint` cache is a `std::deque` of ~484-byte elements

[collisionScene.h:134](../n64/engine/include/collision/collisionScene.h#L134)

**Claim (adjusted).** `cachedConstraints_` as `std::deque` puts each ~484-byte `ContactConstraint` in its own 512-byte node (malloc per new contact, `operator[]` double-indirection, full-struct copy on swap-remove at [collisionScene.cpp:549](../n64/engine/src/collision/collisionScene.cpp#L549)). A contiguous pool removes those, but the claimed velocity-solver speedup "from contiguity alone" is undermined by the intentional per-iteration Fisher-Yates reshuffle ([collisionScene.cpp:1327-1330](../n64/engine/src/collision/collisionScene.cpp#L1327)), which randomizes traversal and thrashes the 8 KB cache whatever the layout; contiguity only helps the index-order passes (`preSolve`).

**Evidence.** `collisionScene.h:134` `std::deque<ContactConstraint> cachedConstraints_{}`; `ContactConstraint` ([contact.h:124-153](../n64/engine/include/collision/contact.h#L124)) holds `ContactPoint points[3]` where each `ContactPoint` is 7 `fm_vec3` + 8 floats (~120 B), giving ~484 B, one element per 512-byte deque node.

**Recommendation.** Replace the deque with a fixed-capacity pool (vector reserved once, or a static array with a free-list) so constraints are contiguous; keep the existing swap-with-last removal and index-based lookup map — pointer/index-stability requirements are the same as today. Optionally split `ContactPoint` into hot solver fields (`aToContact`, `bToContact`, masses, accumulated impulses, velocity bias) and cold anchor fields (`localPointA`/`B`, `contactA`/`B`, `point`) to shrink the per-iteration working set; the SoA split helps the shuffled solver more than contiguity does.

**How to measure.** `ticksVelocitySolve` and `ticksPreSolve` (`sw_velSolve`/`sw_preSolve`) with 20+ active contacts; expect the drop mainly in `preSolve` and other index-order passes, not the shuffled velocity solve.

**Risk & functionality preservation (medium).** Pointer/index stability must be preserved across the step (the lookup map and `solverConstraints_` are rebuilt/updated at the same points today). Impact corrected to low because the headline solver speedup does not materialize under the intentional reshuffle. No parity concern.

## Considered & Rejected

None. All 15 findings survived verification (11 confirmed, 4 adjusted with corrected claim/recommendation/impact applied above). No finding was refuted, and none were contested.

## Suggested Order of Attack

1. **`physics-charbody-zero-displacement-mesh-pass`** and **`physics-dead-push-velocities`** first — both are one-line/dead-code changes, low risk, immediate. The zero-displacement guard directly cuts a per-frame grounded-character cost.
2. **`physics-empty-component-updates`** — land the `if(!compDef.update) continue;` guard in the update loop (this is the real win and also unblocks safely nulling any empty component update), then delete the empty bodies.
3. **`physics-collision-event-copies`** (high) — the largest single win in touching-body scenes and low-risk (post-solve, no simulation state). Requires a precomputed per-object "has any `onColl`" bool; do this before other detection-stage refactors so the listener gate is in place.
4. **`physics-sleep-island-constraint-scan`** (high) — the adjacency-list rebuild. Best done alongside or just after `physics-per-step-heap-allocs`, since both want persistent per-body scratch arrays (stamp/epoch `visited`, edge lists), so share the scratch-buffer infrastructure.
5. **`physics-per-step-heap-allocs`** — hoist per-step containers into cleared scratch members; provides the buffers step 4 uses.
6. **`physics-owner-hash-lookups`** and **`physics-updateworldinertia-redundant-recompute`** — small, low-risk medium wins that shrink the per-body loops before the layout/pass-fusion work.
7. **`physics-quatrotate-out-of-line`** and **`physics-updateworldinertia-algebraic-waste`** — the FP/inlining micro-optimizations; both feed the position solver and world-inertia paths, so land them before measuring `physics-position-solver-redundant-refresh`.
8. **`physics-capsule-sweep-triangle-prereject`** (high) — the plane pre-reject. Medium risk (needs careful conservative-bound reasoning) but a large character-controller win; schedule as a focused task with the before/after triangle-rejection instrumentation.
9. **`physics-position-solver-redundant-refresh`** — medium risk (exact per-body moved-flag bookkeeping across constraints in the same iteration); do after the quat-rotate inline (step 7) so the remaining cost is clearly attributable.
10. **`physics-rigidbody-multipass-and-layout`** and **`physics-constraint-deque-layout`** — structural layout changes with the smallest measured payoff; do last, and re-measure after the earlier passes since some of their benefit is already captured by fewer/cheaper passes.
11. **`physics-solver-shuffle-modulo`** — defer indefinitely unless a one-time determinism/replay break is explicitly accepted by the team; there is no bit-identical division-free replacement.
