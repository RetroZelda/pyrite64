#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

// Moves along X through the zone at a fixed speed per frame.
namespace P64::Script::C1D2E3F405162738
{
  using namespace P64::User::EventTest;

  P64_DATA(
    uint32_t pad;
  );

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_CONTACTS) return;
    obj.pos.x = -3.0f + 0.25f * (float)currFrame;
  }
}
