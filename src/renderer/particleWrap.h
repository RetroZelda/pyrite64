/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <SDL3/SDL.h>
#include <vector>

namespace Renderer
{
  class Scene;
  class Framebuffer;
  class ParticleBatch;
  struct UniformGlobal;
  struct ParticleDrawGroup;

  /**
   * Composites ADDITIVE particle groups with the RDP's real (non-clamping) blender math.
   *
   * GPU blending saturates at 1.0, but the N64 blender's adder drops the carry: any channel
   * sum past 255 wraps around (mod 256) — that is why naive additive looks broken on hardware.
   * Additive contributions are pure sums and addition mod 256 is associative, so the exact
   * hardware result is reproduced by:
   *   1. snapshotting the scene color (everything rendered so far),
   *   2. summing the additive groups into a float target (float attachments don't clamp),
   *   3. a fullscreen composite writing mod(scene + sum, 256) back into the viewport.
   * Runs AFTER the viewport's main render pass ended; a no-op without additive groups.
   *
   * Owns the two per-viewport textures (created lazily on first additive use, recreated only
   * when the framebuffer size changes — same allocator-churn caveat as the framebuffers).
   *
   * Known approximation: additive groups draw after ALL other particle groups, so an
   * alpha-blended emitter overlapping an additive one composites in a different order than
   * the engine's per-emitter draw order. Within additive content the result is exact.
   */
  class ParticleWrapPass
  {
    private:
      SDL_GPUTexture* texAccum{nullptr};     // float sum of additive contributions
      SDL_GPUTexture* texSceneCopy{nullptr}; // scene snapshot read by the composite
      uint32_t width{0}, height{0};

      void ensureSize(const Scene& scene, uint32_t w, uint32_t h);

    public:
      ~ParticleWrapPass();

      // Draws the additive groups in `groups` over fb with hardware wraparound.
      // Call AFTER the main render pass ended (the non-additive groups must already be drawn,
      // additive ones skipped via ParticleGroupFilter::SkipAdditive).
      void run(SDL_GPUCommandBuffer* cmdBuff, Scene& scene, Framebuffer& fb,
        const ParticleBatch& batch, const UniformGlobal& uniGlobal,
        const std::vector<ParticleDrawGroup>& groups);
  };
}
