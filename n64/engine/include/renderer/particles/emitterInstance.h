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
   * A single live particle emitter. Owns a pre-allocated PTX::System (TEX_A_S16, or
   * COLOR_A_S16 when the def has no usable texture) sized to the def's maxParticles,
   * an uncached transform matrix (pushed via the tpx matrix stack
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
      uint32_t fillEmitted{0};    // FIRE_FILL: running total spawned since reset (caps the fill)
      bool oneShotPending{true};
      bool emitting{true};        // false: stopped spawning, existing particles drain then finished()
      bool textured{false};       // any resolvable texture -> TEX_A_S16 draw path
      float tint[4]{1, 1, 1, 1};  // material prim color, folded into per-particle colors
      uint32_t lastSwapIdx{0xFFFF};

      fm_vec3_t pos{};                    // emitter world position
      fm_vec3_t parentPos{};              // SPACE_PARENT anchor (fed by the owning component)

      void buildSetupBlock();

    public:
      void init(const EmitterDef* d, uint32_t seed);
      void teardown();
      void reset(); // restart the emission from scratch (keeps allocations)

      // Graceful stop: stop spawning new particles but keep updating/drawing the live ones
      // until they all die. Once drained, finished() reports true so the manager reclaims it.
      void stopEmitting() { emitting = false; }
      [[nodiscard]] bool isEmitting() const { return emitting; }

      // `camPos`/`haveCam` let update() sort live particles back-to-front (matches the editor
      // preview) so overlapping transparent particles layer correctly; pass haveCam=false to skip.
      void update(float dt, const fm_vec3_t &camPos, bool haveCam);
      // `camBaseSize` is the camera-far-derived tpx base size (see DrawLayer::drawPtx); each
      // emitter scales it by its size-normalization factor before drawing.
      void draw(float camBaseSize);

      void setPos(const fm_vec3_t &p) { pos = p; }
      void move(const fm_vec3_t &delta) { pos.x += delta.x; pos.y += delta.y; pos.z += delta.z; }
      // SPACE_PARENT emitters: position of the anchor object (typically the owner's parent).
      // Ignored in the other sim spaces.
      void setParentPos(const fm_vec3_t &p) { parentPos = p; }
      // Rotates the emission shape + base velocity about Y for FUTURE spawns, so directional
      // emitters (e.g. a wide wave) can face their movement. Live particles are unaffected.
      void setYaw(float yawRad) { cfg.yawSin = sinf(yawRad); cfg.yawCos = cosf(yawRad); }

      [[nodiscard]] bool finished() const; // no live particles left and no more will spawn
      [[nodiscard]] uint32_t aliveCount() const { return system ? system->count : 0; }
      [[nodiscard]] uint32_t maxParticles() const { return def ? def->maxParticles : 0; }
      [[nodiscard]] const EmitterDef* getDef() const { return def; }
      [[nodiscard]] fm_vec3_t getPos() const { return pos; } // emitter world position (debug)
      [[nodiscard]] uint32_t reservedBytes() const;
  };
}
