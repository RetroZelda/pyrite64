# Companions: findings

Raw investigation notes, 2026-11-26, behind [PLAN.md](PLAN.md). Paths are relative to
`kentucky-castle/src/user/` unless they say otherwise. Line numbers are as of KC main 125de2a and
pyrite64 stream/full-frontal 5ecc6e2.

## A. Player movement (scripts/Player.cpp)

- **A1 States.** `EPlayerState` (:128-143): NO_INPUT, READY, ATTACK, DASH, the two dash cooldowns,
  LEAP, CLIMB, CAST, FALLING, LANDING, INTERACT, DEATH. `ChangeState` (:669-978) runs the exit and
  enter work.
- **A2 Walking.** Walk and run speed come from the stick magnitude (`fixedUpdate` :2977-3031). The
  move follows the floor slope up to Slope Limit (35°), using the straight-down `floorCast`
  (:2918). Grounded means the feet are within Ledge Drop Threshold of the floor hit (`isGrounded`
  :1045).
- **A3 Auto-leap** (:2169-2212). This applies while grounded and pushing the stick. It skips the leap
  when there is a wall ahead (a ray from the centre of mass). Otherwise it probes the ground Ledge
  Probe Ahead in front; a drop bigger than Ledge Drop Threshold, or no ground at all, starts LEAP.
- **A4 Leap.**
  - Direction and the stick-magnitude scale are locked on entry (:812-833).
  - At the Leap TAKEOFF event (:1266-1278): an impulse `LEAP_SPEED * scale`, then vertical speed
    `jumpLaunchSpeed(LEAP_JUMP_HEIGHT)`.
  - `jumpLaunchSpeed` (:1034) uses the scene gravity **without** the body's Gravity Scalar; the
    Player's rigid body has gravityScalar 2.
  - At LAND: grounded → cooldown, otherwise FALLING. Dropping below the take-off height also starts
    FALLING (:2490-2503).
- **A5 Dodge** (R): an impulse plus a hop of `DASH_JUMP_HEIGHT`, at the Dodge TAKEOFF event
  (:1236-1250).
- **A6 Falling and landing** (:2214-2231, :3109-3153).
  - Falling starts on: clear air below the feet, no floor while descending, or Fall Min Air Time.
  - Landing: grounded or about to cross the floor this tick → snap the feet to the floor, kill the
    velocity, then READY (small drops) or LANDING (Land_Light / Land_Medium by height).
  - Anti-tunnel clamp on the descent: :3204-3216.
- **A7 Climbing.**
  - Start: the A press starts a Climb interaction (`beginInteract` :1400). Lining up ends in
    `probeClimbWall` along the interaction's facing; then `Interactions::use` and PLAYER_STATE_CLIMB
    (:1511-1533).
  - On the wall (:835-885): kinematic, no gravity, no object collision, faces `-normal`, sits half the
    collider depth plus Wall Padding off the wall, plays Climb_Enter.
  - Steps (:2325-2486): one clip per step, with the body lerped along the step by the clip's progress.
    Up and down steps are 12 gfx × world scale; side steps are 10.
  - Limb order: parity alternates hands on the same vertical direction; sideways steps use the foot
    opposite the hand.
  - Climbing over: an up step with no wall at head height (Ledge Check Height) probes the ledge,
    moves to its height and plays Climb_Over. When it finishes, the body follows `trajectory_Jnt` onto
    the ledge (:2253-2276).
  - Down at the ground: Climb_Exit. Sideways steps stay inside `Interactions::climbBox`.
- **A8 Player-only singletons in onEnable** (:1773-1797): `Targeting::setTargeter`,
  `Perception::setPrimaryTarget`, `UI::bindModel<HUD>` plus `ui::Hud.Show`, the Interactions actor and
  the controller. Death clears `setPrimaryTargetAlive` (:973).

## B. Animation and skeletons

- **B1 AnimationDriver** (`objects/AnimationDriver.h`).
  - `playAnimation()` (:51-65) leaves state mode and calls `setBlendAnim(-1)`, so one clip plays on
    the main skeleton.
  - `beginState()` (:79) can set a blend clip (`_enterState` :351) and cross-fades between states.
  - Non-state `update()` only fires the main clip's events (:131).
  - No statics: many instances work (Minion.cpp already runs many `AnimationDriver<Minion>`).
- **B2 retrostar.trait.**
  - 27 clips (0-26) and 54 bones (0-53).
  - Loops on Idle, Walk and Run.
  - Blends only in Idle (with Locomotion) and Locomotion (Walk with Run); the climb states have no
    blend clip.
  - No roles or body id, so `RoleAnimDriver` cannot use it.
- **B3 Engine Model (Animated)** (`pyrite64/n64/engine/src/scene/components/animModel.cpp`).
  - `initDelete` (:192-206) creates `skelMain` (3 matrix buffers), then
    `skelAnim[i] = t3d_skeleton_clone(&skelMain, false)` for **every** clip ("others for potential
    blending").
  - `setBlendAnim` (:56-75) attaches the blend clip to `skelAnim[idx]`.
  - `update` (:248-280) blends only when `animIdxBlend >= 0`, so at run time an unblended model updates
    one skeleton. The memory is spent anyway.
  - `swapModel` (:95-118) does the same allocation.
  - An event-graph input can call `setBlendAnim` (:332).
- **B4 Cost.**
  - `T3DBone` is 96 bytes (Mat4x3 + scale + quat + position + flags, aligned 16;
    `vendored/tiny3d/src/t3d/t3dskeleton.h:21-29`).
  - A clone with `useMatrices=false` allocates only the bones (`t3dskeleton.c:98-112`).
  - So: 54 × 96 = 5184 B per clip, and 27 clips ≈ 140 KB per RetroStar instance.

## C. Damage, projectiles, enemies

- **C1 Layers are teams** (`global/systems/Damage.h:22-23`).
  - Layer 0 = GOOD (the player, 200 HP); layer 1 = BAD (every enemy prefab).
  - There is no owner check: a broadcast hits every damagable on its layers (`Damage.cpp:329-353`).
  - Enemy damagables register on EncounterBegin (Enable On Encounter), so they can only be hit inside
    an active encounter (`utility/Damagable.cpp:184-201`).
  - 15 damagables per layer (`Damage.cpp:143`).
  - Death unregisters, so `GetDamagable(id, LAYER_1) == nullptr` means dead or not in combat.
- **C2 No iteration API** over the registry. Layer 1 is exactly the enemies that can be hit right now.
  `SpawnDirector::forEachTracked` covers spawned enemies only.
- **C3 Player melee** (Player.cpp:2523-2625): wrist sphere, plus both feet on the third hit. It uses
  `LAYER_1`, `source = player` and `userValue = currentAttackId`; `onDamageHit` ends the hit after the
  first contact, and a noise is emitted.
- **C4 Tagged hits.**
  - Enemies ignore repeats of the same (userValue, source) (`scripts/Enemy.cpp:3285-3290`).
  - They store the knockback until the attacker sends HitPauseEnd (:3320-3326).
  - An untagged hit every frame re-forces ARGH every frame (:3313-3317).
  - A tagged hit retargets the guard to the source (:3304-3308). The next frame that senses the player
    sets it back (:2931-2937).
- **C5 Projectiles** (`scripts/Projectile.cpp`).
  - `ProjectileInit`: sender = owner, value = `(hitLayers << 16) | seekTargetId`; a high half of 0
    means `LAYER_1` (:29-36, :420-438).
  - The owner is excluded from segment hits (:595-596).
  - Impact noise fires whenever the hit layers include layer 1 (:357-360).
  - `fireball.pf`: Shot, speed 500, 7 damage, AoE 25, no stored owner.
  - The player's spawn and aim: `castProjectileFor` (Player.cpp:374-456).
- **C6 Perception** has one primary target (`Perception.cpp:42`), with one sight raycast per frame.
  Making guards fight companions needs multi-target perception and `targetDead` changes (Enemy.cpp
  :2425-2430): a medium-sized job.
- **C7 Encounters.**
  - `tryComplete` needs zero NPCs on every team (`EncounterVolume.cpp:288`, `CombatEncounter.cpp:222`),
    so a GOOD-team companion in an encounter would stop it completing.
  - `EncounterNpcSetTarget` is handled only by Minion.cpp.
- **C8 Navigation agents** (`Navigation.cpp`) have no teams.
  - An enemy thrower treats any agent on its line as an ally in the way, and sidesteps or lobs
    (Enemy.cpp `allyOnPath` :945).
  - 32 agents at most, and SpawnDirector assumes 28 tracked.
  - So companions are **not** registered as Navigation agents; they steer apart from each other on
    their own.
- **C9 Particle pool.** Enemy throwers wait for 4 free emitters (Enemy.cpp:148, :2214); companion
  fireballs share the pool.

## D. Prefabs, scenes, scripts

- **D1 Scripts.**
  - The editor finds `src/user/**/*.cpp` and reads the 16 hex digits after the first `::Script::`
    (`pyrite64/src/project/assetManager.cpp:230-285`, :495-515).
  - `src/p64/scriptTable.cpp` and the `Makefile` are generated by the editor build. A new
    subdirectory under `src/user/` gets its own wildcard line.
  - Only `[[P64::Name]]` members show in the editor, first in `P64_DATA`, with no padding gaps.
  - `ObjectRef` fields are 0 in prefabs.
- **D2 Player.prefab** (`assets/prefabs/Player.prefab`, uuid 405436222). Components:
  - Model (Animated) with the Run.STEP_* and Dodge.TAKEOFF pins to `prefab::Player.*`;
  - the Player script: SetControl ← `global::LevelDriver.PlayerControl`, and the `scene::Player.Camera*`
    outputs;
  - DropShadow;
  - Damagable on layer 0, with HealthChanged → `player::HealthChanged`;
  - EncounterPlayer;
  - a box collider (half 7.5, offset y 7.5, read 139, write 70);
  - a rigid body (mass 100, gravity scalar 2);
  - StatusEffectHolder;
  - StepFx Left / Right children (FollowBone 41 / 49, a particle emitter and an audio source).

  The player-only parts are the SetControl binding, the camera outputs, HealthChanged, EncounterPlayer,
  write bit 6 (visibility and level triggers assume only the player writes it) and read bits 3 and 7.
- **D3 Spawning.** `Scene::addObject(prefabIdx, pos, scale, rot, parentId)` takes effect the next
  frame. Prefab indices come from `"prefabs/x.pf"_asset`.
- **D4 Debug scene 10** (`data/scenes/10/scene.json`).
  - It holds Skycube, Player, BoomCamera and LevelDriver.
  - The levels are LevelDefinition children: `level_interaction_test` (the default) and
    `level_enemy_test`, among others.
  - The levels are prefabs (`assets/prefabs/levels/*.prefab`) that LevelDriver adds and removes;
    objects placed in the scene itself persist.
  - `snapPlayerToSpawn` moves only the Targeting object and posts `global::LevelDriver.PlayerPlaced`
    (`global/systems/LevelDriver.cpp:122-143`).
- **D5 Collision layers** (`global/Constants.h:33-40`):
  - bit 0 world, 1 object, 2 status-effect trigger, 3 interaction;
  - bits 4 and 5 free;
  - 6 visibility trigger, 7 encounter trigger.
