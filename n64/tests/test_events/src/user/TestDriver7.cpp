#include "script/userScript.h"
#include "./testLog.h"

// Drives scene 7, which nestingChecks.sh wraps into a prefab and unpacks again.
namespace P64::Script::C5A1B2C3D4E5F607
{
  using namespace P64::User::EventTest;
  P64_DATA( uint32_t pad; );

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_NESTING) return;
    switch(currFrame)
    {
      case 2: Events::post("scene::Press7"_event); break;
      case 3: Events::post("scene::Sound7"_event); break;
      default: break;
    }
  }
}
