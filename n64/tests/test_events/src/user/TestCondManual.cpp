#include "script/userScript.h"
#include "./testLog.h"

// The same condition without triggers: it only answers when its Check input fires, or when a
// logic node reads it.
namespace P64::Script::C5A1B2C3D4E5F611
{
  using namespace P64::User::EventTest;

  P64_CONDITION_DATA(
    [[P64::Name("Slot"), P64::Range(0, 7)]]
    uint32_t slot = 0;
  );

  P64_CONDITION_TRIGGERS(); // an empty list is allowed: no triggers

  bool evaluate(Object& obj, Data *data)
  {
    return condSlots[data->slot & 7];
  }
}
