# Interactions: findings

Raw investigation notes (2026-11-24), condensed from four read-only explorers and checked where
marked **verified**. Paths are relative to `kentucky-castle/` unless they start with `pyrite64/`.

## A. AI, navigation, registries (explorer cgkap9-8)

### A1. AI list language and VM

- `ai/*.ai` → `tools/ai_compiler.py` → `build/ai/ai_opcodes.h`, `build/ai/ailists.h`. Rebuilt by
  `Makefile.custom:131-132` when an `.ai` file or the compiler changes.
- The `OPCODES` table (`ai_compiler.py:66-134`) is the single source; the opcode value is the row
  index, so new rows are **appended**. `build_env()` (807-818) makes every row a DSL function.
  Argument types: `ARG_WIDTH` (136-137) and `_encode_scalar` (440-474); a `label` must be last.
  Constants in `DSL_CONSTS` (168-174): `PATROL_STOP=1, COVER=2, ALARM=4`, ...
  Ops named `OP_ACT_*` count as "advancing" for the busy-loop warning.
- The checked-in `build/ai/ai_opcodes.h` is stale (55 ops against 57 rows).
- VM state `AI::Vm` (`src/user/objects/AiInterpreter.h:14-27`): one target register `padPreset`,
  a one-level return list. `stepVm()` `Enemy.cpp:2199-2393`; `BRANCH_IF` 2217; an unhandled
  opcode is silently skipped (2386).
- Actions: `EGuardAction` `Enemy.cpp:200-220`; `setAction` idempotent (2134); `enterAction`
  1306-1443; `updateAction` 1497-2130 sets `actionDone` (no success/failure distinction).
  `forceAction` (ARGH) resumes only a fixed list of actions (2148-2150), others fall back to STAND.
- Adding an action touches: enum, `enterAction`, `updateAction`, the `update()` locomotion switch
  (2697-2724), debug text (2862-2881), `forceAction` resume list, `fixedUpdate`'s kinematic lists
  (2749-2751, 2783, 2812-2813). (`AIScripting.md:568-570` documents this.)
- Event ops: `FireEvent` → `postTo(obj, AI_EVENTS[e], 0, obj.id)` (2371); `IfEvent` consumes a
  latched slot; at most 8 event names per guard (`ai_compiler.py:181`).

### A2. The alarm today

- One scene-wide boolean in Perception (`Perception.cpp:49-50`, `setAlarm` 266-271, which also
  emits a 100000-unit unoccluded noise at the source). Reset only at scene load/unload.
- The "alarm button" is only a pad flag `PAD_ALARM = 1<<2` (`WaypointGraph.h:25`), NavPad bitmask
  "PatrolStop,Cover,Alarm". No alarm object, script or prefab exists.
- `guard_alarm.ai` (list 4): patrol; on sight or noise `PadNearestSelf(ALARM)`, `ActGotoPad(SPRINT)`,
  then `ActStartAlarm(); AlarmOn()` in the same VM pass (the alarm turns on when the animation
  starts, not at a press frame). `ActStartAlarm` plays role ALARM (id 7, falls back to ATTACK; no
  body has an alarm clip).
- No NavPads are placed in any scene or prefab; no scene uses list 4. With no pad, GA_GOPOS finishes
  at once and the guard "presses" wherever it stands.

### A3. Navigation

- Layers: WaypointGraph (pads, BFS), Navigation (steering only, `MAX_AGENTS 32`), Enemy.cpp (path
  following, applies velocity and rotation in `fixedUpdate`).
- NavPad registers in `onReady` (`NavPad.cpp:46-62`). **No `unregisterPad`**; pads are cleared only
  at scene load/unload, so pads inside a level prefab outlive the level. Any `registerPad` makes the
  next query re-run an O(n²) finalize with raycasts. NavPads must be scene objects (ObjectRef fields
  resolve to 0 inside prefab templates, `NavPad.cpp:12-13`).
- `findPath` is BFS by hops with a `uint64_t` visited mask: `MAX_PADS 64`, `MAX_PATH 12`.
  `findNearestPad` has no line-of-sight test.
- **Walking to an arbitrary position works:** `beginMoveTo(obj, data, goal)` (`Enemy.cpp:710-763`)
  goes nearest pad → BFS → final straight leg, or straight line without a graph (today's behaviour).
  `Navigation::setTarget(obj, ObjectRef)` follows a moving object.
- **No precise arrival:** `tickMoveTo` (766-796) arrives within `PAD_ARRIVE_RADIUS`
  (`40 * GFX_WORLD_SCALE`). **No facing action.** Likely bug: `ActFaceTarget` never turns the guard,
  because a cleared nav target makes the body kinematic and `fixedUpdate` returns early (2777-2781)
  before the rotation code.

### A4. Registries and the object lifecycle

- `Scene::addObject` is deferred: a spawn from a global hook exists the same frame, from a script's
  `update` the next frame. `init`/`onEnable` run when it is created; `onReady` the frame after.
- `obj.remove()` sets `PENDING_REMOVE` at once; the delete happens at the end of the frame, where
  `~Object` calls `onDisable` (if enabled) then `destroy`. ObjectRef is a `uint32_t id`. Ids are
  16-bit and wrap after about 65k spawns. `~Scene` deletes every object (their `destroy` runs).
- Existing registries: Perception (16, raw pointers, onEnable/onDisable), Navigation (32),
  SplineFollow, Damage (16 per layer, ObjectRef), CombatEncounter (ids, unregister never looks the
  object up), EncounterController, SpawnDirector (ids, a reconcile pass checks 2 entries per update,
  `beginUnload` guard), LevelDriver spawn points (no dedup), Targeting (raw pointers re-submitted per
  frame: **can dangle, do not copy**), WaypointGraph (never unregisters).
- Pattern to copy: register in `onEnable`, unregister in `onDisable` and `destroy`, store ids,
  idempotent register, unregister-by-id that never dereferences, skip `isPendingRemove()` in queries,
  init/destroy from `GlobalSetup` `onScenePreLoad`/`onScenePostUnload`.

### A5. Traits, capabilities, animation

- Traits describe animation only; no capability concept. Enemy capabilities are `P64_DATA` fields
  (`Enemy.cpp:242-402`), u8 only, with a layout `static_assert` (409-415). Two spare editor bytes
  `_pad0`/`_pad1` at offsets 86/87 (283-284). Spawners override fields via `SpawnConfig` `OVR_*`.
- Animation roles: `tools/anim_roles.py` `ANIM_ROLES` (append-only), `ANIM_FALLBACKS`, `EVENT_ROLES`;
  bound per body with `DefineRole`/`DefineEventRole`. `RoleAnimDriver::play(role, speed, restart)`.
  `getBonePosition(obj, BoneRole, out)`; `ATTACK_HAND` bound on both bodies. Minion clips: Idle,
  Walk, Attack, HitReaction, Death; Big Minion: Idle, Walk, Attack.

### A6. Throwing and carrying today

- An enemy's ranged attack spawns a fresh projectile at the hand on the ATTACK clip's `DAMAGE_START`
  (`throwProjectile`, `Enemy.cpp:1157-1240`) and sends `ProjectileInit`/`ProjectileLob`. Nothing is
  picked up or held.
- No interaction, door, button, pickup or carry code or prefab exists. `HardAttach` copies position
  only (no bone, no rotation, no physics handling).

## B. Player, climbing, HUD prompt (explorer cgkap9-7)

### B1. The climb today (`src/user/scripts/Player.cpp`)

- The prefab is `assets/prefabs/traversal/ClimbVolume.prefab` (`.pf` is only the asset-table name):
  one Collider, Box, trigger, `maskRead 0`, `maskWrite 32` (`COLLISION_FLAG_CLIMB_TRIGGER`), no
  script. Placed in `level_kokiri.prefab` (one inline copy, one instance) and `level_junker_prime.prefab`
  (rotated about -13.6 degrees).
- Player collider: Sphere, `maskRead 163`, `maskWrite 70`; `onEnable` forces the climb read bit
  (1535-1542).
- Detection is per-physics-step polling: `onCollision` (3042-3054) sets `inClimbVolume` and the
  volume's world **AABB**; `fixedUpdate` clears it at the end (2934-2935). Only the last volume is
  kept; a rotated box gives a loose AABB.
- `probeClimbWall` (1299-1315): ray from the centre of mass along the flattened facing, any mesh
  collider, mask 0xFF, no normal check.
- Enter (1841-1852): `READY && buttons_pressed.a && inClimbVolume && probeClimbWall`. `ChangeState`
  enter CLIMB (772-822): combat off and buffers cleared; position = hit + normal * (0 + Wall
  Padding) (the Sphere makes `halfDepth` 0); yaw `atan2(-n.x, -n.z)`; kinematic, no gravity, object
  collision read bit off; gait seed; `beginState(STATE_CLIMB_ENTER)`; `fire(Out::CameraClimb, 1)`.
  Exit (679-697) restores all of it and fires `CameraClimb 0`.
- Climb loop (1958-2199): keep facing; climb-over finish via the `trajectory_Jnt` bone; left the
  volume → fall; A = drop or ground exit; steps driven by animation progress
  (`climbStepVector`/`climbStepDistance` 1578-1600); stick picks the direction; ledge check →
  climb over; sideways steps clamped to the volume AABB (2154-2168).
- Animations: `traits/retrostar.trait` clips 10-21, states 127-137, non-looping, no transitions.
  The POOT handler (1112-1120) never runs: the event is only on `Climb_Over_Sensual`.
- **Climbed from behind:** the box has no facing, the probe accepts any mesh hit, and the engine's
  ray-triangle test is double-sided and flips the normal toward the ray
  (`pyrite64/n64/engine/src/collision/raycast.cpp:543-546`).
- Generic parts: detection, availability test + verb (duplicated at 1350-1357 and 1842-1845),
  snap (784-803), control/physics lock and restore, entry animation, camera event, cancel.
  Climb-only parts: gait, step motion, wall-relative input, ledge and climb-over, ground exit,
  sideways clamp and falling off when outside the volume (needs the volume's bounds, ideally oriented).

### B2. HUD prompt

- `src/p64/UISceneTypes.h` (generated from `assets/ui/HUD.canvas`): `HUDData` has
  `const char* actionText{""}`, `bool actionButtonHeld`, `attackText{"SLAP"}`, `dashText{"DASH"}`.
  Canvas variable types (`pyrite64/src/project/canvas/canvas.h:17-26`): Float, Int, Bool,
  ColorRGBA, StringPtr, SpriteRef, FontRef. No enum type.
- `src/p64/uiBindings.cpp` (generated): A sprite `button_basic(_down)` tinted {14,0,160} at
  (272,32); label `rdpq_text_printf(..., 274, 43 + 8, data.actionText)` with no `tp_width`, so a
  long verb ("PICK UP") may run off the right edge (not checked on screen).
- The Player binds the HUD model (`UI::bindModel<UISceneType::HUD>`, 1544); `bindModel` refuses a
  second binder (`uiDriver.cpp:119-127`), so the verb must come through the Player's callback.
  `onUIModelUpdate` (1317-1358) sets `actionText` to "Climb", "Drop" or "".
- Other prompts: B = SLAP, R = DASH, C buttons = elements.
- A new verb needs no regeneration: assign a static string.

### B3. Input, states, movement, animation

- Buttons: L walk, B attack, C cast, D-pad debug element cycle, Z target, R dodge.
  **A is used only for climb enter (1842) and drop (2021).** No jump (ledge leaps are automatic).
- `EPlayerState` (112-126): NO_INPUT, READY, ATTACK, DASH, DASH_COOLDOWN_*, LEAP, CLIMB, CAST,
  FALLING, LANDING, DEATH. `ChangeState` (639-915) has exit and enter sections.
- No walk-to-point and no interpolated teleport: only instant `rb.setPosition` while kinematic,
  instant `setRotation` or the `RotateTowardsMovement` slerp (507-540). The camera's slower climb
  smoothing hides the snaps.
- `AnimationDriver<Traits>` (`src/user/objects/AnimationDriver.h`): `playAnimation`, `beginState`,
  events via `enableEvents`, `getBonePosition`/`getBoneForward`. The player uses it; enemies use
  `RoleAnimDriver`.
- Nothing carries or holds an object. Particles follow bones per frame (2506-2512 and others).
  Engine parent/child (`object.h:214-230`) carries enable and visibility only, not transforms.

### B4. Existing volumes

- Engine enter/exit tracker: `pyrite64/n64/engine/src/events/contactEvents.cpp` (`touch` 111,
  `endStep` 128, `report` 77) fires the Collider pins and calls `onCollisionEnter/Exit(obj, data,
  otherId)`; defining them sets `WANTS_CONTACTS` (`code.h:97`). Pairs follow self read mask vs
  other write mask.
- LevelTransition (enter only), VisibilityVolume (occupant count), StatusEffectVolume (8-slot
  occupant table) all read the player from the volume side; EncounterPlayer reads volumes from the
  player side and sends EncounterPlayerEnter/Exit to the volume. Player climb is the old polling
  pattern.
- Free collision bits: `COLLISION_FLAG_3` and `_4` in `Constants.h`; bit 5 is the climb trigger.
- `SimpleSpawn.cpp` uses A globally (debug toy).

### B5. Projectiles

- Projectiles are not physical and never stay in the world (removed on impact or expiry).
  `detonate` can spawn `zonePrefab`/`impactPrefab`. `ProximityBomb.cpp` is an unused stub.

## C. Engine: collision, bus, conditions, objects (explorer cgkap9-6)

E = `pyrite64/n64/engine`.

### C1. Collision

- Shapes: Sphere, Capsule, Box, Cone, Cylinder, Pyramid (`E/include/collision/types.h:11-19`) on
  `Comp::CollBody`; meshes are `Comp::CollMesh`. `setTrigger`, `setCollisionMask(read, write)`,
  8-bit masks; `readsCollider = (read & other.write) != 0`. A collider joins the collision scene in
  `onEnable`, leaves in `onDisable`.
- Kentucky Castle bits: 0 WORLD, 1 OBJECT, 2 STATUS_EFFECT, **3 and 4 unused**, 5 CLIMB, 6
  VISIBILITY, 7 ENCOUNTER. Player read 163 write 70; Minion read 1 write 6 (bits 1, 2).
- Enter/exit: `onCollisionEnter/Exit(obj, data, otherId)` and the Collider pins
  Enter/Exit/Occupied/Emptied, tracked only for objects with `WANTS_CONTACTS` (set by the build when
  the script has the functions or a collider output is bound). `contactEvents.cpp`: pair list keyed
  (self collider, other id), linear search; `report()` calls **every** script on the object.
  Called directly during the physics step (not queued); pin events land in drain 2.
- Only the side that **reads** gets callbacks. Triggers never search in the body phase (found by a
  non-trigger collider's AABB search); **trigger vs trigger never collides**; CharBody-only actors
  are invisible to triggers (KC actors use RigidBody + CollBody).
- No "what overlaps me" query. `raycast`, `sphereSweep`, `capsuleSweep` exist (collisionScene.h);
  rays skip triggers unless `interactTrigger`; sweeps always skip triggers.
- Cost: AABB rebuild only on transform change; narrow phase only on AABB overlap; a trigger whose
  masks touch mesh layers searches the mesh tree every step (avoid). Up to 5 physics steps a frame.
- **Deleting or disabling the occupant** gives an Exit at the next physics step with a dead id.
  **Disabling or deleting the trigger** drops its pairs with **no Exit** (`contactEvents.h:28`;
  `VisibilityVolume.cpp:50-51` claims otherwise and is wrong). Re-enabling gives fresh Enters.
  Enter and Exit can arrive in one frame. Two trigger colliders on one object call its script
  twice per entry.

### C2. Event bus

- `P64_EVENTS_IN/OUT`, `fire(obj, data, Out::X, value, instigator)`, `onInput(obj, data, In, ev)`;
  payload `{id, sender, instigator, value}`. `post`, `postTo(obj, ...)` for prefab/object scope,
  `listen`/`listenIn`, `send(obj|id, "Pin"_pin, value, sender)` (no binding needed; a send to an
  object spawned this frame is held, up to 32).
- Engine components declare pins in `COMPONENTS[]` (`E/include/events/pins.h:72-80`). Built-in
  objects are ordinary scripts in `E/scripts/*.cpp` (Spawner, Delay, ...).
- `prefab::` is per instance; `prefab_N::` walks N prefab roots up. `prefab::Spawned`, `Enabled`,
  `Disabled`, `Destroying`.
- Limits: 512 subscriptions (each bound input pin on each instance uses one), queue 128, 256 per
  drain, 16 hops, 64 condition providers.

### C3. Conditions from C++

- `P64::Conditions::evaluate(obj)` is synchronous, ignores the enabled flag, uses the first
  condition script on the object, false if none. `check`, `setEnabled`, `reset`.
- `Conditions::Inputs{obj, data, In::X}` walks the bindings of input pin X and evaluates every
  condition whose **Result** event is bound there (providers registered at init, only for conditions
  whose Result is bound, max 64, 8 per binding). A name nothing provides counts as one false input.
- The editor accepts `P64_CONDITION_INPUTS` only in condition scripts
  (`pyrite64/src/utils/codeParser.cpp:852-857`); the runtime does not check. A plain script reading
  an input pin wired to Results "should work" (inferred, untested).

### C4. Objects and prefabs at runtime

- `Scene::addObject(prefabIdx, pos, scale, rot, parentId)` returns the id at once, created next
  frame; `isObjectPending(id)`. `obj.remove(keepChildren=false)`: Destroyed and
  `prefab::Destroying` fire at once, freed at end of frame.
- `getObjectById` (128-slot table + hash), `iterChildren` scans every object, `getComponent<T>()`.
  **No "all objects with script X" iteration** (registries instead). No tag system.
- No transform hierarchy: children store world transforms (spawned children composed once). The
  engine **Constraint** component (ID 7) can copy pos/rot/scale or keep a relative offset
  (`E/src/scene/components/constraint.cpp`). Bones: `getBonePosition`/`getBoneForward`.

### C5. Script data

- Editor types: ints, float, bool (1 byte), `color_t`, `char[N]`, `fm_vec3_t`, `fm_quat_t`,
  `AssetRef<sprite_t|emitter_t>`, `PrefabRef`, `ObjectRef`. Enums via `P64::Enum`, `P64::Bitmask`.
- `obj.pos` (meters), `obj.rot` (`fm_quat_t`), `obj.scale`, `intoLocalSpace`/`outOfLocalSpace`.
- **`ObjectRef` is 0 inside prefabs and on spawned objects** (`objScript.md:286-300`). Find a
  snap child with `iterChildren` and a marker, or carry the snap in the interaction's own data.
- Data is copied in, not constructed; fields the build cannot parse get garbage; keep the struct
  gap-free. Component record max 1020 bytes.
- Component order at init: a script listed before its CollBody runs `init()` first; use `onReady`
  or `onEnable` order carefully.

### C6. Global systems

- `GlobalSetup.cpp` hooks: `onScenePreLoad` (init systems), `onScenePostUnload` (destroy),
  `onSceneUpdate` (runs before this frame's spawns, drain 1, physics and object updates),
  `onScenePreDraw` (UI update). Systems are namespaces with `init/destroy/update` and static arrays.
- System pins: `P64_GLOBAL_EVENTS_IN/OUT(System, ...)` → `global::System.Pin`.

### C7. Bugs spotted on the way (not changed)

- `EncounterNPC.cpp:204` tests object flags against a collision flag.
- `EnemySpawning.md:790` says bool is 4 bytes; it is 1.
- `build/ai/ai_opcodes.h` is stale.

## D. Editor, build, tests, UI (explorer cgkap9-9)

P = `pyrite64`.

### D1. Components, scripts, built-in scripts

- Editor components: `P/src/project/component/components.h:198-428` (fork block from 24: Particle
  Emitter 24, Spline 25; 26 is the runtime-only EventLinks; runtime table has 32 slots). A new
  component touches about 40 files (the Spline commit `45b8574`), needs a placeholder at 26, an
  engine release before the game's CI passes, and has the instance-preview gap
  (`viewport3D.cpp:275-279`).
- A game script is one `.cpp` under `src/user/**`; no editor or engine change.
- Engine-shipped scripts (`P/n64/engine/scripts/*.cpp`, fixed UUIDs `C0E0...01`-`...13`) are the
  built-in objects.
- The docs' rule: generic machinery in the engine, game concepts in the game (levels, saves, the
  Player Health Condition are game code; the condition framework and logic nodes are engine scripts).
  Fork-only; new code in new files; small `// Fork:` hooks in upstream files.

### D2. Editor visualisation

- Viewport passes call `funcDraw3D` / `funcDrawPost3D` of components (`viewport3D.cpp:556-738`);
  objects without one get a generic icon. Rotate mode draws +Y / -Z axes for selected scene objects.
- Line primitives in `P/src/utils/meshGen.h:18-79`; arrows are built from `addLine` (spline
  chevrons, camera marker).
- **A game script cannot draw in the viewport:** the Code component has no `funcDraw3D` /
  `funcDrawPost3D` and no attribute draws. NavPads show nothing in the editor.
- Prefab instances draw their children's components at the composed world transform and pick
  them per instance (`viewport3D.cpp:150-253`).

### D3. Script arguments

- Types and attributes as in C5. A verb must be `uint8_t` + `P64::Enum`, never an enum type
  (unparsed types are taken as s32). Build writes fields back to back with no padding check.
- Avoid member names the object uses (pos, rot, scale), or instance overrides are refused.
- `P/src/build/stringTable.h` exists but is unused (and its dedup can match a prefix).

### D4. Event build

- `pinsOf` feeds inspector and graph from the script's macros. `collect` binds condition triggers
  to Check (unless Auto Check is off), records Results and the inputs that read them. Project index
  checks: undeclared namespace (error), input nothing fires (warning), condition input with no
  Result behind it (warning), `send()` to a pin no script declares (warning), output nothing hears
  (info).
- A non-condition script can read conditions through `Conditions::Inputs` (runtime), but the editor
  forbids the macro (V3). **With no binding `total` is 0 and `all()` is false**: "no gate means
  pass" must be `total == 0 || all()`.
- A script on the prefab root has its pins on instance nodes in outer graphs with no expose step.
- The Events window shows script values only for conditions (`conditionSummary`,
  `eventGraph.cpp:219-251`).

### D5. Tests and tooling

- Host tests `./build/pyrite64_tests` (codeParser, codeParserFork, eventBus, eventEditor,
  conditions, ...; 116 cases last run). Test ROM `n64/tests/test_events` (15 tests, scenes 1-10).
  Shell checks `buildChecks.sh`, `graphChecks.sh`, `nestingChecks.sh`, `conditionChecks.sh`.
  Editor CLI `--cli --cmd build|graph|wire|...`.
- Kentucky Castle: `make p64` runs the editor CLI build; trait and AI-list compilers in
  `Makefile.custom`. No automated game tests: earlier features were verified on a scratch copy, then
  by a playtest in ares.
- **`KC/src/p64/*` and `KC/Makefile` are generated and git-ignored; the copies on disk are from
  2026-11-02.** The current generator's output is in `P/n64/tests/test_events/src/p64/`.

### D6. UI

- One model callback per canvas (`UI::bindModel`); the Player owns the HUD's. Events never write
  canvas variables. The prompt text must come from `Player::onUIModelUpdate`, pointing at a static
  string table (never into an object's `char[]`: deletes run before drawing).

## V. Verified by reading the code (2026-11-24)

- **V1.** Disabling or deleting a collider drops the pairs where it is the *self* side, with no Exit
  (`contactEvents.cpp` `forget`). When the *other* side disappears, its pair is not touched and
  `endStep` reports Exit. So detection on the **actor** side (the actor's collider reads an
  interaction layer) gets an Exit when an interaction is disabled or destroyed.
- **V2.** The broad phase accepts a pair when **either** side reads the other
  (`collisionScene.cpp:1145`); colliders on the same object never pair (1138). Callbacks then go only
  to the side that reads (`constraintMasksOverlap`). Triggers never search; a non-trigger finds them.
- **V3.** `Conditions::Inputs` reads any script's input bindings (`conditions.cpp:149-174`); only the
  editor parser refuses `P64_CONDITION_INPUTS` outside a condition script (`codeParser.cpp:852-857`).
- **V4.** The HUD's `actionText` is drawn `ALIGN_CENTER` at x 274 with no width
  (`uiBindings.cpp:170-176`); the Player sets "Climb" / "Drop" / "" (`Player.cpp:1337-1358`).
- **V5.** A standing guard (`GA_STAND`, `GA_FACE_TARGET`, idle spline) is made kinematic with
  collision masks **0, 0** and `fixedUpdate` returns before any rotation (`Enemy.cpp:2748-2781`):
  `ActFaceTarget` never turns, and a standing guard is invisible to triggers.
