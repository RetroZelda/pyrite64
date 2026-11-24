#include "script/userScript.h"
#include "./perf.h"

// Counts the events its Tick input receives.
namespace P64::Script::C5A1B2C3D4E5F620
{
  P64_DATA(
    [[P64::Name("Send Target")]]
    uint32_t sendTarget = 0;
  );
  P64_EVENTS_IN(Tick);

  void init(Object& obj, Data *data)
  {
    if(data->sendTarget == 1) P64::User::Perf::targetId = obj.id;
    if(data->sendTarget == 2) P64::User::Perf::targetFarId = obj.id;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    ++P64::User::Perf::sinkCalls;
  }
}
