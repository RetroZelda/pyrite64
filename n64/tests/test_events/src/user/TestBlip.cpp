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

  void init(Object& obj, Data *data)
  {
    add(Rec::BlipSpawned, obj.id, obj.id);
  }
}
