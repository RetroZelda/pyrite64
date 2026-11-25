# Interactions: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); raw investigation notes are in
[FINDINGS.md](FINDINGS.md). It builds on the event graph (`../event-graph/`) and the conditions
(`../conditionals/`).

- **Status:** building (2026-11-24). Plan approved (PLAN.md draft 3). Phase log below.
- **Working mode (owner, 2026-11-24):** "it looks good, you can start! you can commit each phase, and
  then I want a guide on how to use it step by step with images and video where necessary". Commit
  each phase on `feature/interactions` with no co-author or AI attribution lines. Open points take
  the recommended option and are logged under "Decisions while building".
- **Branches:** `feature/interactions` in `pyrite64` (from `stream/full-frontal` 2f2af3f) and in
  `kentucky-castle` (from `main` d697148). Nothing is committed or pushed until the owner asks.
- **Status board:** planning was request `cgkap9`; the build is request `hhq3j6`
  (http://mini-debian:8765/r/hhq3j6).
- Never commit the owner's own change to `kentucky-castle/project.p64proj`.

## The owner's request, word for word (2026-11-24)

> in kentucky castle I want to create an interaction system. the interaction system should be a
> system that is placed in the world to enable the player to walk up to objects to interact. this
> includes things like talking to NPC characters to display dialog(dialog system will be coming
> soon), opening doors, climbing walls/ladders(will replace the existing method for this), etc. the
> interaction system should coincide with the A button in the ui, so an NPC should read "TALK", a
> sign should read "READ", a wall/ladder "CLIMB" etc. the interaction system should also work with
> thing that the player could pick up that are dynamically spawned - i.e. if we wanted the player to
> pick up an item by interacting.
>
> the interaction system should utilize the event graph and enable conditionals to function. so we
> should be able to have a button interaction prefab that has an ouput event so then that event can
> check conditionals and connect to a different input to control toggling.
>
> we also want to enable AI characters to be able to navigate to interactions in order to utilize
> them. this could be things like buttons to set off alarms, doors to open/close, etc.
>
> the interaction system ideally should use collision volumns to know when the player is in range
> before we have events and conditionals check if the player can interact. this is something that
> should help keep performance low impact so we only need to check conditions and events for
> interation points that the player is directly close to.
>
> we also need to keep a table of interactions that AI can interact with vs ones that it cant. so AI
> logic can determine and work with interaction points that are relevent to it. we should also have
> an interaction snap point, so we can make sure the player or AI will stand on the correct position
> and face the correct direction for lining up animations.
>
> lets also make sure that dynamically spawned objects via prefabs and dynamically update the AI
> usability tree. so if an enemy wants to pick up an item to throw, and then go and pick it up again
> it can.

## Steps

- [x] Create `feature/interactions` in both repos
- [x] Investigation by four read-only explorers (results in FINDINGS.md sections A to D):
  - [x] engine collision, event bus, conditions, prefab spawning, script data (cgkap9-6)
  - [x] player climb, HUD A-button prompt, input, existing volumes (cgkap9-7)
  - [x] AI list VM, navigation, registries, traits (cgkap9-8)
  - [x] editor gizmos, components vs scripts, build, tests, UI binding (cgkap9-9)
- [x] Verify the load-bearing findings by reading the code directly (FINDINGS section V)
- [x] Draft PLAN.md (draft 1)
- [x] Put the open questions to the owner: two rounds, all answered (below and PLAN.md section 14)
- [x] Fold the answers into PLAN.md (drafts 2 and 3)
- [x] Owner approves the plan (2026-11-24)
- [ ] Phase 1: editor, condition inputs on any script, the snap arrow (pyrite64)
- [ ] Phase 2: debug scene, enemy test level, scene transitions
- [ ] Phase 3: interaction core, the player's use, example prefabs, interaction test level
- [ ] Phase 4: climbing on interactions
- [ ] Phase 5: carry and throw
- [ ] Phase 6: AI use, alarm panels, thrower
- [ ] Phase 7: the guide with images and video, playtest

## The owner's answers, word for word (2026-11-24)

Round 1:

- Snap points: "Child objects + arrow (Recommended)"
- Getting onto a snap point: "lets make the slide and walking there options. so we should also have
  teleport and none as option as well"
- The player carrying: "Carry and throw (Recommended)"
- Test content: "lets start a new scene that mirrors the main game scene that is for debug levels.
  eventually the game will need to transition between scenes so we can start breadcrumbing that.
  when we do this work we will want to have the enemy ai test in there as well"

Round 2:

- The main scene's test levels: "Also register in debug (Recommended)"
- Scene transitions: "Warp + volumes + handoff (Recommended)"
- Scene 3 (TestEnemy): "Keep it (Recommended)"

How they were read: Move (Slide, Walk, Teleport, None) is an argument of every interaction, and
None replaces draft 1's Approach "Stay". The debug scene is a new scene that holds the interaction
test level, an enemy test level rebuilt from scene 3, and the main scene's test levels; the first
scene-transition pieces are part of the work (PLAN.md section 11).

## Choices in the plan that are not questions

Made from the evidence; each can change on request.

1. Game code in Kentucky Castle, plus two fork changes in the editor (condition inputs on any
   script, a viewport arrow). No new engine component.
2. Range is detected on the **actor's** side: interaction triggers write a new layer
   `COLLISION_FLAG_INTERACTION` (bit 3) and read nothing; the player reads it. Only this side gets an
   exit when an interaction is disabled or destroyed (FINDINGS V1).
3. Guards find interactions through the table, not by collision: a standing guard has its collision
   masks set to 0 (FINDINGS V5).
4. The AI "usability tree" is a flat table of up to 64 entries filtered by Usable By, AI Kind and
   the guard's own Uses Interactions mask.
5. The gate is an input pin, CanUse, that reads condition Results; nothing wired means yes. The actor
   asking is available to conditions through `Interactions::currentActor()`.
6. Verbs in capitals from a table in one header; "Drop" becomes DROP. No free-text verb, because the
   HUD keeps the string pointer past the frame's deletes.
7. Interactions carry Type (Event, Climb, Carry), Approach (Face Object, Snap Point, Object Facing,
   Stay), Hold (None, Seconds, Until Finish) and When Blocked (Hide, Show and deny).
8. Guards claim an interaction while going to it; claims never block the player.
9. `guard_alarm` moves to alarm panels and keeps today's behaviour when no panel is in reach.
10. The guards' lining-up fix also makes `ActFaceTarget` turn.
11. The planning files live in `pyrite64/docs/todo/interactions/` like the earlier features; the
    guide will go to `kentucky-castle/docs/Interactions/`.

## Decisions while building

(none yet)

## Phase log

| Phase | Commit | What |
|---|---|---|
