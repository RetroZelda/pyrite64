# Conditions: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); it builds on the event graph
(`../event-graph/`).

- **Status:** ALL DONE (2026-11-23). Every phase is built, tested and committed; the guide is
  updated in kentucky-castle (`docs/EventGraph/README.md`). Waiting for the owner's review.
- **Branches:** `feature/conditionals` in `pyrite64` and in `kentucky-castle`, both stacked on
  `feature/event-graph` (pushed to origin 2026-11-23). Each phase is committed, with no co-author or
  AI attribution lines. Nothing is pushed until the owner asks.
- **Status board:** request `5rcakn` (http://mini-debian:8765/r/5rcakn).

## The owner's request, word for word (2026-11-23)

> next I want a conditional system. conditionals should be stateless objects that fire events when
> a condition changes. there should be input events to enable/disable, manual check, and reset.
> output events should be true or false, newly true(was false) or newly false(was true)
>
> an example conditional would be "Player Health Condition" that has input parameter of either a
> percentage or a raw value, and a comparison operation. it should evaluate true if the input value
> evaluates correctly. this will allow conditionals to fire events when they evaluate.
>
> conditionals should be able to automatically trigger when a gameplay event happens. so a c++ only
> system to register to specific events would be needed. c++ or event scripts or node scripts should
> be able to fire the event that can trigger the conditional. for example a player health update
> event would be needed to trigger the conditional. so when the player takes damage or health, that
> event should trigger from the game code to the conditional.
>
> conditionals should be able to get placed in event graphs at any level. not all conditionals
> should be expected to be triggers.
>
> we will also need basic logical conditional operations nodes too. AND, OR, XOR, NOT, etc
>
> conditional variables should not be pinable - they should be statically set and compiled into the
> graphs.

## Decisions made without the owner

The owner gave the brief and asked for no question round, so each open point got the option I would
recommend. Change any of them on request.

1. A condition is a script with `bool evaluate()` on an object (like the built-in event objects),
   marked by `P64_CONDITION_DATA(...)` in place of `P64_DATA(...)`. "Stateless" is read as: the
   answer always comes from the game; the engine remembers only the last answer (for "newly") and
   whether the condition is enabled.
2. Pin names: Enable, Disable, Check, Reset; True, False, NewlyTrue, NewlyFalse, and a fifth
   output, Result (value 1 or 0 on every check), which is what logic nodes read.
3. A condition starts as false: the first check that is true fires NewlyTrue. Reset goes back to
   false and to "Starts Enabled" and fires nothing.
4. Enable and Disable are the condition's own pins and take effect at once (the object's own
   Enable/Disable pins act a frame later). Enable does not check; bind an event to Enable and to
   Check to do both, Enable is handled first.
5. True / False / NewlyTrue / NewlyFalse pass on the value of the event that caused the check, like
   the other built-in objects; Result carries 1 or 0.
6. Triggers are declared in C++ with `P64_CONDITION_TRIGGERS(...)` and compiled into the object's
   bindings (on Check) by the build; the graph shows them locked.
7. Logic nodes read their inputs by calling `evaluate()` (always current), and follow them through
   the Result event (one check per wave). A disabled condition still answers when read.
8. Logic nodes take any number of inputs on one pin, In. Xor is "an odd number is true"; Not with
   several inputs is NOR. NAND / NOR / XNOR are not separate nodes (a Not behind And / Or / Xor).
9. A Result wire is a named event, so conditions work at every level with the existing scope rules
   instead of object references (which the engine cannot resolve inside prefabs).
10. The example trigger event is a game-namespace event, `player::HealthChanged`, because a system
    output (`global::System.Pin`) cannot be fired from an event graph.
11. New branches `feature/conditionals`, stacked on `feature/event-graph`.
12. The pin of a logic node that takes conditions is called **Inputs**.
13. A logic node that checks itself because an input was checked fires True, False, NewlyTrue and
    NewlyFalse with its own answer (1 or 0) as the value. Passing on the input's Result value would
    depend on which input happened to arrive first.
14. **Auto Check** (not in the brief): an argument of every condition whose script names triggers, on
    by default. Off, the build leaves the triggers out for that object, or for one prefab instance
    when it is overridden there. Without it a condition with triggers could not be used as a plain
    "only if" test, because every trigger would fire its True or False.
15. The Events window shows a condition with its own header colour and icon, its values one per line
    under the title, its triggers as locked wires into Check, and Result outputs and the inputs
    that read them in their own colour. The add menu has a **Condition** entry (the project's
    condition scripts, then And, Or, Xor, Not).
16. Node graphs got a **Fire Event** node (name as text, value a number or a hashed string like the
    Send Event node). A node graph cannot listen to a named event; that was not asked for.
17. The project index counts a trigger as "listens" (so a trigger nothing fires is reported) and a
    Fire Event node as "fires". The event browser and rename list both like code uses.
18. Fixed on the way: the build did not find an instance override of a script argument that the
    prefab file never stored (the argument had no id), and built the default. "Starts Enabled" and
    "Auto Check" are such arguments on every existing condition object.
19. Fixed on the way: `nestingChecks.sh` required "12 of 12" and had failed since test 13 existed; it
    also tried to stop the emulator by a name it does not run under.
20. Kentucky Castle: the health event is fired by a new **HealthChanged** output of Damagable, bound
    to `player::HealthChanged` in the Player prefab, so an event graph fires the trigger. Burn ticks
    and healing write the health from outside and report through a HealthChanged input. No level
    contains a condition yet.
21. The Player Health Condition takes the perception system's primary target as the player. A player
    that is not in the damage registry (it died) counts as 0 health, like the HUD does; without a
    player object the answer is false.
22. Not built: separate NAND / NOR / XNOR nodes, a node-graph node that listens to events, a line for
    conditions on the debug overlay's Events page. The manual stays the guide in kentucky-castle;
    the engine repo has the changelog entry.

## Phase log

| Phase | Commit | What |
|---|---|---|
| 1, 2, 5 | pyrite64 61b9fd9 | runtime, macros, logic nodes, parsing, pins, trigger records, build checks; unit tests and scene 10 |
| 3, 4 | pyrite64 93dbe53 | Events window, inspector, command-line text, Fire Event node, `conditionChecks.sh`, `nestingChecks.sh` fix |
| 3 | pyrite64 dcbcf7f | Auto Check, values one per line, the argument override fix |
| 6 | kentucky-castle a9b96a4 | `player::HealthChanged`, Damagable output, Player Health Condition |
| 7 | kentucky-castle aff2779 | the guide |

## What was verified

- Host unit tests: 115 cases, 1,971 assertions (21 cases are new).
- `n64/tests/test_events` in ares: 15 of 15, test 14 is the conditions (scene 10).
- Shell checks: `conditionChecks.sh` 22 of 22, `graphChecks.sh` 7 of 7, `nestingChecks.sh` 11 of 11,
  `buildChecks.sh` 5 of 5.
- By hand, on a copy: "Auto Check" off and "Starts Enabled" off, each overridden on one instance of
  the CondKit prefab, change only that instance (the test then fails for exactly that instance).
- Kentucky Castle, on a copy, in ares: a throwaway script changed the player's health; two Player
  Health Conditions and an And node fired exactly at the crossings.
- The Fire Event node was looked at in the node-graph editor (it shows its Event and Value fields)
  and run in scene 10.
- Not verified: the Player Health Condition with damage from real combat (the damage path calls the
  same `fire()` as the tested paths).
