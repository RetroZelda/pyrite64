#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

// The zone's script callbacks for collision enter and exit.
namespace P64::Script::C2E3F40516273849
{
  using namespace P64::User::EventTest;

  P64_DATA(
    uint32_t pad;
  );

  void onCollisionEnter(Object& obj, Data *data, uint16_t otherId)
  {
    add(Rec::ZoneEnterCb, otherId, obj.id);
  }

  void onCollisionExit(Object& obj, Data *data, uint16_t otherId)
  {
    add(Rec::ZoneExitCb, otherId, obj.id);
  }
}
