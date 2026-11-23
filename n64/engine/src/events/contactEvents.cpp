/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph, see contactEvents.h.
#include "contactEvents.h"

#include <vector>

#include "events/bus.h"
#include "events/pins.h"
#include "scene/object.h"
#include "scene/objectFlags.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "scene/components/code.h"
#include "scene/components/collBody.h"
#include "scene/components/collMesh.h"
#include "scene/components/rigidBody.h"

namespace
{
  using P64::Object;

  struct Pair
  {
    const void* self;   // the self side's Collider* or MeshCollider*
    Object* selfObj;
    uint16_t otherId;
    uint8_t seen;
    uint8_t trigger;
    uint8_t isMesh;
  };

  struct Change
  {
    const void* self;
    Object* selfObj;
    uint16_t otherId;
    uint8_t isMesh;
    bool enter;
  };

  std::vector<Pair> pairs{};
  std::vector<Change> changes{};

  uint32_t countFor(const void* self)
  {
    uint32_t n = 0;
    for(const auto &p : pairs) if(p.self == self) ++n;
    return n;
  }

  bool isAsleep(Object* obj)
  {
    if(!obj) return false;
    auto *rb = obj->getComponent<P64::Comp::RigidBody>();
    return rb && rb->getBody().isSleeping();
  }

  // the Collider / Collision-Mesh component data that owns 'self' on 'obj'
  void* colliderComponent(Object &obj, const void* self, bool isMesh)
  {
    auto refs = obj.getCompRefs();
    for(uint32_t c = 0; c < obj.compCount; ++c) {
      char* data = reinterpret_cast<char*>(&obj) + refs[c].offset;
      if(!isMesh && refs[c].type == P64::Comp::CollBody::ID) {
        if(&reinterpret_cast<P64::Comp::CollBody*>(data)->collider == self) return data;
      }
      if(isMesh && refs[c].type == P64::Comp::CollMesh::ID) {
        if(reinterpret_cast<P64::Comp::CollMesh*>(data)->meshCollider == self) return data;
      }
    }
    return nullptr;
  }

  void report(const Change &change, uint32_t countBefore, uint32_t countAfter)
  {
    using P64::Events::Pins::ColliderOut;
    Object &obj = *change.selfObj;

    if(void* comp = colliderComponent(obj, change.self, change.isMesh))
    {
      const auto pin = change.enter ? ColliderOut::Enter : ColliderOut::Exit;
      P64::Events::fire(obj, comp, static_cast<uint8_t>(pin), change.otherId, change.otherId);
      if(change.enter && countBefore == 0 && countAfter > 0) {
        P64::Events::fire(obj, comp, static_cast<uint8_t>(ColliderOut::Occupied), change.otherId, change.otherId);
      }
      if(!change.enter && countBefore > 0 && countAfter == 0) {
        P64::Events::fire(obj, comp, static_cast<uint8_t>(ColliderOut::Emptied), change.otherId, change.otherId);
      }
    }

    // script callbacks
    auto refs = obj.getCompRefs();
    for(uint32_t c = 0; c < obj.compCount; ++c) {
      if(refs[c].type != P64::Comp::Code::ID) continue;
      auto *code = reinterpret_cast<P64::Comp::Code*>(reinterpret_cast<char*>(&obj) + refs[c].offset);
      if(change.enter) P64::Comp::Code::onCollEnter(obj, code, change.otherId);
      else P64::Comp::Code::onCollExit(obj, code, change.otherId);
    }
  }
}

void P64::Events::Contacts::beginStep()
{
  for(auto &p : pairs) p.seen = 0;
  changes.clear();
}

void P64::Events::Contacts::touch(Coll::Collider* selfCollider, Coll::MeshCollider* selfMesh, Object& self, Object& other, bool trigger)
{
  if(!(self.flags & ObjectFlags::WANTS_CONTACTS)) return;
  const void* key = selfCollider ? static_cast<const void*>(selfCollider) : static_cast<const void*>(selfMesh);
  if(!key) return;

  for(auto &p : pairs) {
    if(p.self == key && p.otherId == other.id) {
      p.seen = 1;
      return;
    }
  }
  const uint8_t isMesh = selfCollider ? 0 : 1;
  pairs.push_back({key, &self, other.id, 1, static_cast<uint8_t>(trigger ? 1 : 0), isMesh});
  changes.push_back({key, &self, other.id, isMesh, true});
}

void P64::Events::Contacts::endStep()
{
  auto &scene = SceneManager::getCurrent();

  // pairs not touched this step ended, except solid contacts between sleeping bodies: sleeping
  // bodies stop producing contacts, but they still touch
  for(size_t i = 0; i < pairs.size();)
  {
    auto &p = pairs[i];
    if(p.seen) { ++i; continue; }

    Object* other = scene.getObjectById(p.otherId);
    const bool selfAsleep = isAsleep(p.selfObj);
    const bool otherAsleep = isAsleep(other);
    const bool anyBody = p.selfObj->getComponent<Comp::RigidBody>() || (other && other->getComponent<Comp::RigidBody>());
    const bool sleepingContact = !p.trigger && other && anyBody
      && (selfAsleep || !p.selfObj->getComponent<Comp::RigidBody>())
      && (otherAsleep || !other->getComponent<Comp::RigidBody>());
    if(sleepingContact) { ++i; continue; }

    changes.push_back({p.self, p.selfObj, p.otherId, p.isMesh, false});
    pairs[i] = pairs.back();
    pairs.pop_back();
  }

  if(changes.empty()) return;

  // occupancy per collider, before and after this step
  for(const auto &change : changes)
  {
    const uint32_t after = countFor(change.self);
    uint32_t before = after;
    for(const auto &other : changes) {
      if(other.self != change.self) continue;
      if(other.enter) --before; else ++before;
    }
    // Occupied / Emptied fire once per step and collider: on the first enter, or the last exit
    bool firstOfKind = true;
    for(const auto &other : changes) {
      if(&other == &change) break;
      if(other.self == change.self && other.enter == change.enter) { firstOfKind = false; break; }
    }
    report(change, firstOfKind ? before : 1, firstOfKind ? after : 1);
  }
  changes.clear();
}

void P64::Events::Contacts::forget(const void* collider)
{
  for(size_t i = 0; i < pairs.size();) {
    if(pairs[i].self == collider) {
      pairs[i] = pairs.back();
      pairs.pop_back();
    } else {
      ++i;
    }
  }
}

void P64::Events::Contacts::reset()
{
  pairs.clear();
  changes.clear();
}
