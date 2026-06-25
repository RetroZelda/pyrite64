/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <t3d/t3d.h>
#include <t3d/tpx.h>

#include "renderer/particles/ptxSystem.h"
#include "renderer/particles/emitterDef.h"
#include "renderer/particles/particleSim.h"

namespace P64::PTX
{
  /**
   * A single live particle emitter. Owns a pre-allocated PTX::System (TEX_A_S16) sized to
   * the def's maxParticles, an uncached transform matrix (pushed via the tpx matrix stack
   * so particles live in LOCAL space and the whole emitter is cheap to move), a captured
   * rspq setup block built from the def's material/blend settings, and CPU-cached
   * struct-of-arrays sim state driven by the shared P64::ParticleSim.
   *
   * The uncached tpx buffer is WRITE-ONLY from the sim (never read back).
   */
  class EmitterInstance
  {
    private:
      const EmitterDef* def{nullptr};
      System* system{nullptr};            // TEX_A_S16, maxParticles
      rspq_block_t* setupDPL{nullptr};
      T3DMat4FP* mat{nullptr};            // local->world transform (uncached)
      ParticleSim::Particle* parts{nullptr}; // cached SoA sim state, sized maxParticles
      uint16_t* drawOrder{nullptr};       // back-to-front particle indices, sized maxParticles
      sprite_t** texSprites{nullptr};     // preloaded swap textures (texCount)

      ParticleSim::Config cfg{};
      ParticleSim::Rng rng{};
      float spawnAccum{0.0f};
      float emitterTime{0.0f};
      bool oneShotPending{true};
      uint32_t lastSwapIdx{0xFFFF};

      fm_vec3_t pos{};                    // emitter world position

      void buildSetupBlock();

    public:
      void init(const EmitterDef* d, uint32_t seed);
      void teardown();
      void reset(); // restart the emission from scratch (keeps allocations)

      // `camPos`/`haveCam` let update() sort live particles back-to-front (matches the editor
      // preview) so overlapping transparent particles layer correctly; pass haveCam=false to skip.
      void update(float dt, const fm_vec3_t &camPos, bool haveCam);
      void draw();

      void setPos(const fm_vec3_t &p) { pos = p; }
      void move(const fm_vec3_t &delta) { pos.x += delta.x; pos.y += delta.y; pos.z += delta.z; }

      [[nodiscard]] bool finished() const; // one-shot drained
      [[nodiscard]] uint32_t aliveCount() const { return system ? system->count : 0; }
      [[nodiscard]] uint32_t maxParticles() const { return def ? def->maxParticles : 0; }
      [[nodiscard]] const EmitterDef* getDef() const { return def; }
      [[nodiscard]] fm_vec3_t getPos() const { return pos; } // emitter world position (debug)
      [[nodiscard]] uint32_t reservedBytes() const;
  };
}
