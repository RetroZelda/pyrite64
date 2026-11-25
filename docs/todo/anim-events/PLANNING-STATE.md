# Animation events: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); it builds on the event graph
(`../event-graph/`).

- **Status:** PLANNED (2026-11-25). Nothing is built. Waiting for the owner's review of the plan.
- **Branches (planned):** `feature/anim-events` in `pyrite64` (from `stream/full-frontal`) and in
  `kentucky-castle` (from `main`). Not created yet. Commits carry no co-author or AI attribution
  lines.

## The owner's request, word for word (2026-11-25)

> in KC we have animation traits and the event graph. i want to get the animation events to fire
> from the event graph, but im not sure the best way to do that. I essentially want to have specific
> events from the object's trait available. can you brainstorm some implementation methods for me so
> we can determine how we should accomplish this

Six options were offered. Option 5 was "trait-fed pins on the Model (Animated) component". The
owner's answer:

> number 5 looks good although the trait system is KC only and not engine, so lets make a plan to do
> that option but to enable the traits to forward the events through the animnation component
> instead of bringing traits to the engine

Answers to the three follow-up questions (2026-11-25):

| Question | Answer |
|---|---|
| Which pins should a model get for its trait's events? | Both: `Leap.LAND` for that clip and `Any.LAND` for every clip with that event; the value is the clip index |
| What should the Enemy's RoleAnimDriver fire? | Every event, not only those with a role |
| Every `DefineEvent`, or only marked ones? | Every event |

The owner's review of the first plan (2026-11-25):

> in the plan, lets make the animEventPin part of hte trait generation.
> as a test you can use the sound effects and particle emitting for retrostars footsteps use this in
> his prefab

## The owner's decisions

1. Option 5: the events are output pins of the Model (Animated) component.
2. **The engine gets no trait concept.** The engine offers named animation-event pins on Model
   (Animated); Kentucky Castle's traits supply the names and fire them.
3. Pins per clip and event, and per event name (`Any.`); the value is the clip index.
4. Guards fire every trait event as a pin.
5. Every `DefineEvent` is a pin.
6. The pin constants come from the trait generation; the engine has no `animEventPin()`.
7. The test is RetroStar's footsteps: their sounds and dirt particles move into his prefab.

## Decisions made without the owner

Each is my recommendation. Change any of them on request.

1. The list of a model's events is a new section, `animationEvents`, of the existing event catalog
   (`events/*.json`). It is keyed by the model's project path (`assets/retrostar.glb`). Each event
   has a `name`, and may have a `doc` and a `value` text. It is an array, so the file order is the
   pin order in the editor.
2. The contract between game code and the engine is the pin **name**, never its index: game code
   fires by name hash, and the build resolves names to indices and writes the run-time table from
   the same list. So a stale or reordered list can never make the wrong pin fire.
3. Event pins sit at index 0x80 + position on the component, so Model (Animated)'s own outputs can
   still grow. At most 64 per model.
4. Pin names use letters, digits, `_` and dots, like canvas pins. `fireEvent` takes a `PinName`
   (the `pinHash()` of the full name). The trait compiler writes the constants as
   `AnimEvents::Pin::<Clip>::<Event>` and `Pin::Any::<Event>` (game code:
   `AnimationTraits::EventPin::Leap::LAND`), each initialised with the engine's `pinHash()` of the
   name, so Python never computes a hash and the two cannot drift.
5. `fireEvent` returns false for a name the model does not list, with no warning (a model without a
   list is normal).
6. The editor keeps the event lists in a cache on the project, re-read when a file in `events/`
   changes, because `pinsOf()` has no catalog and is called from many places.
7. After `swapModel()` the run-time lookup follows the current model; bindings only match the
   model set in the editor. That is documented, not handled.
8. Kentucky Castle: a trait names its model with `DefineModel("assets/<model>.glb")`. Without it the
   trait gets no pins (the compiler prints a note). Two traits for one model is an error.
9. Pin order per model: every `<Clip>.<Event>` in trait order, then `Any.<Event>` in order of first
   appearance. A clip named `Any` is an error.
10. The trait compiler writes `events/anim_events.json` (`--anim-events`). `make` runs it, and the
    file is committed, because the editor reads it while editing, before any build.
11. The pin hashes go into the trait headers as two arrays on `EventDescriptor`, taken from the
    `EventPin` constants, so both drivers get them, `RoleAnimDriver` through `AnimBody`.
12. Driver `init()` takes the object and keeps an `ObjectRef`, the same way `ObjectDelegate`
    resolves the object. The drivers' event work no longer depends on a bound callback; the
    callbacks keep exactly the events they get today.
13. Out of scope: input pins (play a clip, start a state), engine-side timing for models that no
    driver plays, role pins.
14. The footsteps are a real change that stays in the game, not a test on a copy. Two child objects
    of the Player (`StepFx Left`, `StepFx Right`), each with Follow Bone, a Particle Emitter and an
    Audio (2D); wired with `prefab::` names.
15. A new Kentucky Castle script, **Follow Bone**, puts its object at a bone of its parent's model
    each frame (bone by index: 41 and 49). It uses the parent rather than an object field so every
    instance follows its own body. The bone maths becomes a helper shared with
    `AnimationDriver::getBonePosition`.
16. `Dodge.TAKEOFF`'s dirt moves too (wired to both emitters), so the Player can drop its held
    emitters altogether. The gameplay of those events stays in `Player.cpp`.
17. Accepted differences: a component's Play cuts its own previous sound (only the same foot can,
    a run cycle later), and the dirt may be one frame behind the bone.

## Open questions

None block phase 1.

## Phase log

| Phase | Commit | What |
|---|---|---|
| plan | (not committed) | `PLAN.md`, this file |
| plan review | (not committed) | pin constants moved into the trait generation; the footstep test (PLAN 3.4, phase 6) |
