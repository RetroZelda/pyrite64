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

      void stop();    // stop emitting/drawing (kept alive if refCount>0)
      void replay();  // restart emission from scratch and play
      void destroy(); // release the owning reference (pairs with create())
      void setPos(const fm_vec3_t &p);
      void move(const fm_vec3_t &delta);
  };
}

/**
 * Global particle-emitter manager. Owns a fixed pool of live emitters and hands out
 * ref-counted handles. Two-list behaviour is achieved by branching on `playing`: update()
 * and draw() only touch playing slots; alive-but-idle slots (refCount>0, not playing) cost
 * nothing. Slots are reclaimed when not playing and refCount hits 0.
 */
namespace P64::EmitterManager
{
  void init();
  void update(float deltaTime);
  void draw();
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
