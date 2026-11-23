#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

namespace P64::Script::C8B52C3D4E5F6071
{
  using namespace P64::User::EventTest;

  P64_DATA(
    uint32_t pad;
  );

  P64_EVENTS_IN(Open, Close, Knock);
  P64_EVENTS_OUT(Opened, Closed);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    switch(input)
    {
      case In::Open:
        add(Rec::Open, ev, obj.id);
        fire(obj, data, Out::Opened, obj.id); // continues the chain: same instigator
        break;
      case In::Close:
        add(Rec::Close, ev, obj.id);
        fire(obj, data, Out::Closed, obj.id);
        break;
      case In::Knock:
        add(Rec::Knock, ev, obj.id);
        break;
    }
  }
}
