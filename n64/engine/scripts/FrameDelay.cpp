/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Frame Delay. Fires Elapsed a number of frames after the frame Start
// arrived in, with the value and instigator Start received. With Repeat it keeps firing at that
// interval until Cancel or Reset.
#include "script/userScript.h"
#include "lib/animController.h"

namespace P64::Script::C0E0000000000003
{
  P64_DATA(
    [[P64::Name("Frames"), P64::Min(1)]]
    uint16_t frames = 1;
    [[P64::Name("Repeat")]]
    bool repeat = false;
    bool running;
    uint16_t instigator;
    uint16_t pad;
    uint32_t startFrame;
    uint32_t value;
  );

  P64_EVENTS_IN(Start, Cancel, Reset);
  P64_EVENTS_OUT(Elapsed);

  void init(Object& obj, Data *data)
  {
    data->running = false;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    switch(input)
    {
      case In::Start:
        data->running = true;
        data->startFrame = (uint32_t)AnimController::getFrameCount();
        data->value = ev.value;
        data->instigator = ev.instigator;
        break;
      case In::Cancel:
      case In::Reset:
        data->running = false;
        break;
    }
  }

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(!data->running)return;
    const uint32_t now = (uint32_t)AnimController::getFrameCount();
    const uint32_t frames = data->frames ? data->frames : 1;
    if(now - data->startFrame < frames)return;

    if(data->repeat) {
      data->startFrame = now;
    } else {
      data->running = false;
    }
    fire(obj, data, Out::Elapsed, data->value, data->instigator);
  }
}
