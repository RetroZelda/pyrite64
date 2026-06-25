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
