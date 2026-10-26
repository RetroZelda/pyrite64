# Pyrite64 Engine — N64 Performance Analysis

**Date:** 2026-07-06
**Target:** N64 hardware only (VR4300 @ 93.75 MHz, libdragon + tiny3d). This is analysis and prioritization — **no engine code was changed.**

This folder holds a per-system performance audit of the Pyrite64 N64 runtime engine, focused on the four heavy hitters you called out — collision, physics, animation, particles — plus a cross-cutting engine-core pass that covers the frame loop, matrices, allocation, and culling that feed those systems.

## Reports

| Report | Findings kept | Highlights |
|---|---|---|
| [collision.md](collision.md) | 27 (2 high) | Capsule/cylinder/cone vs mesh runs full GJK+EPA per triangle; per-contact CollEvent copies with no listeners; no tight-AABB prefilter |
| [physics.md](physics.md) | 15 (3 high) | O(bodies×constraints) sleep-island scan; 392-byte event copies per pair; sqrt-heavy capsule-sweep with no plane pre-reject |
| [animation.md](animation.md) | 11 (1 high) | Full skeleton rebuild for culled/hidden objects; keyframes streamed through a locked libc FILE every frame; static matrices rebuilt into uncached RAM |
| [particles.md](particles.md) | 13 (2 high) | Depth sort recomputes distances per comparison; `rspq_wait()` full-pipeline drain mid-update; 9 narrow uncached stores per particle |
| [engine-core.md](engine-core.md) | 11 (1 high) | Static model matrices rebuilt/uploaded to uncached RAM every frame; always-on `unordered_map` timing instrumentation in the hot loops |

**76 findings survived verification** (58 confirmed as written, 18 corrected during verification), **1 was refuted**, **0 left contested.** By impact: 9 high, 37 medium, 30 low.

## How this was produced (and how much to trust it)

Six analysis agents each read one system end-to-end and reported findings grounded in specific `file:line` citations. Every finding was then re-checked by an adversarial reviewer that re-read the cited code and the call sites to establish true per-frame frequency; each of the 16 originally-high-impact findings additionally got a second, independent refuter. A finding is only "confirmed" if it survived that as written; "adjusted" means the claim, recommendation, or impact tier was corrected during verification (several originally-high findings were honestly downgraded to medium once the realistic call frequency was checked); "refuted" findings are recorded in each report's *Considered & Rejected* section so they don't get re-litigated.

**Caveats worth your eye when you're back:**
- Impact tiers are reasoned from VR4300 costs and code-path frequency, **not measured on hardware.** Every finding lists how to measure it — mostly via the stage-tick counters already wired into `ovlCPU`/`ovlComponents`, or a `get_ticks()` bracket. Confirm the big ones before investing.
- A few strong recommendations touch **tiny3d** (`../../tiny3d`), not this repo — the animation report flags those as dependency changes. Your fork owns that checkout, so they're fair game, but they're not engine-local edits.
- Nothing recommended requires editing `particleSim.h` in a way that breaks editor/hardware preview parity; where a particle fix gets near it, the report says so explicitly and constrains the change to stay bit-identical.

## The 9 high-impact findings (cross-system)

Ordered by risk-adjusted value — safe wins first.

| Impact | Effort | Risk | System | Finding |
|---|---|---|---|---|
| high | small | low | particles | [Depth sort recomputes squared camera distance inside the comparator](particles.md#sort-recomputes-squared-camera-distance-inside-the-comparator) — precompute one float key per particle; bit-identical, engine-only |
| high | small | low | engine-core | [`Model::draw` rebuilds & re-uploads the object matrix to uncached RAM every frame](engine-core.md#model-matrix-rebuild-every-frame--modeldraw-rebuilds-and-re-uploads-the-object-matrix-to-uncached-ram-every-frame) — add a dirty flag on the SRT; ~100-200µs on a 50-static-model scene |
| high | medium | low | collision | [`dispatchCollisionCallbacks` copies ~2.4 KB of `CollEvent` per contact even with no listeners](collision.md#coll-broad-dispatch-event-copies--collevent-copy-traffic-per-contact-even-with-no-listeners) — gate on whether any `onColl` handler exists |
| high | medium | low | physics | [Sleep-island building rescans the entire constraint list per awake body](physics.md#physics-sleep-island-constraint-scan--sleep-island-building-rescans-the-entire-constraint-list-per-awake-body) — O(bodies×constraints) → linear via a per-body adjacency list |
| high | medium | low | physics | [Collision-callback dispatch copies ~392-byte events 3-4× per pair, even with no listener](physics.md#physics-collision-event-copies--collision-callback-dispatch-copies-392-byte-events-3-4x-per-pair-even-with-no-listener) — same listener-gate idea as the broadphase copy |
| high | medium | medium | physics | [`capsuleSweepTriangle` runs 20 sqrt-heavy sub-tests per triangle with no plane pre-reject](physics.md#physics-capsule-sweep-triangle-prereject--capsulesweeptriangle-runs-20-sqrt-heavy-sub-tests-per-triangle-with-no-plane-pre-reject) — add an exact affine plane-distance early-out |
| high | medium | medium | collision | [Capsule/cylinder/cone/pyramid vs mesh run full GJK+EPA per candidate triangle](collision.md#coll-narrow-capsule-mesh-gjk-epa-per-triangle--no-analytical-capsule-vs-triangle-path) — the heaviest per-contact path; needs analytical fast paths or warm-starting |
| high | medium | medium | animation | [Full skeleton matrix rebuild runs every frame for culled/hidden objects](animation.md#full-skeleton-matrix-rebuild-runs-every-frame-for-objects-that-are-culled-or-hidden) — gate skeleton update on visibility + a frame-stamped dirty flag |
| high | medium | medium | particles | [Emitter reclaim stalls CPU on a full RSP+RDP drain mid-update](particles.md#emitter-reclaim-stalls-the-cpu-on-a-full-rsprdp-pipeline-drain-mid-update) — defer reclamation via a frame-counter/syncpoint free list instead of `rspq_wait()` |

## Suggested global order of attack

1. **Two free high-impact wins first** — the particle sort key and the static model-matrix dirty flag are both small, low-risk, bit-identical, and each recovers real per-frame time. Do these regardless of profiling.
2. **The listener-gate pair** — collision and physics both materialize large `CollEvent` copies for contacts nobody listens to. It's one shared idea (skip event construction when neither object has an `onColl`), medium effort, low risk, and it lands across both systems.
3. **The character-controller path** — the capsule-sweep plane pre-reject and the zero-displacement mesh-pass skip together attack the dominant character cost. Medium risk (they change *which* triangles get tested, not results), so profile the before/after with a `get_ticks()` bracket around `moveAndSlide`.
4. **Skeleton-update visibility gate** — biggest structural animation win, but medium risk because it moves work from update into a draw/dirty-flag path; verify culled characters still animate correctly when they come back on-screen.
5. **The emitter teardown hitch** — high value on the specific frames it hits (fire-and-forget effects completing), medium risk because it changes reclamation lifetime; needs care that a slot isn't reused before the RSP finishes with it.
6. **The 24 quick wins** (small effort + low risk, listed per-report) — batch these together. Individually small, collectively a meaningful chunk of fixed per-frame overhead: hoisted loop-invariants, out-of-line math brought inline, per-step heap allocations pooled, redundant recomputations dirty-flagged. Low regression risk makes them a good "clean sweep" pass.
7. **Mesh-vs-convex narrowphase** (analytical fast paths / warm-starting per-triangle GJK+EPA) is the largest *algorithmic* win but also the largest effort and highest correctness risk — save it for last, and only after profiling confirms it's your bottleneck on real scenes.

Each report ends with its own system-local *Suggested Order of Attack* and a *Considered & Rejected* section. Start there when you pick up a system.
