/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Spawner. Spawn creates the prefab at this object's position and fires
// Spawned with the new object's id. DespawnAll and Reset remove the objects it spawned (up to 8
// are remembered).
#include "script/userScript.h"

namespace P64::Script::C0E000000000000E
{
  constexpr uint32_t MAX_TRACKED = 8;

  P64_DATA(
    [[P64::Name("Prefab")]]
    PrefabRef prefab;
    [[P64::Name("As Child")]]
    bool asChild = false;
    uint8_t count;
    uint16_t pad;
    // the editor only sizes char arrays, so the remembered ids are separate fields
    uint16_t spawned0;
    uint16_t spawned1;
    uint16_t spawned2;
    uint16_t spawned3;
    uint16_t spawned4;
    uint16_t spawned5;
    uint16_t spawned6;
    uint16_t spawned7;
  );

  uint16_t* spawned(Data *data) { return &data->spawned0; }

  P64_EVENTS_IN(Spawn, DespawnAll, Reset);
  P64_EVENTS_OUT(Spawned);

  void init(Object& obj, Data *data)
  {
    data->count = 0;
  }

  void despawnAll(Data *data)
  {
    auto &scene = SceneManager::getCurrent();
    for(uint32_t i = 0; i < data->count; ++i) {
      if(auto *child = scene.getObjectById(spawned(data)[i])) child->remove();
    }
    data->count = 0;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    if(input != In::Spawn) {
      despawnAll(data);
      return;
    }
    auto &scene = SceneManager::getCurrent();
    const uint16_t id = scene.addObject(data->prefab.get(), obj.pos, {1, 1, 1}, obj.rot, data->asChild ? obj.id : 0);
    if(data->count < MAX_TRACKED) spawned(data)[data->count++] = id;
    fire(obj, data, Out::Spawned, id);
  }
}
