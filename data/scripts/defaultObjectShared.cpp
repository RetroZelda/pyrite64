#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "__STRUCT__.h"

namespace P64::Script::__UUID__
{
  P64_DATA_TYPE(User::__STRUCT__);

  // Event pins (optional). Inputs can be bound to events in the editor and arrive in onInput();
  // outputs are fired with fire(obj, data, Out::Name, value) and post the events they are bound to.
  //   P64_EVENTS_IN(Open, Close);
  //   P64_EVENTS_OUT(Opened);
  //   void onInput(Object& obj, Data *data, In input, const Events::Event& ev) { switch(input) { ... } }
  // Note: onInput must not be the name of a helper function anywhere else in this file.

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

  void onCollision(Object& obj, Data *data, const Coll::CollEvent& event)
  {
    // collision callbacks, only used if any collider is attached
  }
}
