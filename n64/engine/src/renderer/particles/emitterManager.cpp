/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "renderer/particles/emitterManager.h"
#include "renderer/particles/emitterInstance.h"
#include "scene/sceneManager.h"
#include "scene/scene.h"
#include "scene/camera.h"

#include <libdragon.h>
#include <array>

namespace
{
  constexpr uint32_t POOL = 16;

  struct Slot
  {
    P64::PTX::EmitterInstance inst{};
    uint16_t uuid{0};
    uint16_t refCount{0};
    bool used{false};
    bool playing{false};
  };

  std::array<Slot, POOL> slots{};
  constinit uint16_t nextUUID = 1;

  Slot* validSlot(uint16_t slot, uint16_t uuid)
  {
    if(slot >= POOL) return nullptr;
    Slot &s = slots[slot];
    return (s.used && s.uuid == uuid && uuid != 0) ? &s : nullptr;
  }

  // Allocates a free slot for `def`. Returns nullptr if the pool is full.
  Slot* alloc(const P64::PTX::EmitterDef* def, uint16_t &outSlot, uint16_t &outUUID)
  {
    if(!def) return nullptr;
    for(uint32_t i = 0; i < POOL; ++i) {
      if(!slots[i].used) {
        Slot &s = slots[i];
        s.used = true;
        s.uuid = nextUUID++;
        if(nextUUID == 0) nextUUID = 1; // skip 0 (reserved "invalid")
        s.refCount = 0;
        s.playing = false;
        s.inst.init(def, (uint32_t)s.uuid * 0x9E3779B9u);
        outSlot = (uint16_t)i;
        outUUID = s.uuid;
        return &s;
      }
    }
    return nullptr;
  }

  void freeSlot(Slot &s)
  {
    s.inst.teardown();
    s.used = false;
    s.playing = false;
    s.refCount = 0;
    s.uuid = 0;
  }
}

uint64_t P64::EmitterManager::ticksUpdate = 0;

void P64::EmitterManager::init()
{
  for(auto &s : slots) { s = Slot{}; }
  nextUUID = 1;
}

P64::PTX::Handle P64::EmitterManager::create(const PTX::EmitterDef* def)
{
  uint16_t slot, uuid;
  Slot* s = alloc(def, slot, uuid);
  if(!s) return PTX::Handle{};
  s->refCount = 1;     // owned — caller must destroy()
  s->playing = false;  // preloaded, not yet emitting/drawing
  return PTX::Handle{slot, uuid};
}

P64::PTX::Handle P64::EmitterManager::play(const PTX::EmitterDef* def)
{
  uint16_t slot, uuid;
  Slot* s = alloc(def, slot, uuid);
  if(!s) return PTX::Handle{};
  s->refCount = 0;     // fire-and-forget
  s->playing = true;
  return PTX::Handle{slot, uuid};
}

void P64::EmitterManager::update(float deltaTime)
{
  uint64_t t0 = get_ticks();

  // lazy cleanup: reclaim idle, unreferenced slots
  for(auto &s : slots) {
    if(s.used && !s.playing && s.refCount == 0) freeSlot(s);
  }

  // active camera (for back-to-front particle sorting); may be absent (no camera in scene)
  Camera* cam = SceneManager::getCurrent().getActiveCameraPtr();
  fm_vec3_t camPos{};
  bool haveCam = false;
  if(cam) { camPos = cam->getPos(); haveCam = true; }

  // simulate only playing slots; a finished one-shot stops (reclaimed next pass if refCount==0)
  for(auto &s : slots) {
    if(s.used && s.playing) {
      s.inst.update(deltaTime, camPos, haveCam);
      if(s.inst.finished()) s.playing = false;
    }
  }

  ticksUpdate = get_ticks() - t0;
}

void P64::EmitterManager::draw()
{
  for(auto &s : slots) {
    if(s.used && s.playing) s.inst.draw();
  }
}

void P64::EmitterManager::destroyAll()
{
  for(auto &s : slots) {
    if(s.used) freeSlot(s);
  }
}

P64::EmitterManager::Metrics P64::EmitterManager::getMetrics()
{
  Metrics m{};
  for(auto &s : slots) {
    if(!s.used) continue;
    m.aliveCount++;
    if(s.playing) m.activeCount++;
    m.reservedBytes += s.inst.reservedBytes();
  }
  return m;
}

uint32_t P64::EmitterManager::poolSize() { return POOL; }

bool P64::EmitterManager::slotInfo(uint32_t slot, bool &playing, uint16_t &refCount,
                                   uint32_t &alive, uint32_t &maxP, uint32_t &bytes)
{
  if(slot >= POOL || !slots[slot].used) return false;
  Slot &s = slots[slot];
  playing = s.playing;
  refCount = s.refCount;
  alive = s.inst.aliveCount();
  maxP = s.inst.maxParticles();
  bytes = s.inst.reservedBytes();
  return true;
}

P64::EmitterManager::SlotDebugInfo P64::EmitterManager::getSlotDebugInfo(uint32_t slot)
{
  SlotDebugInfo info{};
  if(slot >= POOL) return info;
  Slot &s = slots[slot];
  info.used = s.used;
  info.playing = s.playing;
  if(s.used) {
    info.pos = s.inst.getPos();
    info.def = s.inst.getDef();
    info.aliveCount = s.inst.aliveCount();
    info.maxParticles = s.inst.maxParticles();
  }
  return info;
}

// ---- Handle ----

bool P64::PTX::Handle::isValid() const { return validSlot(slot, uuid) != nullptr; }

bool P64::PTX::Handle::isPlaying() const
{
  Slot* s = validSlot(slot, uuid);
  return s && s->playing;
}

void P64::PTX::Handle::stop()
{
  if(Slot* s = validSlot(slot, uuid)) s->playing = false;
}

void P64::PTX::Handle::replay()
{
  if(Slot* s = validSlot(slot, uuid)) { s->inst.reset(); s->playing = true; }
}

void P64::PTX::Handle::destroy()
{
  Slot* s = validSlot(slot, uuid);
  if(!s) return;
  if(s->refCount > 0) s->refCount--;
  // A continuous emitter never finishes on its own; once unreferenced, stop it so the
  // lazy-cleanup pass can reclaim it (one-shots are left to finish naturally).
  if(s->refCount == 0 && s->playing && s->inst.getDef()
     && s->inst.getDef()->fireMode == EMIT_CONTINUOUS) {
    s->playing = false;
  }
}

void P64::PTX::Handle::setPos(const fm_vec3_t &p)
{
  if(Slot* s = validSlot(slot, uuid)) s->inst.setPos(p);
}

void P64::PTX::Handle::move(const fm_vec3_t &delta)
{
  if(Slot* s = validSlot(slot, uuid)) s->inst.move(delta);
}

// ---- AssetRef<emitter_t> convenience ----

P64::PTX::Handle P64::AssetRef<emitter_t>::create() { return EmitterManager::create(get()); }
P64::PTX::Handle P64::AssetRef<emitter_t>::play()   { return EmitterManager::play(get()); }
