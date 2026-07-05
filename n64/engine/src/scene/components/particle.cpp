/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "scene/components/particle.h"
#include "scene/scene.h" // Object::getParent() instantiation (SPACE_PARENT anchor)

namespace
{
  // SPACE_PARENT anchor: the owner's parent object position; no parent -> world origin
  // (group 0 means parent-less, mirroring Scene's own parent handling).
  fm_vec3_t parentAnchor(P64::Object &obj)
  {
    P64::Object* par = obj.group ? obj.getParent() : nullptr;
    return par ? par->pos : fm_vec3_t{};
  }

  // yaw of the owner's facing (+Z forward), so directional emission shapes rotate with it
  float objectYaw(const P64::Object &obj)
  {
    const fm_quat_t &q = obj.rot;
    // rotate {0,0,1} by q, then take atan2 of the horizontal result
    const float fwdX = 2.0f * (q.x * q.z + q.w * q.y);
    const float fwdZ = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    return atan2f(fwdX, fwdZ);
  }

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
      // Release our owned reference. FLAG_FORCE_DESTROY drops the emitter (and its live
      // particles) at once; otherwise the manager keeps it alive to drain them first.
      data->handle.destroy(data->flags & FLAG_FORCE_DESTROY);
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
    data->handle.setYaw(objectYaw(obj)); // emission shape faces the object's heading
    data->parentSpace = data->emitter.get()->simSpace == PTX::ESPACE_PARENT;
    if (data->parentSpace) data->handle.setParentPos(parentAnchor(obj));
    if (data->flags & FLAG_AUTO_PLAY) {
      data->handle.replay(); // start emitting
    }
  }

  void Particle::update(Object &obj, Particle* data, [[maybe_unused]] float deltaTime)
  {
    data->handle.setPos(obj.pos); // follow the object (no-op on a stale/empty handle)
    data->handle.setYaw(objectYaw(obj)); // keep the emission shape facing the heading
    // SPACE_PARENT: particles anchor to the parent object, so feed its position too
    if (data->parentSpace) data->handle.setParentPos(parentAnchor(obj));
  }

  void Particle::onEvent(Object &obj, Particle* data, const ObjectEvent& event)
  {
    // auto-play emitters already started in initDelete(); only the ones waiting for an external
    // trigger (auto-play off) react to the sync event.
    if(event.type == EVENT_TYPE_PARTICLE_PLAY && !(data->flags & FLAG_AUTO_PLAY))
    {
      data->handle.setPos(obj.pos);
      data->handle.replay();
    }
  }
}
