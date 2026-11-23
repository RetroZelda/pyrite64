#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

// A hand-written trigger: fires Enter and Exit on fixed frames instead of reading collisions.
namespace P64::Script::C7A41B2C3D4E5F60
{
  using namespace P64::User::EventTest;

  P64_DATA(
    uint32_t pad;
  );

  P64_EVENTS_OUT(Enter, Exit, Spawned);

  void init(Object& obj, Data *data)
  {
    fire(obj, data, Out::Spawned, 1); // outputs work from init(): EventLinks is set up first
  }

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_DOOR) return;
    if(currFrame == 2) fire(obj, data, Out::Enter, 0, 77); // instigator 77: "who walked in"
    if(currFrame == 3) {
      if(auto *door = SceneManager::getCurrent().getObjectById(3)) {
        Events::send(*door, "Knock"_pin, 5, obj.id); // by input name, nothing bound
      }
    }
    if(currFrame == 4) fire(obj, data, Out::Exit);
  }
}
