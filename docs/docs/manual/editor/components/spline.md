# Spline

An authored path through a list of points, used for example for AI patrols.\
In the viewport it is drawn as a curve with direction arrows (pointing the way the path is walked)\
and a green start marker at the first point.

New splines start as a closed loop of 4 points around the object.\
The quickest way to create one is **Add Spline Path** in the {doc}`Graph <../windows/sceneGraph>` right-click menu.\
An object can only have one Spline component, counting the one a prefab instance gets from its prefab.

## Options

| Option | Description |
|--------|-------------|
| **Type** | How the points are connected:<br>• **Linear**: straight lines between the points.<br>• **Smooth**: a curve through every point (centripetal Catmull-Rom). |
| **Closed** | Loops back from the last point to the first. Needs 3 or more points. |
| **Subdivisions** | Samples per segment, from 1 to 8. Only used by **Smooth**. |
| **Path Id** | Number that scripts can find this spline by, e.g. a guard's **Patrol Path Id**. Unlike an object reference it also works inside prefabs and for spawned objects (see {ref}`below <spline-referencing>`). 0 = none. |
| **Points** | The point table:<br>• **Position**: object-local, or world space with the **World** toggle.<br>• **Wait (s)**: how long a patrol stops at the point. |

The **Info** row shows the point count, the path length and how much ROM and N64 RAM the spline uses.

## Editing points

Press **Edit Points** in the inspector (or the **Toggle Point Edit** key in the viewport) to edit the points directly in the 3D viewport:\
click and drag the handles, move them with the gizmo, `Shift` + click on level geometry to draw a path,\
or `Shift` + click near the curve to insert a point.

The inspector offers the same operations as buttons (**Add**, **Insert**, **Delete**, **Reverse**, **Drop to Floor**),\
and the point table edits positions and waits directly (click a row number to select that point, which also starts point editing).\
While points are selected, the **Wait (s)** field below the buttons sets the wait of all of them at once.\
Every change is a single undo step, except for points edited in prefab edit mode (see below).

**Drop to Floor** moves the selected points (all of them when none are selected) straight down onto the closest surface\
below that the viewport draws, so a point never jumps up onto an overhang. Sides the viewport culls are seen through,\
as when looking down through a one-sided ceiling. A point inside a one-sided solid, or below the level, moves up onto\
the surface above it instead, by at most 100 units.

See {ref}`Editing spline points <viewport-spline-editing>` in the 3D Viewport page for all mouse and keyboard controls.

## Space and runtime

- Points are stored relative to the object: moving, rotating or scaling the object in the editor moves the whole path.
- Scene builds bake the points into world space, so the game walks exactly the curve shown in the editor.
- Prefab builds keep the points local, they are placed with the transform of the spawned object.\
  A spline on a child object of a prefab does not follow the spawned root.
- The path is fixed once loaded: moving the object at runtime does not move the path.
- Only splines of enabled objects are used: a disabled spline object is not found by its Path Id and AI does not follow it.
- AI following ignores the height (Y) of the path, keep the points close to the floor.

## Prefabs

- The points belong to the prefab and are shared by all of its instances.
- They can only be edited in prefab edit mode (**Edit** on the prefab instance), which cannot be undone.
- **Type**, **Closed**, **Subdivisions** and **Path Id** can be overridden per instance,\
  for example a different **Path Id** for every copy of a room.
- A Spline added directly to a prefab instance is fully editable and keeps its own settings (instance overrides don't apply to it).\
  The editor refuses a second Spline on an object, whether it would come from the prefab or the instance,\
  and the build warns about any object that still ends up with two.
- Scripts inside a prefab must find their spline by its **Path Id**, see {ref}`below <spline-referencing>`.

## Limits

- At most 64 points: the build logs an error and only exports the first 64.
- Waits from 0 to 25.5 seconds, in 0.1 second steps.
- The exported data takes `4 + align4(8 + 13 * points)` bytes, at most 844 bytes.
- At runtime the component takes `24 + 16 * samples + align4(points)` bytes of RAM,\
  where `samples` is the number of polyline points (`segments * subdivisions + 1`).

(spline-referencing)=
## Referencing a spline from a script

A script finds its spline in one of two ways:

- **Object reference**: an `ObjectRef` member pointing at the spline object, best with a\
  `[[P64::RequireComponent("Spline")]]` attribute so the editor only offers objects that have a Spline\
  (like the guard's **Patrol Spline**). At runtime: `P64::Comp::Spline::fromObject()`.
- **Path Id**: a number member holding the spline's **Path Id** (like the guard's **Patrol Path Id**).\
  At runtime: `P64::Comp::Spline::findByPathId()`.

```{admonition} Inside prefabs and for spawned objects, use the Path Id
:class: warning

An `ObjectRef` such as the guard's **Patrol Spline** is `0` inside prefabs and for spawned objects.\
Object references are resolved when a scene is built and can only point to objects placed directly in that scene:\
objects inside a prefab instance can't be picked (they get new ids whenever the scene loads),\
and the prefab data that objects are spawned from at runtime (for example by an Encounter Spawner) has no scene to resolve them in.\
So guards and spawners inside prefabs, and spawned guards, must use the numeric **Path Id**.
```

How a Path Id is looked up:

- Several splines may share a Path Id, for example the copies of a room prefab placed several times.\
  The lookup returns the one closest to the asking object, measured in 3D, so copies stacked on different floors work.
- Only splines of enabled objects with at least 2 points are found.

## Guard patrols

```{note}
This section describes the guard scripts of the Kentucky Castle project (`Enemy.cpp` with its AI lists, and `EncounterSpawner.cpp`), not engine features.
```

To make a guard walk a spline:

1. Create the path (e.g. with **Add Spline Path**) and edit its points.\
   Give it a **Path Id** if the guard is inside a prefab or spawned at runtime.
2. On the guard's Code component, set **AI List Id** to **Guard Spline** (or **Guard Patrol** / **Guard Alarm**).\
   The default, **Idle Guard**, holds its post and never patrols: a guard with **Idle Guard** and a spline set logs a warning once.
3. Set **Patrol Spline** to the spline object, or **Patrol Path Id** to its **Path Id**.
4. Pick the **Spline Mode** and **Spline Direction**.

| Field | Description |
|-------|-------------|
| **AI List Id** | **Guard Spline**, **Guard Patrol** and **Guard Alarm** walk the spline (**Guard Alarm** runs to an alarm pad when it notices the player).<br>Without a usable spline they walk the pad route of **Patrol Route Id** instead.<br>**Idle Guard**, the default, never patrols. |
| **Patrol Spline** | The spline object, the select-box only lists objects with a Spline. Used first.<br>Skipped when its object is disabled or has no usable spline (fewer than 2 points): **Patrol Path Id** is used then.<br>Only works for guards placed directly in a scene: it is `0` inside prefabs and for spawned guards. |
| **Patrol Path Id (0=none)** | The **Path Id** of the spline to walk (the closest enabled spline with that id).<br>Works everywhere, also inside prefabs and for spawned guards. |
| **Spline Mode** | • **Auto**: **Loop** on a closed path, **Ping-Pong** on an open one.<br>• **Loop**: round and round a closed path. On an open path the guard returns from the end to the start (not along the path) and starts over.<br>• **Ping-Pong**: walks to the end, turns around, and so on. On a closed path it turns around at the first point.<br>• **Once**: walks to the end and stays there, still on watch. Guards following it in the same direction line up behind it. |
| **Spline Direction** | **Forward** walks from point 0 towards the last point, **Reverse** the other way. |

While patrolling, a guard:

- joins the path at its closest point, walking there first when it is far away;
- stops for the **Wait (s)** of every point that has one, in either direction;
- keeps a gap to the guard ahead of it on the same path and in the same direction;
- returns to the path after combat or an investigation and carries on (a wait it was serving is not resumed);
- walks at its **Walk Speed** and ignores the height of the path.

### Spawned guards

A guard spawned by an **Encounter Spawner** comes from its prefab, so its **Patrol Spline** is always empty.\
The spawner can hand it a path, mode and direction instead:

| Field | Description |
|-------|-------------|
| **Patrol Path Id (0=Guard Default)** | **Path Id** of the spline the spawned guard walks. 0 keeps the guard prefab's own **Patrol Path Id**. |
| **Spline Mode** | Replaces the guard's **Spline Mode**, **Guard Default** keeps the prefab's. |
| **Spline Direction** | Replaces the guard's **Spline Direction**, **Guard Default** keeps the prefab's. |

Each field is applied on its own, so a spawner can for example only reverse the direction.\
The **AI List Id** of the guard prefab must still be one that patrols.

### At runtime

- A disabled spline object is never followed.\
  A guard whose spline gets disabled while it walks it switches to the closest other enabled spline with its **Patrol Path Id**, or stops.
- **Patrol Path Id** picks the closest spline with that id, measured in 3D, so copies of a room on several floors each keep their own guards.

### Debugging

The game's debug menu has these tweakables:

| Tweakable | Shows |
|-----------|-------|
| **Debug Draw > Splines > Paths** | Every enabled spline: the path (cyan), its first point (green sphere) and the points with a wait (magenta spheres). |
| **Debug Draw > Splines > Followers** | Every guard on a spline: its position on the path (orange sphere) and a line to the spot it walks to (yellow). |
| **Debug Text > Splines** | The number of enabled splines and of followers, and for up to 8 followers: object id `obj`, progress `s` / path length, direction `d` (1 forward, -1 reverse), state `st` (0 none, 1 follow, 2 wait, 3 rejoin, 4 done) and mode `m` (0 auto, 1 loop, 2 ping-pong, 3 once). |

The **Debug Draw** entries only draw while the debug overlay is open.\
**Debug Text > Guard** (for the guard picked with **Debug Text > Guard ID**) also shows that guard's spline and progress,\
and the **Gameplay > Guard > Spline ...** tweakables tune how guards follow paths.

## Runtime API

The runtime component turns the points into a polyline with the same code the editor uses for its preview.\
All queries work on the arc length `s` along that polyline, in world units:

```cpp
#include "scene/components/spline.h"

// the spline of a referenced object (unless it is disabled), else the closest enabled spline with Path Id 12
Object *refObj = data->patrolSpline.get();
P64::Comp::Spline *path = (refObj && refObj->isEnabled()) ? P64::Comp::Spline::fromObject(refObj) : nullptr;
if(!path || !path->isValid())path = P64::Comp::Spline::findByPathId(12, obj.pos);

if(path && path->isValid())
{
  // arc length of the point on the path closest to the object (ignores Y by default)
  float s = path->getClosest(obj.pos);

  // walk along the path, wrapping around closed paths
  s = path->wrapDistance(s + speed * deltaTime);
  fm_vec3_t target = path->getPosition(s);
  fm_vec3_t dir = path->getDirection(s);

  // closest point limited to a window around the last position,
  // avoids jumping between two legs of the path that pass close to each other
  float sNext = path->getClosestInRange(obj.pos, s - 50.0f, s + 50.0f);
}
```

Only splines of enabled objects are live: `findByPathId()` and `getFirst()` / `getNext()` skip disabled ones,\
while `fromObject()` still returns the spline of a disabled object (hence the `isEnabled()` check above).\
`findByPathId()` only returns valid splines (2 or more points), the one closest to the given position in 3D.

Splines are destroyed with their object (e.g. on a scene change).\
If you keep a pointer to one, re-resolve it whenever `P64::Comp::Spline::getEpoch()` changes:\
it changes whenever a spline is destroyed or its object is disabled (not when one is enabled).

## See also

- {ref}`Editing spline points <viewport-spline-editing>`: editing the points in the 3D viewport.
- {doc}`Object-Scripts <../../script/objScript>`: `ObjectRef` members and the `P64::RequireComponent` and `P64::Enum` attributes.
- {cpp:struct}`P64::Comp::Spline`: the runtime component in the C++ API.
