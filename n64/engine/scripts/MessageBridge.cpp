/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Message Bridge, between events and the older addressed messages
// (Scene::sendEvent / onEvent, deprecated in the fork; the bridge uses Scene::sendEventInternal).
// Trigger sends a message of the chosen type to the target with the event's value. A message of
// that type sent to this object fires Received with its value.
#include "script/userScript.h"

namespace P64::Script::C0E000000000000F
{
  P64_DATA(
    [[P64::Name("Message Type")]]
    uint16_t messageType = 0;
    [[P64::Name("Target"), P64::Enum("0=Self, 1=Parent, 2=Children")]]
    uint8_t target = 0;
    uint8_t pad;
  );

  P64_EVENTS_IN(Trigger);
  P64_EVENTS_OUT(Received);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    auto &scene = SceneManager::getCurrent();
    switch(data->target)
    {
      case 1:
        if(obj.group) scene.sendEventInternal(obj.group, obj.id, data->messageType, ev.value);
        break;
      case 2:
        obj.iterChildren([&](Object* child) {
          if(child) scene.sendEventInternal(child->id, obj.id, data->messageType, ev.value);
        });
        break;
      default:
        scene.sendEventInternal(obj.id, obj.id, data->messageType, ev.value);
        break;
    }
  }

  void onEvent(Object& obj, Data *data, const ObjectEvent &event)
  {
    if(event.type == data->messageType && event.senderId != obj.id) {
      fire(obj, data, Out::Received, event.value);
    }
  }
}
