# Interactions: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); raw investigation notes are in
[FINDINGS.md](FINDINGS.md). It builds on the event graph (`../event-graph/`) and the conditions
(`../conditionals/`).

- **Status:** ALL DONE (2026-11-24). Every phase is built, tested in ares and committed; the guide is
  `kentucky-castle/docs/Interactions/README.md` with screenshots and videos. Follow-up done: climb and
  drop are one interaction (decisions 34 to 38). Pushed to origin `feature/interactions` in both repos
  at the owner's request (2026-11-24); waiting for the owner's review.
- **Working mode (owner, 2026-11-24):** "it looks good, you can start! you can commit each phase, and
  then I want a guide on how to use it step by step with images and video where necessary". Commit
  each phase on `feature/interactions` with no co-author or AI attribution lines. Open points take
  the recommended option and are logged under "Decisions while building".
- **Branches:** `feature/interactions` in `pyrite64` (from `stream/full-frontal` 2f2af3f) and in
  `kentucky-castle` (from `main` d697148). Pushed to origin when the owner asked (2026-11-24).
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
- [x] Phase 1: editor, condition inputs on any script, the snap arrow (pyrite64 b6cfc98)
- [x] Phase 2: debug scene, enemy test level, scene transitions (kentucky-castle 44c407c)
- [x] Phase 3: interaction core, the player's use, example prefabs, interaction test level (kentucky-castle 76dc38f)
- [x] Phase 4: climbing on interactions (kentucky-castle 9cff841)
- [x] Phase 5: carry and throw (kentucky-castle 744ee29)
- [x] Phase 6: AI use, alarm panels, thrower (kentucky-castle 2f83b87)
- [x] Phase 7: the guide with images and video (kentucky-castle 43104c5)
- [x] Follow-up: climb and drop on one Climb Wall, chosen by conditions (kentucky-castle 072c02b)
- [x] Push `feature/interactions` in both repos to origin (owner asked)

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

1. (Phase 2) The debug scene uses the main game's Player (scale 0.5). Scene 3's Player is at 0.25,
   the only scene that differs, so `level_enemy_test` plays at the game's scale, not scene 3's.
2. (Phase 2) "Warp: Load Scene" names its scenes with the `"name"_scene` literal in
   `src/user/global/constants/Scenes.h` (Main and Debug), not the generated `SCENE_NAMES`, which is
   positional and would break with gaps in scene ids.
3. (Phase 2) A scene transition to the current scene becomes a level transition; one to a scene id that
   does not load gives up after 60 frames and fades back in.
4. (Phase 2) Playtest tooling (session scratchpad, rebuilt as needed): `kcbuild.sh` syncs the repo
   into a scratch copy and builds it there (the real project is never built or opened); `pt.py` drives
   ares with XTEST keys on a private settings copy (VirtualPad1 mapped, the N64 Gamepad layer cleared,
   audio muted), sends keys only while ares has focus and the screen is unlocked, waits on game log
   lines and grabs screenshots. The overlay's cursor keeps its place across scenes.
5. (Phase 3) **Max Angle** is measured from the way to the object (Face Object, Snap Point), or from
   the object's own facing (Object Facing), not from the snap point's arrow: walking up to an NPC
   from the side shows TALK; a wall still refuses a player behind it.
6. (Phase 3) A snap point moves the player across, not up or down: the target keeps the player's own
   height and the floor decides it.
7. (Phase 3) Using an interaction fires Unfocused (the prompt goes away while it is in use) and
   Focused again afterwards if it is still the best one.
8. (Phase 3) The Door example slides (no transform hierarchy exists for a hinge); Open Offset is in
   world units along the door's own axes.
9. (Phase 3) Sign, Talk Point and Button each carry a Log (and the Talk Point a 2-second Delay that
   fires Finish, standing in for dialog), so the test level shows in the log what fires. The Talk
   Point uses Move = Walk, the others Slide.
10. (Phase 3) The test level has a spawn point in front of each example, for "Warp: Spawn (current)";
    automated input cannot steer reliably while the camera turns with the player.
11. (Phase 3) Layer 3 is named "Interaction" in `project.p64proj`; only that line was committed, the
    owner's own change to the file stays uncommitted.
12. (Phase 3) "Debug Text|Interactions" also shows the player's position and facing.
13. (Phase 4) The three placed climb volumes became Climb Walls on the faces found by probing the
    level geometry with rays (horizontal from each old box, then down on both sides for the floor):
    Kokiri's ladder is climbed facing +Z as before; Kokiri's "wall" is a step between low ground
    (y 24) and high ground (y 80) and Junker Prime's is a cliff (y 60 to 142), both climbed from the
    low side. The old boxes straddled the faces, which is what let the ladder be climbed from behind.
14. (Phase 4) An Object Facing interaction is offered only to an actor in front of its origin (more
    than 1 unit behind it does not count); a Climb Wall's origin sits on its face.
15. (Phase 4) Leaving the climb fires the Climb Wall's Finished; there is no Cancelled for a climb.
16. (Phase 4) Seen, not changed: a player that falls out of the level crashes the game (t3d matrix
    overflow in the camera after a long fall), as two badly placed test spawn points showed.
17. (Phase 5) The holder places a carried item every frame (in front of the chest for the player,
    tweakable), not a bone: there are no carry clips, and a crate at the swinging wrist looked wrong.
    Carried items keep their model on the root (children do not follow a moving object).
18. (Phase 5) While carrying, the player has no focus: A throws, B sets down. Placing a carried
    item into something (a pedestal) is a follow-up.
19. (Phase 5) A thrown item's damage comes from a Throwable script that sweeps its path against the
    damage registry like a projectile; the side is the thrower's (the player hits the bad team, a
    guard the player's). One hit per throw.
20. (Phase 5) Released (thrown or set down) items are out of the table while they move and come back
    after resting ("Rest Speed" 6, "Rest Time" 0.3 s). The throw's lob uses the scene's rigid-body
    gravity (196.2, tweakable "Throw Gravity"); the Crate's body has gravity scalar 1.
21. (Owner, during phase 5) "when you're done can you make videos showing each of the interactions,
    without debug text?" Recorded after phase 7, from a build with no debug text.
22. (Phase 6) The interaction register and the held item live in the guard's data, not in `AI::Vm`.
    The new roles INTERACT, PICKUP and THROW were added (falling back to ATTACK); no new event role:
    DAMAGE_START is the press, grab and release frame.
23. (Phase 6) A guard walks its route to the interaction (arriving within Pad Arrive Radius, 20),
    then walks the last bit at Walk Speed to within "Arrive" (1.5) or for "Approach Timeout" (2 s),
    then lines up as the interaction's Move says. For Face Object it stands "Reach" (7) from the object.
24. (Phase 6) `ActGotoInteraction` restarts when the register names another interaction than the
    one it went to (setAction alone would treat a finished go as done for the next one).
25. (Phase 6) `ActUseInteraction` does not check the distance; lists go there first. Documented.
26. (Phase 6) A guard that is hit drops its carried item and its claims (the plan said ARGH would).
27. (Phase 6) The Alarm Panel is usable by AI only; the player can be allowed per instance (Usable By).
    `guard_alarm` keeps the alarm pad way when no panel is in reach.
28. (Phase 6) `guard_thrower` waits up to 4 s for something to come to rest before it fights in melee.
29. (Phase 6) The Crate's Throwable deals 10 (a guard's slap deals 15); 1 was too little to see.
30. (Phase 6) Doors for AI need no list of their own: `InteractionNearest(KIND_DOOR)` and the same
    go-and-use commands; checked with a scratch list (a guard opened the AI-only door). The guide
    has a recipe.
31. (Phase 7) The guide's clips were recorded from ares (ffmpeg x11grab of the emulator window,
    cropped to the picture) with no debug text; each is a small GIF in the guide (8 fps, 256 px wide)
    and a full-size MP4 (640x480) next to it, about 5 MB in all. The editor screenshots come from a
    scratch-only scene in a copy of the project, looked at through scene cameras placed for it.
32. (Phase 7) The test level's staff door shows a lamp for a second on Denied (object Show / Hide and a
    Delay; the model's prim colour does not tint the textured box, so it is the crate texture), and
    `spawn_alarm` starts out of the alarm guard's sight so walking toward it shows the run to the panel.
33. (Phase 7) Smoke test of the final build: the enemy test level plays as before.

Follow-up, owner (2026-11-24), word for word: "lets make sure the ladder climb and drop are the same
interaction volume and use conditionals to determine if it should start to climb or if it should drop".
Then: "once this is done can you push to origin".

34. (Follow-up) A general mechanism, not a climb special case: a second condition input
    **AlternateWhen** on the Interaction script. All true = the alternate form (Alternate Verb on the
    prompt, **UsedAlternate** fired instead of Used); nothing wired = PrimaryVerb / AlternateVerb
    decide as before, so the Door is unchanged. Pins are appended (InPin 7, OutPin 10) so placed
    bindings keep their numbers.
35. (Follow-up) Verb **DROP** appended (12). The Climb Wall: Verb CLIMB, Alternate Verb DROP, and a
    child **IsClimbing** (Interactor Condition, Climbing) wired into AlternateWhen inside the prefab,
    so every Climb Wall has it and levels need no change.
36. (Follow-up) The form is settled in `begin()` and stored in the entry before the actor becomes the
    user. Asked after, "Climbing" is already true and the climb start logged as a DROP (seen in ares,
    fixed). `use()` fires from the stored form.
37. (Follow-up) The drop is not a new use: the climber is still the user, so the Player calls
    `useAlternate` (UsedAlternate) and then leaves the climb, which ends the use (Finished). A on the
    wall drops only while the wall is in its alternate form; a wall whose AlternateWhen says no keeps
    the player on it (stick and the top still work).
38. (Follow-up) Interactor Condition gains **Using it** (the asking interaction's user is this actor;
    `currentInteraction()` added next to `currentActor()`), **Climbing** and **Not climbing**.
    Appended to its enum so existing conditions keep their test.

## Phase log

| Phase | Commit | What |
|---|---|---|
| plan | pyrite64 b9689c1 | PLAN, FINDINGS, PLANNING-STATE |
| follow-up | kentucky-castle 072c02b | climb and drop as one interaction (AlternateWhen, UsedAlternate, DROP, Interactor Is Using it / Climbing / Not climbing, ClimbWall wiring), guide section with the Events-window image and a climb-and-drop clip; in ares: the test block and Kokiri's ladder show CLIMB then DROP, log `uses 349 (CLIMB)` then `uses 349 (DROP)` |
| 7 | kentucky-castle 43104c5 | the guide (step by step, reference, technical overview), 5 editor images, 12 clips (GIF + MP4) |
| 6 | kentucky-castle 2f83b87 | guard commands and actions, ActFaceTarget fix, roles, Alarm Panel, `guard_alarm`, `guard_thrower`, AI area; in ares: the alarm guard ran to the panel and pressed it (Alarm raised), the thrower threw both crates and fetched them again (every throw hit), a scratch list opened the AI-only door, a standing guard turned to face the player |
| 5 | kentucky-castle 744ee29 | carry: pick up, place, release, rest; the player's throw and set down; Throwable; Crate; in ares: PICK UP, carry, throw (lands, rests, offered again), set down with B, pick up again |
| 4 | kentucky-castle 9cff841 | Climb Wall, the climb through the A focus, oriented sideways clamp, the three level volumes converted, the climb layer removed; in ares: the test block (clamped at both edges, A drops), Kokiri's ladder and step, Junker Prime's cliff, each to the top |
| 3 | kentucky-castle 76dc38f | Interactions system, Interaction / Snap / Interactor Condition / Door scripts, the player's INTERACT state, HUD verb, five prefabs, `level_interaction_test`; in ares: PRESS (Slide) toggles the door through a scene event, READ, TALK (Walk, held until a Delay fires Finish), door from both sides, a staff-only door denied by its condition, Teleport and None (scratch patch), the exit pad to Main |
| 2 | kentucky-castle 44c407c | debug scene 10, `level_enemy_test`, `transitionToScene`, Target Scene, "Warp: Load Scene"; in ares: Main → Debug → Main by warp, and a scratch-only volume in the debug scene → Main at Hyrule Field |
| 1 | pyrite64 b6cfc98 | `P64_CONDITION_INPUTS` in any script; `P64_EDITOR_ARROW`; 7 host tests (129 cases pass); `conditionChecks.sh` 22/22, `graphChecks.sh` 7/7; arrow seen in the editor on a scratch copy of test_events |
