# Conditions: plan

A condition is an object that answers a yes/no question about the game ("is the player's health
below 25%?") and fires events when it is asked and when the answer changes. It builds on the event
graph (`docs/todo/event-graph/`). Status and decisions: [PLANNING-STATE.md](PLANNING-STATE.md).

## 1. What the owner asked for (2026-11-23)

- Conditions are stateless objects that fire events when a condition changes.
- Inputs: enable, disable, a manual check, reset.
- Outputs: true, false, newly true (was false), newly false (was true).
- Example: a "Player Health Condition" with a value that is either a percentage or a raw number,
  and a comparison operation.
- A condition can check itself when a gameplay event happens. Which events is decided in C++ only.
  C++, event graphs and node graphs must all be able to fire such an event.
- Conditions can be placed in event graphs at any level. Not every condition has such triggers.
- Logic nodes: AND, OR, XOR, NOT.
- A condition's variables are not pins: they are set statically and compiled into the graph.

## 2. The model

**A condition is a script with `bool evaluate()`** on an object, like the built-in event objects.
`evaluate()` reads the game and returns the answer; it stores nothing. The engine adds the pins and
the little memory they need (the last answer, and whether the condition is enabled).

```cpp
#include "script/userScript.h"

namespace P64::Script::C0FFEE0000000001
{
  P64_CONDITION_DATA(                       // instead of P64_DATA: marks the script as a condition
    [[P64::Name("Compare"), P64::Enum("0=Less, 1=Less or equal, 2=Equal, 3=Not equal, 4=Greater or equal, 5=Greater")]]
    uint8_t compare = 0;
    [[P64::Name("Unit"), P64::Enum("0=Percent, 1=Points")]]
    uint8_t unit = 0;
    uint16_t pad;
    [[P64::Name("Value")]]
    float value = 25.0f;
  );

  // optional, C++ only: the condition checks itself whenever one of these events fires
  P64_CONDITION_TRIGGERS("player::HealthChanged"_event);

  bool evaluate(Object& obj, Data *data)
  {
    return compare(playerHealth(data->unit), data->value, data->compare);
  }
}
```

### 2.1 Pins every condition has

| Pin | Direction | What it does |
|---|---|---|
| Enable | in | the condition reacts again (takes effect at once) |
| Disable | in | it ignores Check and its triggers until Enable |
| Check | in | evaluate now and fire the outputs |
| Reset | in | forget the last answer (back to false) and return to "Starts Enabled"; fires nothing |
| True | out | fired by every check whose answer is true |
| False | out | fired by every check whose answer is false |
| NewlyTrue | out | the answer was false at the last check and is true now |
| NewlyFalse | out | the answer was true at the last check and is false now |
| Result | out | fired by every check, value 1 or 0; this is also what logic nodes read |

- A condition starts as false, so the first check that is true fires NewlyTrue.
- True, False, NewlyTrue and NewlyFalse pass on the value of the event that caused the check. A
  logic node that checks itself because one of its inputs was checked fires them with its own
  answer as the value: 1 or 0.
- The static fields the engine adds are **Starts Enabled** (on) and, for scripts that name
  triggers, **Auto Check** (on).
- When one event is bound to Enable and to Check of the same condition, Enable is handled first.
- A disabled condition fires nothing, but it still answers when a logic node reads it.

### 2.2 Triggers (C++ only)

`P64_CONDITION_TRIGGERS("ns::Name"_event, ...)` lists the events that make the condition check
itself. The build binds them to the Check input of every object that uses the script; they are not
stored in scenes and cannot be edited in the graph. The editor shows them on the Check pin with a
lock. A script without the macro is only checked through its Check input or read by a logic node.

**Auto Check** switches the triggers off for one object (or, as an override, for one prefab
instance): the build then leaves the bindings out, so the condition only answers when its Check
input fires or a logic node reads it. That is how a condition with triggers is used as a plain
test.

Any event name works. One that C++, event graphs and node graphs can all fire is an event in a
namespace the game declares (`events/*.json`), for example `player::HealthChanged`. A system output
(`global::System.Pin`) can only be fired by its system.

### 2.3 Logic nodes

Built-in objects **And**, **Or**, **Xor**, **Not**. Each is a condition itself (all the pins above)
and has one more input, **Inputs**. Wire the Result of any number of conditions into Inputs.

| Node | Answer |
|---|---|
| And | every input is true (and there is at least one) |
| Or | at least one input is true |
| Xor | an odd number of inputs is true (with two inputs: exactly one) |
| Not | no input is true (with one input: its opposite; with several: NOR) |

NAND, NOR and XNOR are a Not behind an And, Or or Xor.

- **Reading.** When a logic node is checked it evaluates its inputs on the spot (it calls their
  `evaluate()`), so the answer is always current, also for inputs that have no triggers.
- **Following.** When an input is checked, its Result event reaches Inputs and the logic node checks
  itself once the current events have settled. So a logic node follows the triggers of its inputs
  without triggers of its own, and several inputs checked by the same event cause one check.
- A Result wire is a named event like any other (`scene::LowHealth.Result`), so it follows the same
  scope rules: inside prefabs, across nested prefabs, with per-instance overrides.
- An Inputs binding whose name no condition provides counts as a false input; the build warns.
- Game scripts can combine conditions the same way: `P64_CONDITION_INPUTS(Pin)` marks an input pin
  as reading conditions, `Conditions::Inputs` reads them, `Conditions::notify()` schedules the check.

### 2.4 From code

```cpp
#include "events/conditions.h"
bool now = P64::Conditions::evaluate(obj);   // the answer, nothing fired
P64::Conditions::check(obj);                 // like the Check input
P64::Events::send(obj, "Check"_pin);         // the same through the queue
```

### 2.5 Node graphs

A new node-graph node, **Fire Event**, posts a named event (`postTo` in the scope of the graph's
object), so a node graph can fire the event a condition is triggered by.

## 3. How it is built

| Part | Where |
|---|---|
| Pin names and order, shared by engine and editor | `n64/engine/include/events/pins.h` (`CONDITION_IN`, `CONDITION_OUT`, pin index base 0x80) |
| Macros | `n64/engine/include/script/userScript.h` |
| Runtime: header in the script data, check, provider registry, coalesced re-check | `n64/engine/include/events/conditions.h`, `src/events/conditionCore.*` (host-testable), `src/events/conditions.cpp` |
| Script table: `evaluate` | `include/script/scriptTable.h`, `src/build/scriptBuilder.cpp` |
| Pin dispatch | `include/scene/components/code.h` (pins >= 0x80 go to the condition runtime), `src/events/bus.cpp` (`send()` by name) |
| Logic nodes | `n64/engine/scripts/ConditionAnd.cpp`, `ConditionOr.cpp`, `ConditionXor.cpp`, `ConditionNot.cpp` (UUIDs `C0E0000000000010` to `13`) |
| Editor: parsing | `src/utils/codeParser.*` (`P64_CONDITION_DATA`, `P64_CONDITION_TRIGGERS`, `P64_CONDITION_INPUTS`) |
| Editor: pins, build records, checks | `src/project/events/eventPins.*`, `src/build/eventBuilder.cpp`, `src/project/events/eventRules.*` |
| Editor: graph and windows | `src/project/events/eventGraph.*`, `src/editor/pages/parts/eventGraphWindow.cpp`, `eventInspector.cpp` |
| Node graph node | `data/nodes/builtin/object.js` |
| Tests | `tests/conditionCore.cpp`, `tests/eventEditor.cpp`, `n64/tests/test_events` |

**Data layout.** `P64_CONDITION_DATA(...)` puts a 4-byte header in front of the script's fields:
`startsEnabled`, `flags` (enabled, last answer, re-check pending), `autoCheck` (read by the build
only) and one reserved byte. The editor's parser adds the same fields in front, so "Starts Enabled"
and "Auto Check" are ordinary arguments, with instance overrides like any other.

**Pins.** A condition script's pins are its own (`P64_EVENTS_IN/OUT`, index from 0) plus the
condition pins (index 0x80 + position). The Code component hands pins >= 0x80 to the runtime.

**Providers.** A condition whose Result is bound registers (name hash, scope, instance) -> itself
in a small table (`P64_CONDITION_MAX_PROVIDERS`, 64). A logic node walks its Inputs bindings and
looks each one up.

**Re-check.** A Result event arriving at Inputs does not check at once: the node sends itself an
internal `Recheck` (once, a flag is set while it is on its way). The check then runs after every
event of the same wave, so each wave causes one check. Each logic level costs two hops.

## 4. Phases

1. Engine: macros, header, runtime, script table, pin dispatch, logic nodes, host unit tests.
2. Editor: parsing, pins, build records for triggers, checks, unit tests.
3. Events window and inspector: condition nodes (colour, parameters and triggers on the node),
   Result wires, the add menus, command-line text.
4. Node graph: the Fire Event node; the project index counts its uses.
5. Runtime tests in `n64/tests/test_events`: check, triggers, edges, enable, disable, reset, the
   four logic nodes, one check per wave, conditions inside spawned prefabs, the Fire Event node.
6. Kentucky Castle: `player::HealthChanged` (catalog, Damagable output, Player prefab binding), the
   Player Health Condition, a scratch-copy test in the emulator.
7. Guide (`kentucky-castle/docs/EventGraph/README.md`), changelog, report.
