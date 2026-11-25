# Interactions: plan

Draft 3 (2026-11-24): the owner's seven answers are in (section 14). Every other open point carries
the answer I recommend. Status and decisions: [PLANNING-STATE.md](PLANNING-STATE.md).
Evidence with file and line references: [FINDINGS.md](FINDINGS.md). Paths are relative to
`kentucky-castle/` unless they start with `pyrite64/`.

An **interaction** is a place or a thing in the world that the player, or an AI character, walks up
to and uses: talk to an NPC, read a sign, open a door, climb a wall, press a button, pick up a crate.
While the player stands in its range and faces it, the HUD's A button shows its verb (TALK, READ,
CLIMB, ...). Using it fires event-graph outputs, and conditions can decide whether it may be used.

## 1. What the owner asked for

1. Interaction points placed in the world, and also spawned at run time with prefabs (items to pick
   up).
2. Uses named in the brief: talk to NPCs (the dialog system comes later), open doors, climb walls
   and ladders (**replacing today's climb volumes**), pick up items.
3. The A button prompt shows the verb: TALK, READ, CLIMB, ...
4. Built on the event graph, with conditions: for example a button prefab whose output event checks
   conditions and feeds another object's input to toggle it.
5. AI characters can walk to interactions and use them: alarm buttons, doors.
6. Collision volumes tell an interaction that the player is in range; only then do conditions and
   events run, so points far away cost nothing.
7. A table of the interactions an AI can use and the ones it cannot, so AI logic works with the
   points that are relevant to it.
8. An **interaction snap point**: where the player or AI stands and which way it faces, so
   animations line up.
9. Prefabs spawned or destroyed at run time update the AI table: an enemy can pick up an item, throw
   it, and go and pick it up again.

## 2. What exists today (short)

- **No interaction code at all.** No door, button, pickup, carry, sign or NPC talk exists. A is used
  only to climb.
- **Climb volumes** are a bare trigger box (`ClimbVolume.prefab`) that the player polls every physics
  step; the wall is found by a ray along the player's own facing. The box has no facing and the
  engine's ray test is two-sided, which is why a ladder can be climbed from behind (FINDINGS B1).
- **The engine already has what the detection needs.** Collider enter and exit reach scripts once
  per entry (`onCollisionEnter/Exit`); script pins, `send()` by pin name and prefab scopes work;
  `Conditions::Inputs` asks wired conditions synchronously. ObjectRef fields are 0 inside prefabs,
  so nothing in this plan refers to another object through a field (FINDINGS C5).
- **AI.** Guards can walk to any position (`beginMoveTo`), but stop anywhere inside the pad
  arrival radius, and cannot turn to a set facing (a standing guard is kinematic and its
  `fixedUpdate` returns before any rotation, so even `ActFaceTarget` does not turn; FINDINGS V5).
  The alarm is one scene-wide flag; the "alarm button" is only a flag on a navigation pad, and no
  pads are placed anywhere. The pad graph cannot remove pads, so it is not the place for things that
  come and go.
- **HUD.** The Player owns the HUD model and sets `actionText` every frame. A new verb is a static
  string; no canvas change is needed.
- **Collision layers.** All 8 bits are named; bits 3 and 4 are unused.

## 3. The pieces

| Piece | What it is | Where |
|---|---|---|
| **Interaction** | a script on the object that is used: verb, type, who may use it, pins, condition gate | `src/user/scripts/interaction/Interaction.cpp`, data in `src/user/global/systems/InteractionTypes.h` (shared, `P64_DATA_TYPE`) |
| **Range** | a trigger collider on the same object, on the new layer `COLLISION_FLAG_INTERACTION` (bit 3) | any shape; part of every interaction prefab |
| **Snap point** | a child object with the **Interaction Snap** script: where the actor stands and which way it faces. Zero or more per interaction | `src/user/scripts/interaction/InteractionSnap.cpp` |
| **Interactions** | a game global system: the table of every interaction in the scene, each actor's nearby list, the player's focus, AI claims, AI queries | `src/user/global/systems/Interactions.{h,cpp}` |
| **Actors** | the player (detects by collision) and guards (find interactions through the table) | `Player.cpp`, `Enemy.cpp` |
| **Interactor Condition** | a condition about who is using it (the player, an AI, holding something) | `src/user/scripts/conditions/InteractorCondition.cpp` |
| Engine / editor (fork) | condition-input pins on any script; an arrow in the viewport for snap points | pyrite64, section 10 |

Everything is game code except two small editor changes. The engine knows nothing of a player or an
A button; this follows the rule the event graph and conditions set (the generic machinery is the
engine's, the concrete gameplay the game's).

## 4. The Interaction script

### 4.1 Arguments

| Argument | Values | Default | What it does |
|---|---|---|---|
| Verb | USE, TALK, READ, OPEN, CLOSE, CLIMB, PICK UP, PRESS, PULL, PUSH, ENTER, LOOK | USE | the A prompt |
| Alternate Verb | the same list | CLOSE | shown after the AlternateVerb input (a door that is open shows CLOSE) |
| Type | Event, Climb, Carry | Event | what using it does (section 6) |
| Usable By | bitmask: Player, AI | Player | who may use it at all |
| AI Kind | bitmask: Alarm, Door, Switch, Throwable, Item, Custom 1, Custom 2, Custom 3 | none | what AI lists search for (section 8) |
| Approach | Face Object, Snap Point, Object Facing | Face Object | where the actor lines up (section 5.1) |
| Move | Slide, Walk, Teleport, None | Slide | how it gets there (section 5.1) |
| Max Angle | degrees, 0 = any | 70 | the player only: how far its facing may be from the way to the object (Object Facing: from the object's own facing). Stops use from behind |
| Priority | 0 to 255 | 0 | when several are in range, the higher wins |
| When Blocked | Hide, Show and deny | Hide | the gate says no: hide the prompt, or show it and fire Denied on A |
| Hold | None, Seconds, Until Finish | None | how long the actor stays busy after use (dialog, a lever animation) |
| Hold Seconds | float | 1.0 | for Hold = Seconds |
| Actor Animation | None, Press, Pick Up, Talk | None | what the actor plays; each actor maps it to a clip it has (none exist yet) |
| Starts Enabled | bool | on | |

Verbs are a table in one header (`InteractionVerbs.h`); adding one is one line there and one in the
`P64::Enum` text. Verbs are written in capitals like the other prompts (SLAP, DASH); "Drop" while
climbing becomes DROP.

### 4.2 Pins

| Pin | Dir | What | Value |
|---|---|---|---|
| Enable / Disable / Toggle | in | the interaction is offered or not. Its object, model and collider stay as they are | |
| PrimaryVerb / AlternateVerb | in | which verb the prompt shows | |
| Finish | in | ends a Hold = Until Finish | |
| CanUse | in | **condition input**: wire condition Results here. Nothing wired means yes | |
| Used | out | an actor used it | the actor's id (also the instigator) |
| Denied | out | the player pressed A while the gate said no (When Blocked = Show and deny) | the actor |
| Finished | out | the hold ended (time, Finish, or right away with Hold = None) | the actor |
| Cancelled | out | the use was interrupted (hit, death, the item was taken) | the actor |
| Focused / Unfocused | out | the player's prompt now shows / stopped showing this one (a sign can glow) | the player |
| InRange / OutOfRange | out | the first actor came in range / the last one left | the actor |
| PickedUp / Released | out | Carry only. Released value: 0 set down, 1 thrown | the holder |

The script sits on the prefab's root, so its pins show on every instance node in a scene's Events
window with no exposing step.

### 4.3 The owner's button example

```
 Button (Interaction, PRESS)       Has Power (a condition)          Door (Door script)
 ┌──────────────┐                 ┌──────────────────┐             ┌──────────────┐
 │        Used ●─────────────────●│ Check       True ●─────────────● Toggle       │
 └──────────────┘                 │            False ●──────┐      └──────────────┘
                                  └──────────────────┘      │      ┌──────────────┐
                                                            └──────● Trigger  Buzz │
                                                                   └──────────────┘
```

Used fires a named event like any output, so it can feed a condition's Check, a Gate, a Toggle, a
Counter or another interaction's Enable. A condition wired into **CanUse** instead decides whether
the prompt appears at all (the door's own interaction can be blocked by a "has key" condition).

## 5. Range, focus and the A prompt (the player)

1. **Range.** Every interaction has a trigger collider that writes `COLLISION_FLAG_INTERACTION` and
   reads nothing. The player's collider reads that layer. The engine reports enter and exit to the
   **reading** side, so the player gets one call per interaction it comes near and leaves, and the
   triggers themselves do no work (FINDINGS V1, V2).
2. **Detection on the player's side** also gives an exit when an interaction is disabled or destroyed
   while the player stands in it; detection on the trigger's side would not (FINDINGS V1).
3. **Nearby list.** The player's `onCollisionEnter/Exit` pass ids to `Interactions::actorEnter/Exit`;
   ids that are not interactions are ignored. Up to 8 per actor.
4. **Focus.** Every frame the player can act (READY, walking or running), the system looks at the
   nearby list only: usable by the player, enabled, not held or busy, inside Max Angle. For those it
   asks the **CanUse** gate (with the player as the current actor, section 7). The winner is the
   highest Priority, then the smallest angle, then the nearest. A change fires Unfocused and Focused.
5. **Prompt.** The Player's HUD callback shows the focused verb. While climbing: DROP. While
   carrying: THROW.
6. **Cost.** Nothing runs for interactions out of range. In range, a gate is about 0.6 µs per
   condition (`docs/EventGraph/README.md`, "What it costs").

### 5.1 Lining up (Approach and Move)

**Approach** says where the actor ends up:

| Approach | The actor ends up ... | For |
|---|---|---|
| Face Object | where it stands, turned toward the interaction | signs, NPCs, items on the floor |
| Snap Point | on the nearest snap point, facing its arrow | buttons, levers, doors, alarm panels |
| Object Facing | where it stands, facing the interaction's own arrow | walls and ladders (the arrow points at the wall) |

**Move** says how it gets there (the owner's answer: all four are options):

| Move | The player | A guard, after it has walked within reach |
|---|---|---|
| Slide | slides and turns over a short time (tweakable, 0.15 s) with control taken; the camera's smoothing hides it, as it does for today's climb snaps | the same final slide |
| Walk | walks there on its own and turns at the end; if it is blocked or takes too long (tweakable, 2 s) it finishes with a slide | a slow, precise final approach, then turns |
| Teleport | is placed and turned in one frame | the same |
| None | neither moves nor turns | uses the interaction where it stopped |

## 6. What using does (Type)

On A with a focus: if the gate says no, Denied (or nothing). Otherwise the player enters a new
state, **INTERACT**: control and combat are off, it lines up, plays the Actor Animation if it has
one, and then:

- **Event.** Fires Used, holds as Hold says, fires Finished and returns to READY. A Hold = Until
  Finish that never gets Finish is ended after a tweakable maximum (30 s) with a warning, so a
  missing wire cannot lock the player. TALK uses this: the dialog system will listen to Used and
  fire Finish when the dialog closes.
- **Climb.** Fires Used and enters today's climb state on that interaction (section 6.1).
- **Carry.** Fires Used and PickedUp; the item goes to the hand (section 6.2).

A hit, death or a lost control (`SetControl 0`) during INTERACT fires Cancelled.

### 6.1 Climbing on interactions (replaces the climb volumes)

- A **Climb Wall** prefab: an Interaction with Type Climb, Verb CLIMB, Approach Object Facing, a box
  trigger, and its arrow pointing at the wall.
- The wall is found by a ray from the player along the **interaction's** facing, and the hit's
  normal must point back against that facing. With Max Angle this ends climbing from behind.
- While climbing, the player stays on the wall while the climb interaction is in its nearby list
  (instead of the per-step flag), and the sideways clamp uses the interaction's **oriented** box
  instead of a world AABB (rotated walls clamp correctly).
- The climb itself (steps, gait, ledge and climb over, ground exit, camera ClimbMode) stays in
  `Player.cpp` unchanged.
- The three placed climb volumes (two in `level_kokiri.prefab`, one in `level_junker_prime.prefab`)
  are replaced by Climb Wall instances facing their walls. Then `ClimbVolume.prefab`, the player's
  `onCollision` polling and `COLLISION_FLAG_CLIMB_TRIGGER` are removed (bit 5 becomes free).

### 6.2 Carrying and throwing

- A carried item follows the holder's hand bone every frame (player: the hand bone the hand glow
  uses; guards: `BoneRole::ATTACK_HAND`) and turns with the holder. Its body is kinematic with its
  collision masks off while held, and restored on release. Only held items do per-frame work.
- **Player:** A throws (at the locked target if there is one, else straight ahead), B sets it
  down in front. No slap, cast or dodge while carrying. Neither the player nor the minions have
  carry, pick-up or throw clips, so the item simply sits at the hand until clips exist.
- **Throw.** The item becomes a dynamic body with a lob velocity toward the target (the projectile
  lob arithmetic). A **Throwable** script on the item deals its damage to the first damageable of
  the other team it hits while in flight.
- **Back in the table.** Picked up: the item leaves the AI table (held). Thrown: still out (in
  flight). Once it has come to rest (slow for a short time) it is offered again, to the player and to
  AI alike, so a guard can fetch it and throw it again.

## 7. Conditions

- **CanUse** is an input pin that reads conditions (`P64_CONDITION_INPUTS`), the way And and Or read
  theirs: the gate is the And of every condition Result wired into it, and nothing wired means yes.
  Inside a prefab the wires are `prefab::` names, so each instance reads its own conditions.
- The gate is asked only when it matters: for the player, for interactions in range, every frame
  while in range; for AI, when a list looks for an interaction and again just before using it.
- **Who is asking.** While a gate is asked, `Interactions::currentActor()` returns the actor. The new
  **Interactor Condition** ("Interactor Is": the player, an AI, holding something, holding nothing)
  uses it, and game conditions written later can too (a key in the inventory, a quest flag).
- Conditions also work after use, as in the button example: Used is an ordinary event.

## 8. AI

### 8.1 The AI table

- Every enabled interaction is in the table from its `onEnable` to its `onDisable` or `destroy`
  (stored by id, never by pointer; registering twice is harmless; lookups skip objects about to be
  removed). Spawned prefabs are in it the frame they are created; destroyed ones leave it before
  they are freed.
- An entry is **AI-usable** when Usable By has AI. AI queries read only those, filtered by AI Kind,
  by what the guard may use (8.2), and by availability: enabled, not held, not busy, not claimed by
  another guard, and the gate says yes for this guard.
- A flat table of up to 64 entries rather than a tree: a linear pass over 64 is cheaper on the N64
  than keeping a tree, and queries run only when a list asks, not every frame.

### 8.2 What a guard may use

The Enemy gets **Uses Interactions**, a bitmask of the AI kinds, in a spare editor byte (`_pad0`,
`Enemy.cpp:283`, so the layout does not move). Default: none, so no existing guard changes.

### 8.3 New AI list commands

Appended to the compiler's table (`tools/ai_compiler.py`), documented in `docs/AIScripting.md`:

| Command | What it does |
|---|---|
| `InteractionNearest(kind)` | puts the nearest available interaction of that kind into the guard's interaction register (or clears it) |
| `IfInteractionFound(label)` | the register holds one that is still there and still available to this guard |
| `ActGotoInteraction(speed)` | claims it, walks to its snap point (or within reach for Face Object), then lines up as the interaction's Move says. Done when lined up; if the interaction goes away on the way, the claim is dropped and the register cleared |
| `ActUseInteraction()` | plays the use animation and uses it at the animation's event frame (or at once): Used fires, or the item is picked up. Done after the hold |
| `IfHolding(label)` | the guard holds an item |
| `ActThrowHeld()` | throws it at the target with the ranged attack's aim, released on the attack clip's event |
| `ActDropHeld()` | sets it down |
| `ReleaseInteraction()` | drops the claim |

- **Claims.** A guard claims an interaction while it walks to it and uses it, so two guards do not
  run for the same crate. Claims do not stop the player: if the player takes it first, the guard's
  action ends and its list looks again. A claim ends when the guard's action changes to something
  else, or the guard is disabled, hit (ARGH drops a held item) or killed.
- **Lining up.** A new final phase after `beginMoveTo`, per Move (5.1): a short kinematic slide, a
  slow precise approach, or a teleport, and a turn that runs **before** the kinematic early return
  in `fixedUpdate`. The same change makes `ActFaceTarget` turn (today it does not).
- **Animations.** New roles INTERACT and PICKUP (falling back to ATTACK, as ALARM does) and an event
  role for the press or grab frame. No body has such clips yet.

### 8.4 The alarm on interactions

- An **Alarm Panel** prefab: an Interaction (PRESS, Usable By AI, AI Kind Alarm, a snap point) and an
  **Alarm Panel** script that, on Used, turns the alarm on (`Perception::setAlarm` at the panel) and
  fires a `Raised` output for sirens, lights or reinforcements.
- `guard_alarm.ai` becomes `InteractionNearest(ALARM)` → `ActGotoInteraction(SPRINT)` →
  `ActUseInteraction()`. Without a panel in reach it does what it does today (raises the alarm where
  it stands), so levels without panels keep working. The alarm now goes on at the press, not when
  the animation starts.

### 8.5 Fetch and throw

A new list, `guard_thrower` (id 7): with the player in sight, find a Throwable, fetch it, throw it,
and look again; with none in reach, fight as `combat_melee`. Thrown items come back into the table
once they come to rest, which is the brief's "pick it up again".

## 9. Run-time rules

| Case | What happens |
|---|---|
| An interaction is spawned | in the table and detectable the frame it is created (`onEnable`) |
| It is destroyed or disabled while the player is in range | it leaves the table at once; the player's nearby list gets the collider exit; focus moves on |
| Its Disable input | it stays in the table but is not offered; its object keeps running |
| A guard holding an item dies | the item is released where the hand is and comes back once it rests |
| A guard with a claim is destroyed | its claims and held item are released in its `onDisable` |
| The level or scene unloads | the table is cleared in `onScenePostUnload`; nothing fires during teardown |
| A snap point | registers with its parent interaction's entry in its own `onEnable`; positions are kept relative to the parent, so they follow a moving object |

## 10. Engine and editor changes (pyrite64, fork only)

1. **Condition inputs on any script.** The editor accepts `P64_CONDITION_INPUTS` only in condition
   scripts (`codeParser.cpp:852-857`); the run time already works for any script (FINDINGS V3).
   Allow it everywhere, so CanUse is drawn as a condition input and the build checks it (the "input
   with no Result behind it" warning). Host tests in `tests/codeParserFork.cpp`.
2. **An arrow in the viewport.** A script cannot draw in the editor today (FINDINGS D2). A script that
   declares `P64_EDITOR_ARROW()` gets an arrow along its object's facing and a small foot circle,
   drawn by the Code component. Interaction Snap and Interaction declare it. Inside prefab instances
   it is drawn at the composed transform like other gizmos. No run-time change, no new component id.
3. Every change sits in new code or behind `// Fork:` markers. The game's CI builds with the released
   editor, so it passes only after an editor release that has these changes (as for the event graph).

Not planned: showing "Verb: TALK" on the Events window node (the window shows values for conditions
only). It would be useful and is listed as a follow-up.

## 11. Example prefabs, a debug scene, and the first steps toward scene transitions

### 11.1 Example prefabs

In `assets/prefabs/interaction/`, built from the box model until real art exists:

| Prefab | Contents |
|---|---|
| Interaction | the bare base: Interaction + box trigger |
| Sign | READ, Face Object, the player only; Used → a Log until dialog exists |
| Talk Point | TALK, a snap point in front; to put on an NPC; Hold Until Finish |
| Button | PRESS, a snap point, the player and AI, kind Switch |
| Door | a **Door** script (Open, Close, Toggle; Opened, Closed) and an interaction with OPEN / CLOSE, a snap point on each side, the player and AI, kind Door; wired inside the prefab so it toggles itself |
| Climb Wall | section 6.1 |
| Crate | Carry, PICK UP, the player and AI, kind Throwable; rigid body, solid collider, range trigger, Throwable |
| Alarm Panel | section 8.4 |

### 11.2 The debug scene (the owner's answer)

A new scene, `kentucky-castles-debug`, mirrors the main game scene (scene 9,
`kentucky-castles-start`): the same Skycube, Player and BoomCamera and a LevelDriver, from the same
prefabs, so the persistent core stays one thing. Its LevelDriver registers debug levels only:

- **`level_interaction_test`** (new, the default level): one of each example prefab, an alarm guard,
  a thrower guard, and a Spawner that drops crates.
- **`level_enemy_test`** (new): the enemy AI test of scene 3 (`TestEnemy`) rebuilt as a level
  prefab: its terrain, the visibility and encounter volumes with the minion spawner, the patrol spline
  (the guards find it by Path Id, because object references are 0 inside prefabs), the fireball
  spawner, and a spawn point. Scene 3 stays as it is.
- **The existing test levels** (Kokiri, Hyrule Field, Bob-omb Battlefield, the main scene's
  `level_tests` group) are registered here too. The main scene is not changed and still starts in
  Kokiri.

The game still boots into the main scene: the boot scene is a project setting, and the project file
is not changed for this.

### 11.3 Breadcrumbs toward scene transitions

Today a level transition never leaves its scene (`docs/LevelSystem.md`). The game will need to move
between scenes later, so this work lays the first pieces:

- **`LevelDriver::transitionToScene(scene, level, spawn)`**: fade out, `SceneManager::load`, and a
  small request that survives the scene change; the next scene's LevelDriver loads that level and
  spawn in place of its default, then fades in.
- **LevelTransition** gets an optional **Target Scene** (0: this scene), so a volume can lead into
  another scene.
- **Debug overlay:** "Warp: Load Scene" lists the main scene and the debug scene, next to the
  existing "Warp: Load Level".
- Not in this work: carrying the player's state (health, elements, a held item) into the next scene,
  and saving.

## 12. Tools, limits, cost

- **Debug overlay page "Interactions":** the table (id, verb, kind, enabled, held by, claimed by),
  the player's nearby list and focus. With the debug draw on: snap arrows, the focus, guards' claims.
- **Limits:** 64 interactions per scene, 8 nearby per actor, 4 snap points per interaction, one claim
  and one held item per guard. Overflow logs a warning and the newest is not added.
- **Subscriptions:** each bound input pin on each instance uses one of the 512; 64 interactions with
  three bound inputs each use 192.
- **Cost:** out of range, nothing. In range: the player's focus pass over at most 8 entries plus their
  gates. AI: a linear pass over the AI-usable entries when a list asks.

## 13. Phases

| Phase | Repo | What | Verified by |
|---|---|---|---|
| 1 | pyrite64 | condition inputs on any script; the viewport arrow | host tests; the arrow by eye in the editor |
| 2 | kentucky-castle | the debug scene, `level_enemy_test`, `transitionToScene`, Target Scene, "Warp: Load Scene" | in ares: warp main → debug → main, a volume into the other scene, the enemy test plays as in scene 3 |
| 3 | kentucky-castle | collision layer, Interactions system, Interaction, Interaction Snap, Interactor Condition, verbs, HUD prompt, the player's INTERACT state (Event type, Approach, Move, Hold), debug page; Sign, Talk Point, Button, Door; `level_interaction_test` | in ares, in the test level |
| 4 | kentucky-castle | Climb type, Climb Wall, the three placed volumes replaced, the old climb trigger removed | in ares: climb from the front, refused from behind, rotated wall clamps |
| 5 | kentucky-castle | Carry type, the player's pick up / throw / set down, Throwable, Crate | in ares |
| 6 | kentucky-castle | AI: Uses Interactions, the commands, lining up and facing, claims, carry and throw, roles, Alarm Panel, `guard_alarm`, `guard_thrower` | in ares: two guards and one crate, the alarm |
| 7 | kentucky-castle | the guide `docs/Interactions/README.md` (like `docs/EventGraph`), `AIScripting.md`, `LevelSystem.md` | a playtest with real input (needs the screen unlocked) |

Each phase is committed on `feature/interactions` once the owner approves the plan. The collision
layer names live in `project.p64proj` ("Layer 3" becomes "Interaction", "ClimbingTrigger" becomes
"Layer 5"); that hunk is committed on its own, without the owner's uncommitted change to the same
file.

## 14. The owner's answers (2026-11-24)

1. **Snap points:** child objects with the Interaction Snap script, drawn as an arrow (as
   recommended).
2. **Getting onto a snap point:** "lets make the slide and walking there options. so we should also
   have teleport and none as option as well". Move is an argument: Slide, Walk, Teleport, None
   (section 5.1).
3. **The player carrying:** A throws, B sets down, no attacks while carrying (as recommended).
4. **Test content:** "lets start a new scene that mirrors the main game scene that is for debug
   levels. eventually the game will need to transition between scenes so we can start breadcrumbing
   that. when we do this work we will want to have the enemy ai test in there as well" (section 11).
5. **The existing test levels:** registered in the debug scene too; the main scene unchanged.
6. **Scene transitions:** "Warp: Load Scene", Target Scene on level transitions, and the level and
   spawn handed to the next scene; no player state carried over yet (section 11.3).
7. **Scene 3 (TestEnemy):** kept as it is.

## 15. Not in this plan

- The dialog system (TALK fires Used and holds until Finish, ready for it).
- An inventory: collecting an item is Used wired to its Destroy plus whatever the game records.
- Climbing down from the top of a ladder; companions (Tank) using interactions; animation clips for
  press, pick up, carry and throw.
- The verb on the Events window node; per-room alarm state (the alarm stays one flag per scene).
- Bugs seen on the way and left alone: `EncounterNPC.cpp:204` tests object flags against a collision
  flag; `EnemySpawning.md:790` says bool is 4 bytes; `VisibilityVolume.cpp:50-51` says a disabled
  trigger reports exits.
