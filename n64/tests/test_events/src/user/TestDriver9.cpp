#include "script/userScript.h"
#include "./testLog.h"

// Drives scene 9: shows the TestHud canvas, then plays its Flash clip, through its pins.
namespace P64::Script::C5A1B2C3D4E5F609
{
  using namespace P64::User::EventTest;
  P64_DATA( uint32_t pad; );
  P64_EVENTS_OUT(Show, Flash);

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_CANVAS) return;
    if(currFrame == 2) fire(obj, data, Out::Show);
    if(currFrame == 4) fire(obj, data, Out::Flash);
  }
}
