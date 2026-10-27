/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>
#include <t3d/t3dmath.h>

#include "renderer/particles/emitterDef.h"
#include "assets/assetManager.h"

namespace P64::PTX
{
  /**
   * Lightweight handle to a live emitter, modelled on Audio::Handle: a 4-byte {slot, uuid}
   * value. The uuid is a generation counter; once the slot is reused the handle is stale and
   * every method becomes a safe no-op. Copy-safe POD.
   */
  class Handle
  {
    private:
      uint16_t slot{0};
      uint16_t uuid{0};

    public:
      Handle() = default;
      explicit Handle(uint16_t s, uint16_t u) : slot{s}, uuid{u} {}

      [[nodiscard]] bool isValid() const;
      [[nodiscard]] bool isPlaying() const;

      // Graceful by default: stop spawning but let the live particles finish (keeps updating &
      // drawing until drained). immediate=true stops now (particles vanish this frame).
      // Kept alive if refCount>0 either way (replay() can restart it).
      void stop(bool immediate = false);
      void replay();  // restart emission from scratch and play
      // Release the owning reference (pairs with create()). Graceful by default: once the last
      // reference is gone the emitter stops spawning and drains its live particles before the
      // slot is reclaimed. immediate=true reclaims as soon as unreferenced (particles vanish).
      void destroy(bool immediate = false);
      void setPos(const fm_vec3_t &p);
      void move(const fm_vec3_t &delta);
      // SPACE_PARENT emitters: world position of the anchor (the owner's parent object)
      void setParentPos(const fm_vec3_t &p);
      // rotates the emission shape + base velocity about Y for future spawns
      void setYaw(float yawRad);
  };
}

/**
 * Global particle-emitter manager. Owns a fixed pool of live emitters and hands out
 * ref-counted handles. Two-list behaviour is achieved by branching on `playing`: update()
 * and draw() only touch playing slots; alive-but-idle slots (refCount>0, not playing) cost
 * nothing. Slots are reclaimed when not playing and refCount hits 0.
 *
 * stop()/destroy() are graceful by default: they stop the emitter spawning but keep it
 * `playing` so its live particles drain out normally; once finished() (no particles left) the
 * update loop clears `playing` and the slot is reclaimed. Pass immediate=true to skip the
 * drain and drop the particles at once.
 */
namespace P64::EmitterManager
{
  void init();
  void update(float deltaTime);
  // `camBaseSize` = camera-far-derived tpx base size (see DrawLayer::drawPtx); emitters
  // scale it per-instance for their normalized size bytes.
  void draw(float camBaseSize);
  // true if draw() would draw anything (a playing emitter)
  [[nodiscard]] bool hasDrawable();
  void destroyAll();

  // create(): preloaded & NOT playing, refCount=1 — the caller MUST destroy() it.
  PTX::Handle create(const PTX::EmitterDef* def);
  // play(): fire-and-forget, refCount=0 — one-shots self-reclaim; continuous needs stop()/destroy().
  PTX::Handle play(const PTX::EmitterDef* def);

  struct Metrics { uint16_t activeCount; uint16_t aliveCount; uint32_t reservedBytes; };
  Metrics getMetrics();

  // per-slot inspection for the debug overlay (Phase 4)
  uint32_t poolSize();
  bool slotInfo(uint32_t slot, bool &playing, uint16_t &refCount, uint32_t &alive, uint32_t &maxP, uint32_t &bytes);

  // Richer per-slot info for the debug "Particles" page (position + def for shape visualization).
  struct SlotDebugInfo {
    bool used{false};
    bool playing{false};
    fm_vec3_t pos{};
    const PTX::EmitterDef* def{nullptr};
    uint32_t aliveCount{0};
    uint32_t maxParticles{0};
  };
  SlotDebugInfo getSlotDebugInfo(uint32_t slot);

  extern uint64_t ticksUpdate; // CPU sim time of the last update(), for the CPU overlay
}

namespace P64
{
  // AssetRef<emitter_t> exposes create()/play() returning a handle (forwards to EmitterManager).
  // Same single-pointer layout as the generic AssetRef<T> so component data deserializes the same.
  template<>
  struct AssetRef<emitter_t>
  {
    emitter_t* ptr{};

    emitter_t* get()
    {
      if((uint32_t)ptr < 0xFFFF) {
        ptr = (emitter_t*)AssetManager::getByIndex((uint32_t)ptr);
      }
      return ptr;
    }

    PTX::Handle create();
    PTX::Handle play();
  };
}
