#include "script/userScript.h"
#include "./testLog.h"

// Drives scene 8, whose bindings were all made by drawing wires (graphChecks.sh).
namespace P64::Script::C5A1B2C3D4E5F608
{
  using namespace P64::User::EventTest;
  P64_DATA( uint32_t pad; );
  P64_EVENTS_OUT(Go, Shut);

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_GRAPH) return;
    if(currFrame == 2) fire(obj, data, Out::Go);
    if(currFrame == 4) fire(obj, data, Out::Shut);
  }
}
