# Companions: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); the investigation notes are in
[FINDINGS.md](FINDINGS.md).

- **Status (2026-11-27):** BUILT and playtested in ares, every phase, including the owner's
  follow-up round (phase 8). The guide is `kentucky-castle/docs/Companions/README.md`, with screenshots
  and clips.
  - pyrite64: phase 1 (Blend Skeletons) and phase 8's engine changes (attachment tracking, fixedUpdate
    timing, this commit) are on `stream/full-frontal`. They were pushed on 2026-11-27, when the owner
    asked to push KC; the engine goes first.
  - kentucky-castle: committed on `feature/companions` and fast-forwarded onto `main` on 2026-11-27. It
    was pushed once this engine release was out.
  - tiny3d (decision 33): the priming fix is in RetroZelda/tiny3d `full-frontal` 95bc862, and pyrite64
    `stream/full-frontal` 65ee072 points at it (pushed 2026-11-27, at the owner's request). In the
    tiny3d fork, as in pyrite64's, the working branch should be the default and `main` only the upstream
    bridge. On 2026-11-27 the owner set the default branch to `full-frontal`. At their request, `main`
    was moved to 2f509c4, the revision upstream pyrite64 pins (TINY3D-ATTACH.md section 8).
- **Working mode:** the owner's first message asked to "get a system in place", so the build follows
  the answers. Open points take the recommended option and are logged below under "Decisions while
  building".
- **Status board:** request `987ua3` (http://mini-debian:8765/r/987ua3).
- **Branches:** `feature/companions` in both repos, cut when the build starts. Commit per phase, with
  no co-author or AI attribution lines.
- Never commit the owner's own change to `kentucky-castle/project.p64proj` if one is there.

## The owner's request, word for word (2026-11-26)

> I want to have the ability to have companions fight alongside retrostar. this will involve having a
> companion ai behavior that will follow and stay near the player when walking around, jumping over
> the same things that the player jumps over, climbing, falling, etc. THen when in combat, companions
> should either fight near the player, or fight enemies that are away from the player. so lets get a
> system in place that will do this. we can test by having copies of retrostar follow the player
> around. companions will generally only do melee or ranged attacks, so lets start by having 2
> companions: 1 that only does melee and 1 that only does ranged(using hte fireball)

Added while planning:

> also, companions should not use any animation blending. this is important so they only have 1
> skeleton active(instead of multiple which is required by the blends)

The owner's follow-up after the first SC64 test (2026-11-26):

> the companions seems to snap after combat. also in combat it seems that they will walk backwards.
> they should generally always face the direction they move. also it seems that there is poor
> performance with both - lets make sure we can capture the AI perforamance under CPU. companions also
> seem to stay really close all the time, we should allow them a bigger buffer. also it seems like they
> will get stuck if you walk close to corners/ledges. lets try to get them to attempt to navigate around
> obstacles. it also seems like they wont want to go the full range that some enemies will want to go
> to attack so ranged enemies can attack while they dont see them. also, can we make an option in the
> debug menu to spawn companions as well? we should be able to make retrostar companion spawn, but
> hypothetically any enemy AI can become a companion so lets try making it able to spawn and despawn
> companions and to include the minion as a potential companion that we can spawn(because we will
> eventually have the mind control element working)

## Decisions in the plan

1. Companions are a new script, not the Player script with AI input: Player claims singletons only
   the player may hold (FINDINGS A8).
2. Following replays the player's trail. Events come from her own state changes (take off, land,
   climb on, climb over, let go), so jumps and climbs are copied rather than guessed.
3. Every take-off is replayed as a leap whose arc is solved to land on the recorded landing point.
4. Companions climb on their own, copying the player's climb code, not through the Climb interaction
   (one user at a time), so they can climb while the player is on the wall.
5. A companion warps to the player when farther than Catch Up Distance, stuck for 3 s, or left off
   the end of the trail, and to its slot on `global::LevelDriver.PlayerPlaced`.
6. Companions fight the enemies on damage layer 1, so only during an active encounter, and they never
   join encounters (FINDINGS C7).
7. Companions are not Navigation agents (FINDINGS C8). Their collider reads the world only and writes
   no object bit, so they never block the player in a corridor.
8. Companion attacks make no noise for the guards.
9. Animation goes through `playAnimation()` only: one clip at a time and cuts between clips. Never
   `beginState()`.
10. The test companions live in debug scene 10 beside the Player, as Melee + Guard and Ranged + Hunt.

11. (Q1) The engine gets a generic Blend Skeletons option on Model (Animated); the Companion prefab
    turns it off.
12. (Q2) A Combat Role field, Guard / Hunt / Auto, with Auto as the default.
13. (Q3) Fully targeted. Companions have 120 HP on damage layer 0 and are knocked out at 0, getting up
    when the fight is over. Guards pick between the player and the companions who are up once they
    are engaged; noticing still watches only the player. Minions get companions through the
    encounter's rotation.

## The owner's answers (2026-11-26)

- Q1 Skeletons: "Yes, engine option (Recommended)"
- Q2 Roles: "Role field, Auto default (Recommended)"
- Q3 Enemies: "Fully targeted"

## Decisions while building

14. **Leash default 35 m, not 15** (the field and the prefab). The enemy test's fireball thrower keeps
    33 m or more from the player and hits her from there; with 15 m no companion ever engaged it. A
    Guard uses half the Leash, and only fights enemies within that of her; otherwise it stays by her.
15. **Following never walks off a ledge.** A ledge probe (the player's auto-leap probe) stops a
    following companion at any edge, so going down is always one of her take-offs, replayed as a leap.
    While she is in the air or on a wall, a companion whose goal is her or its slot waits. Held at an
    edge counts toward Stuck Time, ending in a warp. Without the stop, companions chased her off the
    block and fell instead of leaping.
16. **Take-Off Reach (2.5 m).** A companion climbs over a body width beside her, so it meets the edge
    beside her take-off point, not on it. Stopped at an edge within Take-Off Reach of her take-off, it
    leaps from there. Before this it was stuck at the edge until it warped.
17. **The leap is steered every physics step:** horizontal speed = remaining distance ÷ time left in
    the air, from the current height and vertical speed. With one launch impulse, the floor's friction
    in the take-off step ate most or all of the horizontal speed. Leaps landed 18 units short, or hopped
    in place on the block. The player's own leap sets its speed every step for the same reason. Landing
    is now within 2 units (logged: `landed X from her landing`).
18. **The trail is reset when the level system places her** (`LevelDriver::snapPlayerToSpawn` calls
    `Party::leaderPlaced()`), and when her position jumps more than Warp Jump (25 m) in a frame.
    Companions warp to their slots on the next frame she has control. No event binding is needed in
    the prefab.
19. **Guards:** target choice every 0.5 s; a hit by the player or a companion makes the hitter the
    favoured attacker for 2 s; a companion target is checked with a sight ray every 0.25 s from
    `Perception::takeRayBudget`. With no companions in the scene, `chooseTarget` always answers the
    player, so guard behaviour is unchanged. Log line `now fights X (a companion | the player)`.
20. **Projectile impact noise only for the player's own shots** (`ownerObject == primary target`).
    Before, any shot aimed at layer 1 made noise.
21. **The test companions are identical copies of RetroStar**, not tinted or scaled; the debug text
    tells them apart.
22. **Collider:** reads the World layer only (1) and writes only the status-effect bit (4).
23. **Debug:** `Debug Text|Companions` (the Party block plus one line per companion) and
    `Debug Draw|Companions` (the trail by mark, each companion's goal line). The log has one line per
    special trail point (`[Party] trail N: take-off at ...`) and per leap, climb, fight start and end,
    knock-out and getting up.

### The follow-up round (2026-11-27)

24. **Warps happen out of view.** A warp waits until the companion is off screen (the camera's view
    matrix) and lands it on her trail point that is out of view and within Warp Near (15 m). It warps
    in view only after Warp Wait (6 s), at three times Catch Up Distance, or when the level system
    places her. In view, it runs back. This removes the snap after a fight.
25. **Facing follows movement.** A companion turns to its target only while standing, to swing or
    throw. The ranged kite turns and runs to the middle of its band, with hysteresis, instead of
    backing off.
26. **The follow buffer:** Follow Distance 4.5 m (was 2.5). Settled at its slot, it stays until the slot
    is Slack (3 m) away. While she moves it matches her speed plus what closes the gap, so it does not
    stop and start.
27. **Steering:** a waist ray and, while following, a ledge probe every Steer Interval. It takes the
    nearest clear angle at 40°, 80° or 120°, holding the side it took. Stuck for 1 s, it walks her trail
    point by point for 4 s (strict mode) before warping. Next to her take-off, an edge means "leap from
    here".
28. **Aggressors:** a guard that sees and attacks a party member is noted for 3 s
    (`Party::noteAggressor`). Hunt and Auto companions go after aggressors up to 80 m from her, beyond
    the Leash, and Hunt prefers them. Guard-role companions do not, so they stay by her.
29. **Any body:** the brain plays animation roles (RoleAnimDriver, one clip, no blend) and reads its
    capabilities (climbing, combo length, a dash) from the body's roles. The Body field picks the trait
    registry entry. RetroStar got a body (`DefineBodyId(2)`) and nineteen new roles (append-only ids
    11-29, events 5-8). The Player still plays her clips directly. A body without climb roles waits at
    the wall and warps out of view.
30. **The Minion companion** (`CompanionMinion.prefab`) is the MinionAgent prefab with the Companion
    script. It has no Targetable and no EncounterNPC, a Damagable on layer 0 at 80 health, and the
    status-effect team GOOD. It is not tinted; the debug text marks the body (`R` or `m`).
31. **Debug menu page "Companions":** spawn RetroStar or Minion, Melee or Ranged; despawn the last
    spawned or all. A spawn is the prefab plus `Party::presetCompanion` (style and role) read by the
    script's init.
32. **CPU:**
    - A "Companion AI" row on CPU → UserGlobals: plain counters, published once a frame, because the
      string-keyed map is too slow to touch per call.
    - The engine's component and script Update rows now include `fixedUpdate` time.
    - A shared budget of 12 world raycasts per frame, and the floor probe once a frame (4 Hz standing).
    - The enemy list is built once a frame, and turning is skipped when it already faces the way.
    - AnimStagger 2 / 3 / 4 on both prefabs; never 0, because the brain waits for clip events.
    - Result per companion: settled 607 → 207 µs, running 4-6.7 ms → 586 µs.
33. **Clip changes cost 20 ms each, so the engine now tracks attachment.** `t3d_anim_attach` primes
    every channel from ROM, about 10 ms for a RetroStar clip, and each clip change attached twice
    (`setMainAnim`, then the driver). `AnimModel` now records which skeleton each clip is attached to and
    only rewinds a clip already there. The game's drivers and the Dialog talk clips no longer attach
    again. The first attach of a clip cost about 10 ms more, because the fork's priming loop recounted
    every channel after each keyframe it read (O(keyframes × channels), RetroZelda/tiny3d 63bafbb).
    **Fixed on 2026-11-27** in tiny3d `full-frontal` 95bc862: each channel is counted once as it is
    filled. The bone values are byte-identical, and an attach now takes 1.4-2.2 ms in ares. pyrite64
    `stream/full-frontal` 65ee072 bumps the submodule. Plan, effects and outcome:
    [TINY3D-ATTACH.md](TINY3D-ATTACH.md).
34. **Leaps:** the solve uses scene gravity times the body's Gravity Scale. At launch, the body is lifted
    0.05 m and its vertical speed held for 0.12 s, so an AnimStagger-delayed TAKEOFF on the floor cannot
    cancel it. A launch that never leaves is retried once.

## Phase log

1. **Engine** (pyrite64 5ede302): the Blend Skeletons option on Model (Animated), its editor checkbox,
   the manual and the changelog. The engine and the editor build; at run time the companions never
   blend and no blend warning appears.
2. **Party and trail:** `Party.{h,cpp}`, the Player hooks, `Damage::listDamagables`,
   `LevelDriver → Party::leaderPlaced`, the trail debug draw.
3. **Following:** the Companion script and prefab, and the two test companions in scene 10.
   Playtested: slots, walk and run, warps on level load, leaps off the climb block (4 of 4 runs land
   within 2 units).
4. **Climbing:** playtested on `level_interaction_test`'s wall. They climb beside her, go over the top,
   and skip the climb when she let go.
5. **Combat:** targets and roles, melee combo with hit pause, fireball with lane and pool checks.
   Playtested on `level_enemy_test`, with a scratch-only spawner setup giving encounterless, alerted
   minions. Both companions kill enemies.
6. **Enemies fight back:** companion health, knock-out and Revive; guard target choice; the
   encounter's rotation; Minion dropping a knocked-out companion. Playtested: guards switch to
   companions and back; companions are knocked out, and get up 5 s after the fight while she lives.
7. **Guide:** `kentucky-castle/docs/Companions/README.md` (follow, climb-leap and fight clips; climb,
   leap, fight and debug images); `AIScripting.md` §5.3 updated for companion targets.

8. **Follow-up round** (decisions 24-34). Playtested in ares:
   - follow with the new slots and pace;
   - climb and leap (8 of 8 within 1 unit);
   - steering at the test level's corners and the climb block's edges;
   - a fight with the far thrower as an aggressor;
   - knock-out and getting up;
   - "warp (out of view)" after a fight;
   - debug-menu spawns of both Minion companions, and Despawn All;
   - the Player's slap and climb after the driver change;
   - the CPU numbers above.

   Not reproduced: one Minion leap that never left the floor (the retry was added for it). Despawn
   Last was not tested on its own.

Playtest setup (session scratchpad, gone with it): a scratch copy built with the editor CLI, booting
scene 10 into a chosen level and spawn. Ares ran with a private settings file; keys were sent through
XTEST, and clips were recorded with ffmpeg x11grab.
