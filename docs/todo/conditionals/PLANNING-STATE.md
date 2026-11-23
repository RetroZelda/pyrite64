# Conditions: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); it builds on the event graph
(`../event-graph/`).

- **Status:** in progress (started 2026-11-23). Phase log below.
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

## Phase log

(filled in as phases finish)
