# Event bus and conditions: benchmark

Measures what an event and a condition check cost, and how many fit into a frame. It is a small
project with one scene of 237 objects (bound script inputs, conditions, And nodes, a chain of
Relays) and a global script, `src/user/PerfTests.cpp`, that drives it.

## Run it

```sh
export N64_INST=$HOME/.local/n64
cd pyrite64
./pyrite64 --cli --cmd build "$PWD/n64/tests/perf_events/project.p64proj"
ares --system "Nintendo 64" n64/tests/perf_events/perf_events.z64
```

It takes a few seconds, prints one `[perf]` line per test to the debug log and ends with
`[perf] DONE`. The screen shows the same numbers on two pages that alternate: nanoseconds per unit
for the single tests, then how much fits into a frame.

**On a console** the numbers are the real ones: flash the ROM and read the screen (or the log
through USB). Everything below was measured in the ares emulator (v148), which counts CPU cycles
and cache misses but is not a console.

**The stress build** raises the bus limits so one drain can take a whole frame:

```sh
PERF_ENGINE_FLAGS="-DP64_EVENT_QUEUE_SIZE=16384 -DP64_EVENT_DRAIN_BUDGET=65536 -DP64_EVENT_MAX_SUBSCRIPTIONS=1024 -DP64_EVENT_INDEX_SIZE=1024" \
  ./pyrite64 --cli --cmd build "$PWD/n64/tests/perf_events/project.p64proj"
```

## How it measures

- Each test runs in one frame, from the global script's update hook. It produces events (`post`,
  `fire`, `send`) and then calls the bus's drain itself; both parts are timed with the CPU's cycle
  counter (`get_ticks()`, 21.3 ns a tick). Nothing else of the frame is inside the timed stretch.
- Every single test runs three times in three frames and the fastest run counts, so an audio or
  video interrupt that lands inside a timed stretch is not measured.
- Each test checks that the calls it expects really happened (the `ok` at the end of the line).
- The "frame of" tests repeat the same work until 16.67 ms (60 fps) or 33.33 ms (30 fps) are used
  up and report how many units were done: the whole frame given to the bus, nothing else.
- The conditions answer a global flag (`PerfCond.cpp`), the cheapest `evaluate()` there is. The flag
  flips before every check, so every check changes the answer and fires NewlyTrue or NewlyFalse.

`gen_scene.py` writes the scene; change the numbers there and in `PerfTests.cpp` together.

## Results (ares 148, 2026-11-23, default limits, 237 objects)

| What | Cost each |
|---|---|
| a drain with nothing queued (three a frame) | 1.4 µs |
| an event nobody listens to | 3.4 µs |
| an event to 1 code listener | 3.9 µs |
| an event to 4 / 16 code listeners | 5.6 µs / 12.5 µs (about 0.6 µs per further listener) |
| an event to 1 script input (a bound pin) | 4.9 µs |
| one event to 64 script inputs | 2.4 µs per input called |
| `fire()` of a script output, the object has 1 binding / 16 bindings | 5.7 µs / 8.3 µs |
| `send()` to an object by id | 5.6 µs to 6.1 µs |
| `Scene::getObjectById()`, id below 128 / above | 0.2 µs / 0.5 µs |
| a chain through 8 Relay objects | 90 µs (about 11 µs a hop) |
| a condition check, nothing bound to its outputs | 6.4 µs |
| a condition check that fires two bound outputs (True or False, and NewlyTrue or NewlyFalse) | 21.8 µs |
| conditions feeding And nodes: each check of a wave (32 conditions and 16 And nodes: 1.3 ms a wave) | 27 µs to 30 µs |
| `Conditions::evaluate()` from code | 0.6 µs |
| `Conditions::check()` from code, nothing bound | 2.9 µs |

| How much fits into a whole frame | 60 fps (16.7 ms) | 30 fps (33.3 ms) |
|---|---|---|
| events to 1 code listener | 4,300 | 8,600 |
| events to 1 script input | 3,500 | 7,100 |
| condition checks, nothing bound | 2,600 | 5,200 |
| condition checks firing two bound outputs | 770 | 1,540 |
| checks of conditions and And nodes together | 670 | 1,300 |

With the limits raised, one drain delivered every one of the 16,384 events the queue then holds, in
86 ms (the test posts 172 more than fit, and those are dropped, as intended). A single drain of
3,600 events took 18.6 ms, 5.2 µs each, about as much as the same events in drains of 100 (4.7 µs
each in that build). Both are a little more per event than with the default sizes.

**Numbers move between builds.** The same ROM gives the same numbers on every run, but a changed
scene or script moves things in memory, and the N64's data cache is small and direct-mapped. Across
the builds made for this test, single results moved by up to a factor of two (the Relay chain: 54 to
101 µs; an event to a script input: 4.5 to 6.8 µs). Read them as "a few microseconds", not to the
digit.

## What limits the bus before time does

The default sizes (`n64/engine/include/events/config.h`, each can be raised per project with
`ENGINE_CXXFLAGS`):

| Limit | Default | What happens beyond it |
|---|---|---|
| events waiting at once (`P64_EVENT_QUEUE_SIZE`) | 128 | further posts are dropped, with a warning (the test "posting more at once than the queue holds" shows 128 of 300 accepted) |
| events one drain delivers (`P64_EVENT_DRAIN_BUDGET`) | 256 | the rest waits for the next drain; three drains a frame, so 768 a frame |
| listeners at once (`P64_EVENT_MAX_SUBSCRIPTIONS`) | 512 | a listener is not added, with a warning. Every bound input pin and every condition trigger is one |
| Result outputs bound at once (`P64_CONDITION_MAX_PROVIDERS`) | 64 | a condition cannot be read by logic nodes, with a warning |
| sends waiting for objects being spawned (`P64_EVENT_HELD_SIZE`) | 32 | further ones are dropped, with a warning |

At about 4 µs an event, the 768 events a frame the default limits allow take about 3 ms: 9% of a
30 fps frame. So the limits, not the time, are what a game reaches first, and they sit at a sensible
share of a frame.

Memory, all static: the queue 2 KB (16 bytes an event), the listener table 10 KB (20 bytes each) and
its index 1 KB. Each bound pin adds 20 bytes to its object, a condition 4 bytes to its script data.

## What this test found

**Finding an object by id was slow above id 128.** `Scene::getObjectById()` had a direct table for
ids below 128 and went through every object for the rest, a cache miss each: 60 µs for the last of
the 237 objects here, against 0.2 µs. Every object of a spawned prefab has such an id, so in a game
whose levels are prefabs every `send()` by id, every `ObjectRef` and every logic node paid it.

| | Before | With the id table (`include/scene/idTable.h`) |
|---|---|---|
| `getObjectById()`, id above 128 | 60.4 µs | 0.5 µs |
| `send()` by id, id above 128 | 77.3 µs | 6.1 µs |
| conditions feeding And nodes, per check | 94 µs | 27 µs |
| ... of those, in a 60 fps frame | 192 | 670 |

## Where the time goes, if it ever matters

- An event costs about 3.4 µs before any listener runs: queueing, finding the listeners, recording
  it for the debug overlay's Events page.
- A script input costs more than a code listener (2.4 µs against 0.6 µs a call): the bus calls the
  object's EventLinks, which calls the component, which calls the script.
- `fire()` looks through the object's bindings every time, so an object with many bound pins pays
  more for each output it fires.
- A condition check always tries its three outputs (True or False, NewlyTrue or NewlyFalse,
  Result), bound or not: about 6 µs with nothing bound.
- A logic node costs a Result event in, an internal re-check event, and one `evaluate()` per input.
