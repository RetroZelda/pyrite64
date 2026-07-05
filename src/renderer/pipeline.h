/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <vector>
#include <SDL3/SDL.h>

#include "shader.h"

namespace Renderer
{
  class Pipeline
  {
    private:
      SDL_GPUGraphicsPipeline* pipeline{nullptr};

    public:
      struct InfoVertDef
      {
        SDL_GPUVertexElementFormat format{};
        uint32_t offset{};
      };

      struct Info
      {
        const Renderer::Shader &shader;
        SDL_GPUPrimitiveType prim{};
        bool useDepth{};
        bool drawsObjID{};
        bool translucent{};
        bool additive{};       // with translucent: additive blend (src*alpha + dst) instead of alpha
        bool noAlphaWrite{};   // mask the alpha channel (keeps an FB's alpha at 1 for compositing)
        bool depthWrite{true}; // write to the depth buffer (set false for alpha-blended particles)
        bool hasDepthTarget{true}; // false: render pass binds NO depth target (e.g. fullscreen composite)
        // color target 0 format (target 1, the objID buffer, is always R32_UINT).
        // Non-UNORM float formats don't clamp when blending (used by the additive-wrap accum pass).
        SDL_GPUTextureFormat colorFormat{SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
        uint32_t vertPitch{};
        std::vector<InfoVertDef> vertLayout{}; // empty: no vertex buffer (fullscreen gl_VertexIndex tri)
      };

      explicit Pipeline(const Info &info);
      ~Pipeline();

      // SDL_GPUGraphicsPipeline* getPipeline() const { return pipeline; }

      void bind(SDL_GPURenderPass* pass) const {
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
      }
  };
}
