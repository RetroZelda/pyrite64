#include "script/userScript.h"
#include "scene/sceneManager.h"

namespace P64::Script::__UUID__
{
  P64_DATA(
    // Put your arguments and runtime values bound to an object here.
    // If you need them to show up in the editor, add a [[P64::Name("...")]] attribute.
    //
    // Types that can be set in the editor:
    // - uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t
    // - float
    // - bool (checkbox)
    // - color_t (color picker, default e.g. `color_t tint = {255, 0, 0, 255};`)
    // - fm_vec3_t, fm_quat_t
    // - AssetRef<sprite_t>
    // - ObjectRef (only objects placed directly in a scene: it is 0 inside prefabs and for spawned objects)
    //
    // For numbers you can add [[P64::Range(0, 10)]] to edit them as a slider,
    // or [[P64::Min(0)]] / [[P64::Max(10)]] to only clamp the entered value.
    //
    // For unsigned integers (uint8_t/uint16_t/uint32_t) you can add a
    // [[P64::Bitmask("0=Fire, 1=Water, 2=Earth")]] attribute to edit them as a
    // named multi-select of bits instead of a plain number.
    // [[P64::Enum("0=Off, 1=On")]] on integers shows a select-box with named values, and
    // [[P64::RequireComponent("Spline")]] on an ObjectRef only lists objects with that component.
    // Bitmask / Enum numbers can be decimal or 0x hex.
    // Put several attributes into one block: [[P64::Name("Mode"), P64::Enum("0=A, 1=B")]]
    //
    // Avoid member names the object already uses (pos, rot, scale, ...):
    // prefab instances can't override such a member while that property is overridden.
    //
    // [[P64::Group("Your Name")]] puts this and all following members into a group.
    // Until the next or an empty group ends it.
    //
    // Other types can be used but are not exposed in the editor.
  );

  // The following functions are called by the engine at different points in the object's lifecycle.
  // If you don't need a specific function you can remove it.

  void init(Object& obj, Data *data)
  {
    // initialization, this is called once when the object spawns
  }

  void destroy(Object& obj, Data *data)
  {
    // clean-up, this is called when the object gets deleted
  }

  void update(Object& obj, Data *data, float deltaTime)
  {
    // this is called once every frame, put your main logic here
  }

  void fixedUpdate(Object& obj, Data *data, float fixedDeltaTime)
  {
    // this is called on the fixed physics timestep before collision/physics are stepped
  }

  void draw(Object& obj, Data *data, float deltaTime)
  {
    // this is called once every frame, and for every active camera.
    // Put your drawing code here
  }

  void onEnable(Object& obj, Data *data)
  {
    // this is called when the object is enabled and active in scene
  }

  void onDisable(Object& obj, Data *data)
  {
    // this is called when the object is disabled and inactive in scene
  }

  void onReady(Object& obj, Data *data)
  {
    // object is fully initialized, no update call has happened yet
  }

  void onEvent(Object& obj, Data *data, const ObjectEvent &event)
  {
    // generic events an object can receive
    switch(event.type)
    {
      // you can check for your own custom types here
    }
  }

  void onCollision(Object& obj, Data *data, const Coll::CollEvent& event)
  {
    // collision callbacks, only used if any collider is attached
  }
}
