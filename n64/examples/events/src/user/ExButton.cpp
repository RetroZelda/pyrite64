#include "script/userScript.h"

// Fires Pressed when the A button on controller 1 goes down.
namespace P64::Script::E7A3C1B2D4F50603
{
  P64_DATA( uint32_t pad; );
  P64_EVENTS_OUT(Pressed);

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(joypad_get_buttons_pressed(JOYPAD_PORT_1).a) fire(obj, data, Out::Pressed);
  }
}
