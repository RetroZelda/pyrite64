/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "scene/components/particle.h"

namespace
{
  // binary layout written by the editor's Component::Particle::build()
  struct InitData
  {
    uint16_t emitterIdx;
    uint8_t flags;
    uint8_t padding;
  };
}

namespace P64::Comp
{
  void Particle::initDelete(Object &obj, Particle* data, uint16_t* initData_)
  {
    auto initData = (InitData*)initData_;
    if (initData == nullptr) {
      data->handle.destroy(); // release our owned reference (manager reclaims it)
      data->~Particle();
      return;
    }

    new(data) Particle();
    data->flags = initData->flags;

    if (initData->emitterIdx == 0xFFFF) return; // no emitter assigned

    // AssetRef stores the asset index in its pointer until first get() resolves it.
    data->emitter.ptr = (emitter_t*)(uintptr_t)initData->emitterIdx;
    data->handle = data->emitter.create(); // owned by this component
    data->handle.setPos(obj.pos);
    if (data->flags & FLAG_AUTO_PLAY) {
      data->handle.replay(); // start emitting
    }
  }

  void Particle::update(Object &obj, Particle* data, [[maybe_unused]] float deltaTime)
  {
    data->handle.setPos(obj.pos); // follow the object (no-op on a stale/empty handle)
  }
}
