# UI Canvas — Phase 6: Draw-Efficiency Refactor (deferred)

> Status: **not started / deferred.** Phases 0–5 of the UI canvas feature expansion are done and
> build-verified (events, component scripts + focus, repeaters, timeline incl. per-instance element
> clips, typed clip/event enums). Phase 6 is a pure optimization of the generated
> `draw_<Name>_fn` and is the one remaining plan item.

## Why it's deferred

- **Correctness is defined by golden-image diffs on the emulator.** Each sub-step must produce a
  pixel-identical frame for static content; only the *command stream* gets cheaper. That visual
  comparison can't be done headlessly — it needs someone running frames on emu/hardware.
- **Modest payoff for typical HUDs.** A HUD with a handful of elements spends almost nothing in
  per-element RDP mode churn. The biggest win (`rspq_block` record-once) only applies to a
  **fully static** canvas, and most HUDs have at least one dynamic element (a count, a bar, a
  bound prop), which disqualifies the whole subtree.
- So it's high-regression-risk, emulator-gated, and low-return for the common case. Worth doing
  for a large/static UI or a measured per-frame hotspot — revisit then.

## The single invariant everything keys off

`isElementStatic(const CanvasElement&)` in [src/build/canvasBuilder.cpp](../src/build/canvasBuilder.cpp)
is the purity chokepoint. An element is dynamic (NOT static) if any of: a bound/op prop, `visible`
bound, a component `scriptUuid`, `focusable`, a repeater, an item-field/index binding, or it is
targeted by a timeline track (canvas or element clip), or it frame/tint/transform-animates.
**Every future dynamism source must extend this predicate** — a missed case = an element silently
frozen inside a baked block. Phase 6c (`rspq_block`) gates strictly on it.

## Sub-steps (ship & verify each independently, golden-image diff vs. the previous frame)

### 6a — Mode batching
Track a "current RDP mode" model `(blender, combiner, alphacompare)` during codegen and emit
`rdpq_mode_*` **only on change**, instead of the current per-element `rdpq_mode_begin/end` block.

- **Pixel-safe by construction** when done conservatively: only elide a mode set that is *provably
  redundant* (identical tuple, no boundary since the last emit). The RDP state at each draw call is
  unchanged; only redundant commands disappear.
- **Reset the tracked mode to "unknown" at every boundary** where the codegen can't prove the state:
  - after a **Text** element (`rdpq_text_*` sets its own combiner/blender internally),
  - around a **visible-guard** (`if (data.x) { … }`) — the element may not draw,
  - around a **repeater loop** — runs 0..N times.
  - Optionally hoist a constant mode set *outside* a repeater loop when every iteration shares it.
- Today's per-element emission is in `generateSpriteCode` / `generateTextCode` /
  `generateColorRectCode` / `generateTextureRectCode`.

### 6b — FILL / COPY mode selection
- Opaque solid `ColorRect` → `rdpq_set_mode_fill` (≈4× fill rate). Note: **FILL can't blend**, so
  only for fully-opaque rects (no animated alpha, no `RDPQ_BLENDER_MULTIPLY`).
- Opaque, unrotated, unscaled, compatible-format sprites → `rdpq_set_mode_copy` (≈4× blit rate).
- Everything else → STANDARD.
- **FrameIndex classifier rule:** a bound/animated `frameIndex` only varies `s0/t0` (static TMEM),
  so the element stays COPY/FILL-eligible, but its per-frame-changing params **exclude it from
  6c's record-once** (re-emit each frame). A literal `frameIndex` is fully static.

### 6c — `rspq_block` for fully-static canvases / subtrees
Record the draw commands once at bind/scene-enable, replay each frame with `rspq_block_run`.
- Gate strictly on `isElementStatic`.
- **C1 (dangling pointer):** call `AssetRef::get()` *outside* `rspq_block_begin` so the lazy-load
  side-effect isn't baked, and rely on Phase 1's non-stale / pinned UI sprites so the recorded
  pointer can't dangle. (UI sprites must not be cached across scenes — see Phase 1.)
- **C2 (nesting):** verify on hardware/emu that `rspq_block_begin` is legal inside
  `DrawLayer::use2D()` — if `use2D()` is itself an open recording, nesting asserts.
- Re-set RDP mode after `rspq_block_run` (the block leaves arbitrary mode state).

### 6d — Text + atlas
- Cache `rdpq_paragraph_build` layouts for fully-static text.
- Emit `rdpq_tex_multi_begin` / `rdpq_tex_reuse_sub` for sprites sharing a source texture
  (9-slice / atlas reuse).

## Verification model (when we do this)
- Headless build of **jam25** and **kentucky-castle** per sub-step (catches codegen/compile breaks).
- Run on emu and **visually diff a frame** against the pre-refactor output. A static canvas's
  pixels must be identical; only the command stream gets cheaper.
- Command-stream sanity: confirm the optimized path emits the same RDP modes *active* at each draw,
  just fewer redundant sets.

## Suggested order
6a (safest, pixel-provable) → 6b (clear wins, watch the FILL-can't-blend rule) → 6c (biggest win
but the dangling/nesting hazards + needs the most emu validation) → 6d (text/atlas).
