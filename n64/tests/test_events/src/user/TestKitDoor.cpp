#include "script/userScript.h"
#include "./testLog.h"

namespace P64::Script::C5A1B2C3D4E5F602
{
  using namespace P64::User::EventTest;
  P64_DATA( uint32_t pad; );
  P64_EVENTS_IN(Open, Spawned);
  P64_EVENTS_OUT(Opened);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    if(input == In::Spawned) {
      add(Rec::KitSpawned, rootX(obj), obj.id);
      return;
    }
    add(Rec::KitDoorOpened, rootX(obj), obj.id);
    fire(obj, data, Out::Opened);
  }
}
