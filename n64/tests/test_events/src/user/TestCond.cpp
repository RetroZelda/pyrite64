#include "script/userScript.h"
#include "./testLog.h"

// A condition for the runtime test: true while its slot of the test's "game state" is set.
// It checks itself whenever global::Test.CondTick fires.
namespace P64::Script::C5A1B2C3D4E5F610
{
  using namespace P64::User::EventTest;

  P64_CONDITION_DATA(
    [[P64::Name("Slot"), P64::Range(0, 7)]]
    uint32_t slot = 0;
  );

  P64_CONDITION_TRIGGERS("global::Test.CondTick"_event);

  bool evaluate(Object& obj, Data *data)
  {
    return condSlots[data->slot & 7];
  }
}
