#include "script/userScript.h"
#include "./testLog.h"

// Drives the prefab scope test of scene 6.
namespace P64::Script::C5A1B2C3D4E5F606
{
  using namespace P64::User::EventTest;
  P64_DATA(
    uint16_t spawned;
    uint16_t pad;
  );

  void init(Object& obj, Data *data)
  {
    data->spawned = 0;
  }

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_PREFABS) return;
    auto &scene = SceneManager::getCurrent();
    switch(currFrame)
    {
      case 2: Events::post("scene::Press1"_event); break;
      case 3: Events::post("scene::Press2"_event); break;
      case 4: Events::post("scene::PressE"_event); break;
      case 5: Events::post("scene::RaiseAlarmE1"_event); break;
      case 6: Events::post("scene::GlobalAlarm"_event); break;
      case 7: data->spawned = scene.addObject("prefabs/DoorKit"_prefab, {50.0f, 0.0f, 0.0f}); break;
      case 10:
        if(auto *root = scene.getObjectById(data->spawned)) {
          Events::postTo(*root, "prefab::Plate.Pressed"_event); // reach into one spawned instance
        }
        break;
      case 11: Events::post("scene::Lockdown"_event); break;
      default: break;
    }
  }
}
