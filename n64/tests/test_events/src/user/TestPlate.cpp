#include "script/userScript.h"
#include "./testLog.h"

namespace P64::Script::C5A1B2C3D4E5F601
{
  using namespace P64::User::EventTest;
  P64_DATA( uint32_t pad; );
  P64_EVENTS_IN(Press);
  P64_EVENTS_OUT(Pressed);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    fire(obj, data, Out::Pressed);
  }
}
