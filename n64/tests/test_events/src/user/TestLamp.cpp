#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

// Sits next to a door script on the same object and hears its object:: events only.
namespace P64::Script::C9C63D4E5F607182
{
  using namespace P64::User::EventTest;

  P64_DATA(
    uint32_t pad;
  );

  P64_EVENTS_IN(Light);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    if(input == In::Light) add(Rec::Light, ev, obj.id);
  }
}
