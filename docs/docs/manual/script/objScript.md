# Object-Scripts

Now we will focus on how to create a script for an object.

## Creating Scripts

Creating a script is very simple, in the "Files" tab under "Scripts" you can see all existing scripts.\
If you click the "+" icon, you can add a new one after entering a file name. 

```{image} /_static/img/script_create.png
:align: center
```
\
Once done, the editor creates it in the `src/user/` directory of your project,\
and now lists it in the editor.

Pyrite64 has no builtin code editor, so an external one (e.g. VSCode, CLion) is required.\
Either navigate to the directory, or right-click on the entry in the editor to open it:

```{image} /_static/img/script_edit.png
:align: center
```
\
Since scripts are internally identified by a UUID stored in the file itself,\
you are free to move or rename files at any point.

If "Shared Data" is checked when creating an object script, its data is put into a separate header.\
See [Shared Data](#shared-data) for more details.

## Using Scripts

Select the object you want to attach the script to,\
and add a new "Code" component to it:

```{image} /_static/img/script_add_comp.png
:align: center
```
\
Once done, you can now choose your script in the select-box:\
Alternatively, drag and drop the script from the list view into the select-box.

```{image} /_static/img/script_sel_comp.png
:align: center
```
\
You can add as many different scripts to an object as you want.\
This is useful to compose behaviors by having simple scripts that can be combined.

Using the same script on multiple objects is of course also possible.\
This happens naturally when you, for example, place multiple instances of the same object. 

## Script Contents

Files have the following structure:

```cpp
#include "script/userScript.h"
#include "scene/sceneManager.h"

namespace P64::Script::C4640C925988CA72
{
  P64_DATA(
    // data for the script instance
  );
   
  // one or more functions will follow... 
}
```

All object scripts are put in the `P64::Script` namespace, with their own UUID as the last name.\
The UUID (here `C4640C925988CA72`) is generated when the script is created,\
and is used internally to identify it.\
Renaming the file is therefore allowed, as long as the UUID is not touched.

Inside the namespace are two sections: a data block, and functions.

### Data Block

`P64_DATA` is a macro that will expand into a struct definition,\
you can put any variables you want in there, or keep it empty. \
For example:
```cpp
P64_DATA(
  fm_vec3_t direction;
  float speed;
  int status;
);
```


```{admonition} Warning:
:class: warning

Complex data types in P64_DATA should be avoided, 
constructors / destructors are not called automatically!
```

At runtime this struct is then instantiated for each object that has that script.\
Note ths this happens automatically, you don't need to manage this memory yourself.

Variables can also be exposed to the editor to allow setting an initial value.\
By default this is disabled, and you need to put a `P64::Name` attribute on members:

```cpp
P64_DATA(
  [[P64::Name("Speed")]]
  float speed;
  [[P64::Name("Some Other Value")]]
  int status;
  
  int noExposed;
);
```
Please make sure to put all exposed variables at the top of the block\
without any non-exposed variables in between.\
Ideally, also avoid any gaps in the memory layout to avoid misalignment issues:

```cpp
P64_DATA(
  [[P64::Name("Status")]]
  uint8_t status;
  // BAD: has 3 bytes of hidden alignment!
  [[P64::Name("Speed")]]
  float speed;
);
```

While you can use any type you want for variables,\
exposed ones are limited to a few known types:
- Integers: `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`
- Float: `float`
- Boolean: `bool`
- Color: `color_t`
- Vectors: `fm_vec3_t`, `fm_quat_t`
- References: `AssetRef<sprite_t>`, `ObjectRef`

The reference types allow you to plug in assets or objects from the editor.

Booleans show up as a checkbox, colors as a color picker with alpha.\
Vectors are edited as their individual components,\
quaternions as their raw `x,y,z,w` values.\
Default values can be set with a regular initializer, e.g. `fm_vec3_t dir = {0, 1, 0};`\
or `color_t tint = {255, 128, 0, 255};` (macros like `RGBA32(...)` can't be read by the editor.)\
A quaternion without a default value starts as the identity rotation.

Keep in mind that `bool` is a single byte, so like `uint8_t` it can introduce\
alignment gaps when followed by a larger type (see above).

In the editor, you can now see the values showing up:

```{image} /_static/img/script_args.png
:align: center
```

Further attributes change how a value is edited, see the sections below.\
To use several of them on a member, put them all into one `[[...]]` block, separated by commas:

```cpp
P64_DATA(
  [[P64::Name("Quality"), P64::Enum("0=Low, 1=Medium, 2=High")]]
  uint8_t quality = 1;
);
```

Separate blocks directly in front of the same member (e.g. one per line) are merged as well.

### Range, Min and Max Attributes

Integer and float members can be given bounds that the editor enforces.\
`P64::Range(min, max)` turns the input into a slider between the two values:

```cpp
P64_DATA(
  [[P64::Name("Speed"), P64::Range(0, 10)]]
  float speed = 2.5f;
  [[P64::Name("Lives"), P64::Range(1, 5)]]
  uint8_t lives = 3;
);
```

If you only want to limit a value without a slider, use `P64::Min(x)` and/or `P64::Max(x)`.\
These keep the regular number input but clamp whatever is entered:

```cpp
P64_DATA(
  [[P64::Name("Spawn Count"), P64::Min(1)]]
  uint16_t spawnCount = 1;
  [[P64::Name("Damping"), P64::Min(0), P64::Max(1)]]
  float damping = 0.9f;
);
```

The bounds are also applied when building the ROM, so values that ended up outside the range\
(e.g. from a hand-edited scene or a default value) are clamped with a warning in the log.

### Bitmask Attribute

For unsigned integer members (`uint8_t`, `uint16_t`, `uint32_t`) you can add a\
`P64::Bitmask` attribute to edit them as a set of named flags instead of a plain number.\
The attribute takes a comma-separated list of `bit=name` entries,\
where `bit` is the bit index (`0` being the least significant bit):

```cpp
P64_DATA(
  [[P64::Name("Elements"), P64::Bitmask("0=Fire, 1=Water, 2=Earth")]]
  uint8_t elementFlags;
);
```

In the editor this shows up as a select-box where you can toggle each named bit on or off.\
The resulting value is the combined bitmask, so for the example above selecting\
"Fire" and "Earth" stores `0b101` (`5`).

You don't need to name every bit, only the ones you want to expose.\
At runtime the member is just a regular integer, so you can test the bits as usual:

```cpp
if(data->elementFlags & (1 << 0)) { /* Fire is set */ }
```

Entries without a `bit=` part continue from the previous entry (starting at `0`),\
so `P64::Bitmask("Fire, Water, Earth")` is the same as the example above.\
Numbers can be decimal or hex with a `0x` prefix (a leading `0` does not make a number octal).\
An entry whose number can't be read (e.g. `1.5=Half`) or whose bit the member can't hold\
(e.g. bit `8` of a `uint8_t`) is skipped with a warning in the log.

### Enum Attribute

For integer members (`uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`) you can add a\
`P64::Enum` attribute to pick the value by name instead of entering a number.\
It uses the same format as the bitmask, a comma-separated list of `value=name` entries:

```cpp
P64_DATA(
  [[P64::Name("Spline Mode"), P64::Enum("0=Auto, 1=Loop, 2=Ping-Pong, 3=Once")]]
  uint8_t splineMode = 0;
  [[P64::Name("Spline Direction"), P64::Enum("0=Forward, 1=Reverse")]]
  uint8_t splineReverse = 0;
);
```

In the editor this shows up as a select-box with the names, selecting one stores its value.\
If the current value has no name, the plain number is shown instead.\
As with the bitmask, values can be decimal (negative ones too, for signed members) or `0x` hex (e.g. `0xFF=Default`),\
entries without a `value=` part continue from the previous entry,\
and an entry whose value can't be read or that the member can't hold (e.g. `300` for a `uint8_t`) is skipped with a warning in the log.

At runtime the member is just a regular integer, so it is a good idea to keep the same values in an enum:

```cpp
enum SplineMode : uint8_t { AUTO = 0, LOOP = 1, PING_PONG = 2, ONCE = 3 };

if(data->splineMode == SplineMode::ONCE) { /* stop at the end */ }
```

### Group Attribute

Members can be grouped into collapsible sections in the editor with `P64::Group`.\
A group starts at the member that has the attribute, and includes all members after it until the next group.\
An empty name ends the current group.

Example:
```cpp
P64_DATA(
  [[P64::Name("Health")]] int32_t health = 100;

  [[P64::Group("Movement"), P64::Name("Speed")]]
  float speed = 2.0f;
  [[P64::Name("Jump Height")]]
  float jumpHeight = 1.5f;

  [[P64::Group("Debug"), P64::Name("Show Path")]]
  bool showPath;
);
```

Groups are only for the editor and have no effect at runtime.

### RequireComponent Attribute

The select-box of an `ObjectRef` member lists the objects of the scene, sorted by name.\
You can type into it to filter the list, or drag an object from the {doc}`Graph <../editor/windows/sceneGraph>` onto it.\
Objects that share a name get their id appended (e.g. `Spline Path (#1234567)`) to tell them apart.\
Objects inside a prefab instance are not listed, as they are re-created with new ids whenever the scene loads.

(objscript-objectref-prefabs)=
```{admonition} Object references and prefabs
:class: warning

An `ObjectRef` is `0` inside prefabs and for spawned objects (`get()` returns `nullptr`).\
It is resolved when a scene is built and can only point to an object placed directly in that scene:

- objects inside a prefab instance can't be picked, they get new ids whenever the scene loads;
- the prefab data that objects are spawned from at runtime has no scene, so every `ObjectRef` in it is `0`;
- a reference set on an object inside a prefab only resolves where the prefab is placed in a scene that contains the referenced object.

Scripts inside prefabs and on spawned objects have to find other objects another way,
for a spline by its numeric **Path Id** (see {doc}`Spline <../editor/components/spline>`),
e.g. with a **Patrol Path Id** member next to the **Patrol Spline** below.
```

With a `P64::RequireComponent` attribute, only objects that have a component of the given type are offered\
(for a prefab instance, the components of its prefab count too).\
The type is the component name exactly as listed under "Add Component", e.g. `"Spline"`, `"Light"` or `"Camera"`:

```cpp
P64_DATA(
  [[P64::Name("Patrol Spline"), P64::RequireComponent("Spline")]]
  ObjectRef patrolSpline;
);
```

The name is case-sensitive. An unknown name (e.g. `"spline"`) can't filter anything: all objects are offered,\
and a warning below the select-box says so (suggesting `"Spline"` when only the case differs), which is also written to the log once.

Dropping an object without that component onto the select-box is rejected.\
If the stored object no longer matches (e.g. its component was removed), it is shown as `(invalid)`,\
and a reference to a deleted object is shown as `<missing ...>`, so you can see what to fix.

The attribute only affects the editor, so still check the component at runtime:

```cpp
Object *splineObj = data->patrolSpline.get();
auto *spline = splineObj ? splineObj->getComponent<Comp::Spline>() : nullptr;
if(spline) { /* follow the path */ }
```

(objscript-instance-overrides)=
### Prefab Instance Overrides

On a prefab instance, each exposed value can be overridden for that instance alone (the lock button next to it in the inspector).\
These overrides are stored under the member's C++ name (not its `P64::Name`),\
in the same place as the overrides of the object's other properties.\
So avoid member names the object already uses for something else:

- A member named like a property of the object itself (`pos`, `rot`, `scale`) or of one of its other components\
  (e.g. `mass` of a Rigid-Body, `volume` of an Audio (2D)) can't be overridden on an instance where that property is overridden:\
  it keeps the value from the prefab, and the inspector refuses to override it and says why.\
  Rename the member in the script to override it.
- Members with the same name in two scripts on the same object share one override.

### Global Memory

Since a script is just a regular C++ file, it is also possible to have global memory.\
This can be useful to share data global across all instances of a script.

The recommended way to do this is by using an anonymous namespace.\
Just be aware that nothing in there can be exposed to the editor,\
and any memory must be manually managed by you.

```cpp
#include "script/userScript.h"
#include "scene/sceneManager.h"

// Global data shared across all instances:
namespace 
{
  float someGlobalValue{42.0f};
}

namespace P64::Script::C4640C925988CA72
{
  ... 
}
```

### Shared Data

By default, the data of a script is only known to the script itself.\
If other scripts need to access it, the data can be put into a struct in a regular header instead.\
The struct can have any name and namespace (`P64::User` by default), and can also contain methods:

```cpp
// PlayerData.h
#pragma once
#include "script/userScript.h"

namespace P64::User
{
  struct PlayerData
  {
    [[P64::Name("Health")]]
    int32_t health = 100;

    void heal(int32_t amount);
  };
}
```

The script then uses `P64_DATA_TYPE` with the name of the struct, instead of `P64_DATA`.\
Everything else stays the same, `Data` can still be used in the script functions:

```cpp
// Player.cpp
#include "script/userScript.h"
#include "PlayerData.h"

namespace P64::Script::C3A1F09B22D4E781
{
  P64_DATA_TYPE(User::PlayerData);

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(data->health < 10)data->heal(1);
  }
}

void P64::User::PlayerData::heal(int32_t amount) {
  health += amount;
}
```

Methods of the struct have to be defined outside the script namespace, as shown above.\
Exposed members work the same as in `P64_DATA`, the editor finds the struct by following the includes of the script.

Any other script can now include the header and access the data on an object:

```cpp
#include "PlayerData.h"

if(auto *player = otherObj->getScriptData<User::PlayerData>()) {
  player->heal(5);
}
```

`getScriptData` returns `nullptr` if the object has no script using that type.\
The same header can also be used by multiple scripts, in which case any of them can be found that way.\
If multiple of those are on the same object, the first one is returned.

```{admonition} Warning:
:class: warning

The struct must be trivially copyable and standard-layout,
so no constructors, destructors, virtual functions or base classes.
Initial values are copied directly into memory, constructors are never called.
```

### Functions

The other section is a set of functions you can provide.\
A newly created script will contain all of them by default,\
and you can remove the ones you do not need.

All functions have at least two common arguments:
- `Object& obj` - The object the script is attached to
- `Data *data` - Data pointer for this instance, structure as defined in `P64_DATA` 

Currently allowed functions are:

#### Init
```cpp
void init(Object& obj, Data *data)
```
Initialization function, called once when the object spawns.\
At this point `data` has already been initialized with the values from the editor,\
or for non-exposed variables set to zero.\
If you do have non-trivial data types as variables, please also manually construct them here.

#### Destroy
```cpp
void destroy(Object& obj, Data *data)
```
Opposite of `init`, called once when the object is destroyed.\
This is the place to manually free any memory you may have allocated in `init`.\
If you do have non-trivial data types as variables, please also manually destruct them here.

Accessing other components of the object is not safe, as they might have been destroyed already.\
The object itself is still valid however, this can be useful to e.g. send an event to other objects.

#### onEnable
```cpp
void onEnable(Object& obj, Data *data)
```
Called every time the object is enabled and after `init` if enabled in the scene.\
This is the place to utilize initialized varaibles from `init` and/or register into external systems

Accessing other components of the object is generally safe, although they might not be enabled themselves yet.

#### onDisable
```cpp
void onDisable(Object& obj, Data *data)
```
Called every time the object is disabled and before `destroy` if enabled in the scene.\
This is the place to utilize unregister from external systems that may have been initialized in `onEnable`

Accessing other components of the object is generally safe, although they might still be enabled.

#### Update
```cpp
void update(Object& obj, Data *data, float deltaTime)
```
Called once per frame, before starting to draw anything.\
This is the place to put all your logic in, for example moving the object,\
interacting with other objects, accessing other components, etc.

To help with frame-rate independent movement, `deltaTime` is provided as an argument.\
This should be multiplied with any change in transformation.

For example, here is a function that moves an object at a constant speed:
```cpp
void update(Object& obj, Data *data, float deltaTime)
{
  constexpr fm_vec3_t dir{0.0f, 0.0f, 1.0f};
  obj.pos += dir * (deltaTime * data->speed);
}
```

Or one that rotates it around an axis:
```cpp
void update(Object& obj, Data *data, float deltaTime)
{
  constexpr fm_vec3_t rotAxis{0.0f, 0.5f, 0.2f};
  fm_quat_rotate(&obj.rot, &obj.rot, &rotAxis, deltaTime * ROT_SPEED);
  fm_quat_norm(&obj.rot, &obj.rot);
}
```

#### Fixed Update
```cpp
void fixedUpdate(Object& obj, Data *data, float fixedDeltaTime)
```

Called once per collision scene step, before collision detection and physics response.\
This is the place to put your logic that interacts with rigidbodies and colliders,\
as well as code that applies forces to bodies.
It is also the recommended place to perform raycasts into the collision scene.

There is a parameter `fixedDeltaTime` provided as an argument.\
This is the fixed Physics Step time mostly detached from the frame time.\
This is mostly fixed but may be different between scenes depending on the scenes `Physics Tickrate` setting.\
A lower `Physics Tickrate` may improve performance but at the same time reduces simulation accuracy.\
Choose according to your games needs.

For example, a fixed Update function that applies an acceleration at a specific point in space to a rigidbody:
```cpp
void fixedUpdate(Object& obj, Data *data, float fixedDeltaTime)
{
  constexpr float ACCELERATION = 1000.0f;
  fm_vec3_t localForward = obj.rot * VEC3_FORWARD;
  //choose point slightly unter rigidbodies center for more realistic effect
  fm_vec3_t applyAtPoint = obj.pos - (fm_vec3_t){0.0f, 5.0f, 0.0f};
  data->carRigidBody->applyForceAtPoint(localForward * data->moveInput * ACCELERATION, applyAtPoint);
}  
```

Or another example that performs a raycast and uses the result to apply a spring force to a rigidbody:
```cpp
void fixedUpdate(Object &obj, Data *data, float fixedDeltaTime)
{
  Coll::CollisionScene *collisionScene = Coll::collisionSceneGetInstance();
  Coll::RaycastHit hit;
  float maxLength = data->RestLength + data->SpringTravel;

  fm_vec3_t localUp = obj.rot * VEC3_UP;
  fm_vec3_t localDown = -localUp;
  data->wheelIsGroundedFlags[i] = 1;

  Raycast ray = Coll::Raycast::create(data->ConnectionPoint, localDown, maxLength, Coll::RaycastColliderTypeFlags::MESH_COLLIDERS, false, 0xff);
  if (collisionScene->raycast(ray, hit))
  {
    float currentSpringLength = hit.distance - data->WheelRadius;
    float springCompression = (data->RestLength - currentSpringLength) / data->SpringTravel;
    float springForce = springCompression * data->SpringStiffness;
    data->carRigidBody->applyForceAtPoint(localUp * springForce, data->ConnectionPoint);
  }
}
```

#### Draw
```cpp
void draw(Object& obj, Data *data, float deltaTime)
```
Called once per frame and once **per active camera**. \
Here you can put custom drawing logic, most commonly any 2D draws.\
Any builtin mesh-components attached to an object draw themselves automatically,\
so you don't need to do anything for that here.

If you have a culling-component attached, then this function may not always be called.\
Similarly, it may get called multiple times in case you have multiple active cameras.

If you need to know the current camera you are drawing for,\
use `obj.getScene().getActiveCamera()`.

By default, all draws assume to be running in the first 3D draw-layer.\
If you wish to draw 2D elements, first switch to a 2D layer and then back afterwards:

```cpp
void draw(Object& obj, Data *data, float deltaTime)
{
  DrawLayer::use2D();
    // some 2D draw
    rdpq_sprite_blit(data->icon.get(), data->posX, data->posY, nullptr);
   
  DrawLayer::useDefault();
}
```

#### OnEvent
```cpp
void onEvent(Object& obj, Data *data, const ObjectEvent &event)
```
Pyrite64's engine implements a basic event bus for objects.\
This allows them to send small messages to each other without having to know *what* object are.

An event is defined as such:
```cpp
struct ObjectEvent
{
  uint16_t senderId{};
  uint16_t type{};
  uint32_t value{};
};
```
Besides the sender, you will get a type and value.\
There are a few builtin types, so it must be explicitly checked.

The safe range for user-defined types is from `EVENT_TYPE_CUSTOM_START` to `EVENT_TYPE_CUSTOM_END`.\
This starts from `0`, whereas builtin types reserve the end of the range. 

The default script template defines this function as such:
```cpp
void onEvent(Object& obj, Data *data, const ObjectEvent &event)
{
  switch(event.type)
  {
    // you can check for your own custom types here
  }
}
```

It is also safe to send new events from within this function.\
Any outgoing events in general are deferred to the end of the current frame.\
This avoids infinite loops with objects referring to each other.

#### OnCollision
```cpp
void onCollision(Object& obj, Data *data, const Coll::CollEvent& event)
```
If a collider is attached to your object, you may start to receive collision events.\
The `event` argument gives you further information about the collision.\
For example which collider object or mesh was involved.\
Note that this function is directly called during the collision check, and not deferred like `onEvent`.
Collision checks are performed during the collision scene step which might happen more than once or not at all per frame\
depending on the Scenes framerate and the `Physics Tickrate` setting.

As an example, here is an object that plays a sound, spawns a particle effect,\
and then removes itself after colliding.
```cpp
void onCollision(Object& obj, Data *data, const Coll::CollEvent& event)
{
  AudioManager::play2D("sfx/CoinGet.wav64"_asset);
  obj.getScene().addObject("ParticlesCoin.pf"_asset, obj.pos);
  obj.remove();
}
```

#### Custom Functions

You can also put custom functions in the script.\
Those are not known to the engine and also never called automatically,\
but can be useful to organize your code.

Similarly to global variables, those should only be put in an anonymous namespace.\
For example:

```cpp
#include "script/userScript.h"
#include "scene/sceneManager.h"

// Global functions bound to this script:
namespace
{
  color_t getRainbowColor(float s) {
    float r = fm_sinf(s + 0.0f) * 127.0f + 128.0f;
    float g = fm_sinf(s + 2.0f) * 127.0f + 128.0f;
    float b = fm_sinf(s + 4.0f) * 127.0f + 128.0f;
    return color_t{(uint8_t)r, (uint8_t)g, (uint8_t)b, 255};
  }
}

namespace P64::Script::C4640C925988CA72
{
  P64_DATA(
    float time;
  );

  void update(Object& obj, Data *data, float deltaTime)
  {
    data->time += deltaTime;
    auto col = getRainbowColor(data->time);
    ...
  }
   
  ...
}
```
