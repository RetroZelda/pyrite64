#include "script/userScript.h"

// A door that slides up when it hears Open and back down on Close. Opened and Closed fire once it
// arrives. Its pins are wired in the Events window; nothing here knows who opens it.
namespace P64::Script::E7A3C1B2D4F50602
{
  P64_DATA(
    [[P64::Name("Lift")]]
    float lift = 1.0f;
    [[P64::Name("Speed")]]
    float speed = 2.0f;
    float baseY;
    float targetY;
    bool moving;
  );

  P64_EVENTS_IN(Open, Close);
  P64_EVENTS_OUT(Opened, Closed);

  void init(Object& obj, Data *data)
  {
    data->baseY = obj.pos.y;
    data->targetY = obj.pos.y;
    data->moving = false;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    data->targetY = data->baseY + (input == In::Open ? data->lift : 0.0f);
    data->moving = true;
  }

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(!data->moving) return;
    const float step = data->speed * deltaTime;
    const float left = data->targetY - obj.pos.y;
    if(left <= step && left >= -step) {
      obj.pos.y = data->targetY;
      data->moving = false;
      fire(obj, data, data->targetY > data->baseY ? Out::Opened : Out::Closed);
      return;
    }
    obj.pos.y += left > 0.0f ? step : -step;
  }
}
