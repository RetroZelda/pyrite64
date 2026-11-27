# Companions: plan

Draft 2 (2026-11-26): the owner answered Q1-Q3 (section 9). Q3 widened the plan, because enemies now
fight companions too (section 5.5). Status and decisions are in [PLANNING-STATE.md](PLANNING-STATE.md).
The evidence, with file and line references, is in [FINDINGS.md](FINDINGS.md). Paths are relative to
`kentucky-castle/` unless they start with `pyrite64/`.

A **companion** is an AI character who travels with RetroStar and fights beside her. Outside combat it
follows her and keeps close, and it gets everywhere she gets: it jumps the gaps she jumps, climbs the
walls she climbs and drops where she drops. In combat it fights next to her, or it goes after enemies
that are away from her. This first pass has two attack styles: melee only, or ranged only with the
fireball. It is tested with two copies of RetroStar.

## 1. What the owner asked for

1. Companions fight alongside RetroStar.
2. Outside combat, a companion follows the player and stays near her. It jumps over the same things,
   climbs, falls, and so on.
3. In combat, a companion either fights near the player or fights enemies that are away from her.
4. Two companions to start with: one melee only, one ranged only (the fireball).
5. The test uses copies of RetroStar following the player around.
6. Added during planning: **companions use no animation blending**, so that each one has only one
   skeleton active (blending needs several).

## 2. What exists today (short)

- **The player moves in a few distinct ways**:
  - She walks or runs (camera-relative stick).
  - She leaps automatically when she runs at a ledge, and the dodge (R) hops.
  - She falls when the ground goes away, and lands with Land_Light or Land_Medium.
  - She climbs a Climb Wall interaction one step at a time and climbs over its top.
  - All of this is in `Player.cpp`, driven by the controller.
- **Climbing is an interaction** that one actor uses at a time (`Interactions::begin` takes it for one
  user). A companion that wants to climb while the player is on the wall cannot use it.
- **Enemies only ever fight the player.**
  - Perception has a single target: whoever called `setPrimaryTarget`.
  - A guard that senses anything retargets the player every frame.
- **Damage layers are the teams.**
  - The player can be hit on layer 0; her melee and her fireball hit layer 1.
  - Enemies can be hit on layer 1, and only while their encounter is active.
  - Anything that hits layer 1 can never hurt the player.
  - Any object can spawn `fireball.pf` and name itself as the owner.
- **There is no list of "enemies in combat"** to ask. The damage registry's layer 1 is exactly that
  list, but it has no way to walk through it yet.
- **Animation**:
  - The player's driver (`AnimationDriver<RetroStar>`) blends Idle with Locomotion, and Walk with Run.
    Its plain `playAnimation()` path never blends.
  - The engine's Model (Animated) builds a copy of the skeleton for **every clip** when it loads, kept
    for blending. That is 27 copies of 54 bones for RetroStar, about 140 KB per character, whether
    anything ever blends or not.
- **Copying the Player script is not an option.** It claims singletons that only the player may hold:
  the guards' perception target, the lock-on targeter, the HUD model, the camera pins and the encounter
  player. It also reads the controller.

## 3. The pieces

| Piece | What it is | Where |
|---|---|---|
| **Party** | a game global system: the roster of companions, the player's **trail**, the enemies in combat and who is fighting whom | `src/user/global/systems/Party.{h,cpp}` |
| **Companion** | the script on a companion: following, traversal, combat and single-clip animation | `src/user/scripts/companion/Companion.cpp` |
| **Player hooks** | a few calls in `Player.cpp` that tell the Party what the player is doing: on the ground, took off, landed, got on a wall, climbed over, let go | `src/user/scripts/Player.cpp` (state changes) |
| **Damage walk** | `Damage::forEachDamagable(layer, fn)`, to list the enemies that can be hit right now | `src/user/global/systems/Damage.{h,cpp}` |
| **Revive** | a Damagable input that restores health and registration | `src/user/scripts/utility/Damagable.cpp` |
| **Enemy targets** | guards and minions choose between the player and companions who are up | `src/user/scripts/Enemy.cpp`, `src/user/scripts/Minion.cpp`, `src/user/global/systems/CombatEncounter.cpp` |
| **Companion prefab** | a copy of `Player.prefab` without the player-only parts, with the Companion script instead of Player | `assets/prefabs/companions/Companion.prefab` |
| **Single skeleton** (engine, Q1) | a Model (Animated) option that skips the per-clip blend skeletons and refuses blends | `pyrite64/n64/engine/.../animModel.*` and the editor's component panel |

The engine learns nothing about companions or traits. The only engine piece is a generic switch on
Model (Animated).

### 3.1 Companion fields (editor)

| Field | Values | Default |
|---|---|---|
| Attack Style | Melee, Ranged | Melee |
| Combat Role | Guard, Hunt, Auto (Q2) | Auto |
| Follow Distance | meters from the player where it stops and waits | 2.5 m |
| Leash | the farthest it goes from the player in combat | 15 m |
| Catch Up Distance | farther than this from the player: it warps to her (section 4.7) | 40 m |

Speeds, the jump arc, the climb steps and the attack timings are tweakables under
`Gameplay|Companion|...` in the debug menu. They start at the player's values.

## 4. Following

### 4.1 The player's trail

The Party keeps a ring of the last 64 places the player has been (in physics space):

- **Ground points**, one every 1 m she walks.
- **Events**:
  - **Take Off**: the last place she stood before a leap, a dodge hop or walking off an edge.
  - **Land**: where she next stood.
  - **Climb On**: where she got on a wall, with the wall's point and normal and the Climb interaction's
    id.
  - **Climb Over**: where she stood on top.
  - **Let Go**: she dropped off the wall or stepped off at the bottom.

The player's state changes write the events, so the trail knows *why* she left the ground and does not
have to guess. A companion keeps a **cursor** into the trail: the next point it is heading for.

### 4.2 Where a companion waits

When the player stands still, each companion goes to its **slot**: behind her, one to the left and one
to the right, Follow Distance away. A slot that is inside a wall or over a drop falls back to the
nearest trail point. While she walks, a companion that is closer than Follow Distance does nothing; one
that is farther moves.

### 4.3 Walking: shortcuts and the trail

A companion does not snake along every trail point. Every few frames it looks for the newest trail
point it can walk to in a straight line, and moves its cursor there. A point counts only when all of
these hold:

- no Take Off or Climb On lies between the cursor and that point;
- a ray at waist height finds no wall in the way;
- the ground under the halfway point and under the point itself is within a step of the companion's
  feet.

When the player is on the same ground and in plain view, this goes straight to her. When she went
round a corner, over a gap or up a wall, it walks the trail up to the next event.

It walks at walk speed when it is near its slot and at run speed when it is farther. Companions steer
apart from each other, and a companion that stops making progress for a while counts as stuck
(section 4.7).

### 4.4 Jumps (leaps, dodge hops, walking off edges)

At a **Take Off**, the companion walks onto the spot, faces the paired **Land** and plays Leap. At the
clip's TAKEOFF event it launches on an arc that lands on the Land point:

- it starts with the player's leap height;
- it goes higher only if the landing is higher than that arc reaches;
- the arc uses the scene's gravity times the body's Gravity Scale.

In the air it steers a little toward the Land point. It lands exactly like the player does
(section 4.6). Every kind of take-off is replayed as a leap, so a dodge-hop over a gap or a walk off a
ledge are crossed the same way.

### 4.5 Climbing

The companion does not use the Climb interaction, so it can climb while the player is still on the
wall. It copies the player's climbing code instead:

1. At a **Climb On** it walks to the spot and finds the wall with a ray along the recorded normal.
2. It gets on with Climb_Enter. If the player is climbing right there, it moves one body width to the
   side, staying inside the interaction's climb box.
3. It then climbs one step at a time with the same clips, distances and limb order as the player:
   - It steps sideways toward the **Climb Over** point while it is more than a step away from it
     sideways.
   - Otherwise it steps up.
4. When the wall ends at head height it climbs over, with the same ledge probe and trajectory hand-off
   as the player.

How the player's climb ended decides what the companion does:

- **No Climb Over yet** (she is still on the wall): it climbs up behind her and waits one step below
  her.
- **She let go** (Let Go): it skips the climb and carries on along the trail from where she dropped.

### 4.6 Falling and landing

These copy the player's rules:

- Falling starts after a short time in the air, or at once over a clear drop.
- The landing snaps the feet to the floor it hit.
- Land_Light or Land_Medium play by the height of the fall, and a small drop plays no landing clip.
- The descent clamp that stops tunnelling through the floor is copied too.

### 4.7 Catching up

A companion warps to a free trail point a few meters behind the player when:

- it is farther than Catch Up Distance from her;
- or it is stuck for 3 seconds;
- or its cursor fell off the end of the ring (it was left too far behind).

A warp resets its cursor. It also warps to its slot when the player is placed by the level system: it
listens to `global::LevelDriver.PlayerPlaced` (warps and level loads).

### 4.8 When the player has no control

- During a dialog or a level transition, companions stop where they are and idle.
- When the player dies, they stop and idle.

## 5. Combat

### 5.1 When a companion fights

A companion fights when at least one enemy can be hit and stands within its Leash of the player. "Can
be hit" means the enemy is registered on damage layer 1, which is true exactly while its encounter is
active. So companions join in when the player starts an encounter, and stop when it ends. Companions do
not join encounters themselves: a GOOD-team NPC in an encounter would stop it from ever completing.

### 5.2 Choosing a target

The Party lists the enemies that can be hit and how many companions are on each one. A companion picks
the enemy with the best score:

- **Guard**: it prefers enemies close to the player and the one she has locked on to. It stays within
  half its Leash of her.
- **Hunt**: it prefers enemies that are **away** from her and not her lock-on target. It goes as far as
  its Leash.
- **Auto** (Q2): Guard when an enemy is within 6 m of the player and no other companion is guarding,
  Hunt otherwise.

Both roles score an enemy down when another companion already fights it, so two companions spread out
when there is a choice. A companion sticks with its target until:

- the target dies or leaves layer 1;
- the target goes past the Leash;
- or a clearly better target turns up (a margin, rechecked every half second).

When the player leaves, the Leash pulls the companion back into following.

### 5.3 Melee

The melee companion fights with the player's slap combo:

- It runs into Attack Range.
- It plays Attack_Slap_1, then Slap_2 and Slap_3 while the target is still in reach.
- It uses DashAttack to close the last few meters, like the player's dash-in.
- It pauses between combos (a tweakable cooldown).

Each hit works like the player's. The damage window comes from the same DAMAGE_START and DAMAGE_END
events, and the damage sphere follows the right wrist (plus both feet on Slap_3). It hits layer 1, with
the companion as the source and one tag per swing, so one swing hits an enemy once. The hit pause
then sends HitPauseEnd so the enemy's knockback plays.

### 5.4 Ranged

The ranged companion keeps a distance band from its target, 8 to 14 m by default:

- It backs off when the target is too close and moves in when it is too far.
- It sidesteps when its line of fire is blocked by a wall or by the player.

To shoot, it faces the target and plays the player's cast clip (the Attack_Slap_1 placeholder). At
DAMAGE_START it spawns `fireball.pf` aimed at the target's body and names itself as the owner, so the
fireball can only hit enemies. It waits a cooldown between shots, 1.5 s by default. Like the guards'
throws, it holds its shot while the particle pool has fewer than 4 free emitters.

### 5.5 Enemies fight companions (Q3: fully targeted)

**Companions can be hurt.**

- A companion has a Damagable on damage layer 0, the GOOD team, the same as the player: 120 HP and
  0.5 s of immunity after a hit.
- So every enemy swing, shot, bomb or thrown item that reaches it hurts it, as it would hurt the
  player. Those already hit layer 0.
- RetroStar has no hit-reaction clip, so a hit only pushes the companion back, as it does the player.

**At 0 HP a companion is knocked out.**

- It plays Death and lies there. The Party marks it as down, and enemies stop choosing it.
- It gets up again with full health when either of these happens:
  - no enemy in combat has been within its Leash of the player for 5 s;
  - the player is placed by the level system (a warp or a level load).
- A new, generic **Revive** input on Damagable brings back the health and the registration, and fires
  HealthChanged.

**Guards (`Enemy.cpp`) choose between the player and the companions.**

- **Noticing is unchanged.** Perception still watches only the player, so stealth works as before. A
  guard that has never sensed the player and has never been hit knows nothing about companions either.
- **Once engaged** (it sensed the player, was hit, or is hunting), the guard asks
  `Party::chooseTarget` each half second. That picks the nearest of the player and the companions that
  are up, within the guard's sight range, with three adjustments:
  - the player is favoured (she counts 2 m nearer);
  - the current target is kept unless another one is clearly nearer;
  - whoever hit the guard in the last 2 s is favoured, so a companion that attacks a guard draws it
    away from the player.
- **While its target is a companion, the guard's senses follow that companion.**
  - It "sees" the companion when the companion is within its sight range and a line-of-sight ray says
    so. The ray is checked a few times a second, from the shared ray budget.
  - The last known position tracks the companion while it is seen.
  - So `IfSeeTarget`, the distance checks, the chase, the melee and the throws work on whoever is the
    target. No AI list changes.
- **`IfTargetDead`** is also true when the target is a companion who is down. The guard then picks
  again (or returns to its own list when nobody is left).
- **When the player dies**, every guard stands down as today, companions or not.

**Minions (`Minion.cpp`: Minion, BigBubba)** take their targets from the encounter's rotation over the
GOOD team. Companions who are up join that rotation after the players (`getEnemyObjects`), and a
target who is down is dropped.

**Attack slots** are still given out per team, so having more targets does not make enemies attack more
often.

### 5.6 Noise and stealth

A companion's swings and fireballs make no noise for the guards' hearing, so a companion cannot give
the player away. Today a projectile's impact makes noise whenever it can hit layer 1
(`Projectile.cpp`). The build limits that to shots the player owns.

## 6. Animation: one clip, one skeleton

- **The companion never blends.**
  - It drives `AnimationDriver<RetroStar>` only through `playAnimation()`, which plays one clip on the
    main skeleton and clears any blend.
  - It never calls `beginState()`.
  - It picks clips itself:
    - Idle, Walk or Run by its speed, with a gap between the thresholds so it does not flicker;
    - Leap, Falling, Land_*, the climb clips, the slaps, DashAttack and Death.
  - Walk and Run play faster or slower with the speed, so the feet do not slide much.
- **Changes are cuts**, with no cross-fade. That is the price of one skeleton.
- **The skeleton copies (Q1).** Even a model that never blends allocates a blend skeleton per clip. The
  engine option **Blend Skeletons** on Model (Animated) (on by default), does two things
  when it is off:
  - it allocates only the main skeleton (about 140 KB saved per companion);
  - it ignores blend requests, logging a warning once.

  The Companion prefab turns it off.
- The trait's events still fire through the clip events: TAKEOFF and LAND on Leap, the DAMAGE window
  on the slaps, and the step pins (the StepFx children keep their dirt and sounds).

## 7. Test setup

- **Two prefabs**, `Companion Melee.prefab` and `Companion Ranged.prefab`. They are one Companion
  prefab with different Attack Style and Combat Role overrides: Melee + Guard and Ranged + Hunt.
- **Both go in the debug scene (scene 10)** next to the Player, outside the levels, so they follow her
  through every debug level. They warp to their slots on `PlayerPlaced`.
- To tell them apart, the ranged one is tinted or scaled by 0.9 (whatever the material allows without
  new art).
- **Walks to try**:
  - `level_interaction_test`: walk, run, leap the ledges, dodge-hop, climb the wall and go over the
    top, drop off it.
  - `level_enemy_test`: start an encounter; the melee one guards and the ranged one picks off the far
    enemies.
- **Debug**:
  - `Debug Text|Companions` shows each companion's state, cursor, target and role.
  - `Debug Draw|Companions` draws the trail (event points in colour) and each cursor.

## 8. Phases

Each phase is committed on `feature/companions` in both repos.

1. **Engine** (Q1: yes): the Blend Skeletons option on Model (Animated), with the editor panel and the
   docs. It needs an engine release before KC's CI can build.
2. **Party and trail**: the Party system, the Player hooks, the Damage walk, and the trail debug draw.
3. **Companion following**: walking with shortcuts, slots, single-clip animation, jumps, falling and
   landing, catch-up and PlayerPlaced; the prefab and the two test companions in scene 10. Playtest.
4. **Climbing**: Climb On / Over / Let Go replay. Playtest.
5. **Combat**: targets and roles, melee, ranged. Playtest in `level_enemy_test`.
6. **Enemies fight back**: companion health, knock-out and Revive; target choice in Enemy.cpp and the
   encounter rotation. Playtest.
7. **Guide**: `docs/Companions/README.md` with screenshots and clips, and the planning notes.

## 9. The owner's answers (2026-11-26)

- **Q1. Engine option that skips the per-clip blend skeletons:** "Yes, engine option".
- **Q2. Choosing between near and away:** "Role field, Auto default": Guard / Hunt / Auto.
- **Q3. Can enemies hurt companions:** "Fully targeted". Enemies hurt companions and choose them as
  targets (section 5.5).

## 10. Not in this pass

- Companions using interactions: talking, doors, carrying.
- Companions with elements or casts other than the fireball.
- Companion health on the HUD; revival by the player.
- Orders from the player (stay, attack that one).
- Companions in the main scene or saved in the game state.
