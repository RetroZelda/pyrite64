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

    // Like a level's volumes greeting their children while the level prefab is created: many sends
    // to an object of the same spawn, which exists already. They are queued at once, none is held
    // back or lost (BLIP_GREETINGS is more than P64_EVENT_HELD_SIZE).
    const uint16_t child = static_cast<uint16_t>(obj.id + 1);
    for(uint32_t i = 0; i < BLIP_GREETINGS; ++i) Events::send(child, "Greet"_pin, i, obj.id);
    Events::send(child, "Report"_pin, 0, obj.id);
  }

  // sent by id in the frame the Spawner queued this prefab, before the object existed
  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    add(input == In::Poke ? Rec::BlipPoked : Rec::BlipNudged, ev, obj.id);
  }
}
