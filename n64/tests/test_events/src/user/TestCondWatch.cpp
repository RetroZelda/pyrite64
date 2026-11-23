#include "script/userScript.h"
#include "./testLog.h"

// Inside the CondKit prefab: records what the instance's Not fires, with the instance's x.
namespace P64::Script::C5A1B2C3D4E5F613
{
  using namespace P64::User::EventTest;

  P64_DATA( uint32_t pad; );
  P64_EVENTS_IN(NotTrue, NotFalse);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    Events::Event rec = ev;
    rec.value = rootX(obj);
    add(input == In::NotTrue ? Rec::KitNotTrue : Rec::KitNotFalse, rec, obj.id);
  }
}
