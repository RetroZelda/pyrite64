# Companions: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); the investigation notes are in
[FINDINGS.md](FINDINGS.md).

- **Status (2026-11-26):** draft 2. The owner answered Q1-Q3 (PLAN section 9) and the build has
  started, phase by phase (PLAN section 8).
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

(none yet)

## Phase log

(none yet)
