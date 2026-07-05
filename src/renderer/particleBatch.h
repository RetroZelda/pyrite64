/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <SDL3/SDL.h>
#include <vector>

#include "vertex.h" // Renderer::ParticleVertex

namespace Renderer
{
  class Scene;
  class Texture;
  class Pipeline;
  struct UniformGlobal;

  // One emitter's slice of a billboard batch plus the hardware-faithful render state the
  // previews copy from the material (blend mode, filter, mirror addressing, alpha-clip).
  // The texture is resolved and drawn within the same frame — it never dangles.
  struct ParticleDrawGroup
  {
    Texture* tex;        // null -> 1px white (flat-colored, untextured emitter)
    uint32_t firstVert;
    uint32_t vertCount;
    ParticleBlend blend;
    bool mirror;
    bool bilinear;
    float alphaThreshold;
  };

  // Additive groups need the RDP's non-clamping (wraparound) add, which GPU blending can't do
  // in-pass: they are SKIPPED in the main pass and re-drawn by ParticleWrapPass afterwards.
  enum class ParticleGroupFilter : uint8_t { All, SkipAdditive, OnlyAdditive };

  // Draws grouped billboard batches with the matching particle pipeline/sampler per group.
  // Shared by the scene viewport and the emitter-preview tab so both composite identically.
  // `pipelineOverride` forces one pipeline for every drawn group (used by the accum pass).
  void drawParticleGroups(SDL_GPUCommandBuffer* cmdBuff, SDL_GPURenderPass* pass, Scene& scene,
    const class ParticleBatch& batch, const UniformGlobal& uniGlobal,
    const std::vector<ParticleDrawGroup>& groups,
    ParticleGroupFilter filter = ParticleGroupFilter::All,
    const Pipeline* pipelineOverride = nullptr);

  /**
   * A reusable GPU vertex buffer for per-frame particle-billboard batches. It allocates once and
   * only GROWS (never shrinks / reallocates per frame), so a steady particle count never churns
   * the SDL GPU allocator (which previously triggered a defrag crash on per-frame reallocations).
   * Upload happens in a copy pass; bind+draw in a render pass (unindexed TRIANGLELIST).
   */
  class ParticleBatch
  {
    private:
      SDL_GPUDevice* gpuDevice{nullptr};
      SDL_GPUBuffer* buffer{nullptr};
      SDL_GPUTransferBuffer* bufferTrans{nullptr};
      size_t currVertByteSize{0};   // bytes of live vertex data this frame
      size_t allocVertByteSize{0};  // allocated capacity (>= currVertByteSize)
      bool needsUpload{false};
      static constexpr float CAPACITY_HEADROOM = 1.5f; // grow with slack to avoid frequent resizes

      void resize(uint32_t requestedSize);

    public:
      explicit ParticleBatch(SDL_GPUDevice* device) : gpuDevice{device} {}
      ~ParticleBatch();

      // Stage `verts` for upload. Only reallocates the GPU buffer when the data exceeds capacity.
      void setVertices(const std::vector<ParticleVertex> &verts);
      // Copy the staged data to the GPU buffer (call inside a copy pass).
      void upload(SDL_GPUCopyPass* pass);

      void bind(SDL_GPURenderPass* pass) const {
        if(!buffer) return;
        SDL_GPUBufferBinding binding{};
        binding.buffer = buffer;
        binding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
      }

      [[nodiscard]] uint32_t getVertexCount() const { return (uint32_t)(currVertByteSize / sizeof(ParticleVertex)); }
      [[nodiscard]] bool isEmpty() const { return currVertByteSize == 0; }
      [[nodiscard]] bool isReady() const { return buffer != nullptr; }
      void clear() { currVertByteSize = 0; needsUpload = false; }
  };
}
