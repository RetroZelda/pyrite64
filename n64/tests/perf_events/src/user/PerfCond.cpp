#include "script/userScript.h"
#include "./perf.h"

// The cheapest possible condition: answers the benchmark's flag. It has no triggers; the scene
// binds an event to its Check input.
namespace P64::Script::C5A1B2C3D4E5F622
{
  P64_CONDITION_DATA(
    [[P64::Name("Code Target")]]
    uint32_t codeTarget = 0;
  );

  void init(Object& obj, Data *data)
  {
    if(data->codeTarget) P64::User::Perf::evalId = obj.id;
  }

  bool evaluate(Object& obj, Data *data)
  {
    ++P64::User::Perf::evaluations;
    return P64::User::Perf::flag;
  }
}
