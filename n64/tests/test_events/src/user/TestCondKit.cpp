#include "script/userScript.h"
#include "./testLog.h"

// The condition inside the CondKit prefab: true while the test's kitValue is the x position of
// the instance it sits in, so every instance answers differently.
namespace P64::Script::C5A1B2C3D4E5F612
{
  using namespace P64::User::EventTest;

  P64_CONDITION_DATA();

  P64_CONDITION_TRIGGERS("global::Test.KitTick"_event);

  bool evaluate(Object& obj, Data *data)
  {
    return kitValue == rootX(obj);
  }
}
