#include "script/userScript.h"
#include "./testLog.h"

namespace P64::Script::C5A1B2C3D4E5F605
{
  using namespace P64::User::EventTest;
  P64_DATA( uint32_t pad; );
  P64_EVENTS_IN(Heard);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    add(Rec::TallyHeard, rootX(obj), obj.id);
  }
}
