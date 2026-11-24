#include "script/userScript.h"
#include "./testLog.h"

// Child of the Blip prefab: counts the greetings its root sends from init(), and records the count
// when asked.
namespace P64::Script::C5A1B2C3D4E5F615
{
  using namespace P64::User::EventTest;

  P64_DATA(
    uint32_t greetings;
  );
  P64_EVENTS_IN(Greet, Report);

  void init(Object& obj, Data *data)
  {
    data->greetings = 0;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    if(input == In::Greet) { ++data->greetings; return; }
    add(Rec::BlipGreeted, data->greetings, obj.id);
  }
}
