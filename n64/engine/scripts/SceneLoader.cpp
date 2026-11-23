/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Scene Loader. Load switches to the scene set here, or, if that is 0, to
// the scene id the event carries as its value.
#include "script/userScript.h"

namespace P64::Script::C0E000000000000D
{
  P64_DATA(
    [[P64::Name("Scene Id")]]
    uint16_t sceneId = 0;
    uint16_t pad;
  );

  P64_EVENTS_IN(Load);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    const uint16_t id = data->sceneId ? data->sceneId : (uint16_t)ev.value;
    if(id) SceneManager::load(id);
  }
}
