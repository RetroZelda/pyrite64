#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

// Root script of the Blip prefab the Spawner creates.
namespace P64::Script::C3F405162738495A
{
  using namespace P64::User::EventTest;

  P64_DATA(
    uint32_t pad;
  );

  P64_EVENTS_IN(Poke, Nudge);

  void init(Object& obj, Data *data)
  {
    add(Rec::BlipSpawned, obj.id, obj.id);
  }

  // sent by id in the frame the Spawner queued this prefab, before the object existed
  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    add(input == In::Poke ? Rec::BlipPoked : Rec::BlipNudged, ev, obj.id);
  }
}
