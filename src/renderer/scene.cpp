/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "scene.h"
#include <SDL3/SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include "../context.h"

#include "framebuffer.h"
#include "shader.h"

Renderer::Scene::Scene()
{
  shaderN64 = std::make_unique<Shader>(ctx.gpu, Shader::Config{
    .name = "n64",
    .vertUboCount = 2,
    .fragUboCount = 1,
    .vertTexCount = 0,
    .fragTexCount = 2,
    .vertSboCount = 1,
  });
  shaderLines = std::make_unique<Shader>(ctx.gpu, Shader::Config{
    .name = "lines",
    .vertUboCount = 2,
    .fragUboCount = 0,
    .vertTexCount = 0,
    .fragTexCount = 0,
  });
  shaderSprites = std::make_unique<Shader>(ctx.gpu, Shader::Config{
    .name = "sprites",
    .vertUboCount = 2,
    .fragUboCount = 0,
    .vertTexCount = 0,
    .fragTexCount = 1,
  });

  for(int i = 0; i < (int)pipelineN64.size(); ++i)
  {
  pipelineN64[i] = std::make_unique<Pipeline>(Pipeline::Info{
    .shader = *shaderN64,
    .prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
    .depthTest = (i & 0b11) != 0,
    .depthWrite = (i & 0b10) != 0,
    .depthCompare = (i & 0b01) ? SDL_GPU_COMPAREOP_LESS : SDL_GPU_COMPAREOP_ALWAYS,
    .drawsObjID = true,
    .translucent = true,
    .vertPitch = sizeof(Vertex),
    .vertLayout = {
      {SDL_GPU_VERTEXELEMENTFORMAT_SHORT4     , offsetof(Renderer::Vertex, pos)},
      {SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(Renderer::Vertex, color)},
      {SDL_GPU_VERTEXELEMENTFORMAT_SHORT2    ,  offsetof(Renderer::Vertex, uv)},
      {SDL_GPU_VERTEXELEMENTFORMAT_SHORT2    ,  offsetof(Renderer::Vertex, boneIdx)},
    }
  });
  }

  pipelineLines = std::make_unique<Pipeline>(Pipeline::Info{
    .shader = *shaderLines,
    .prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
    .drawsObjID = false,
    .vertPitch = sizeof(LineVertex),
    .vertLayout = {
      {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3     , offsetof(Renderer::LineVertex, pos)},
      {SDL_GPU_VERTEXELEMENTFORMAT_UINT       , offsetof(Renderer::LineVertex, objectId)},
      {SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(Renderer::LineVertex, color)},
    }
  });

  pipelineSprites = std::make_unique<Pipeline>(Pipeline::Info{
  .shader = *shaderSprites,
  .prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
  .drawsObjID = true,
  .vertPitch = sizeof(LineVertex),
  .vertLayout = {
    {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3     , offsetof(Renderer::LineVertex, pos)},
    {SDL_GPU_VERTEXELEMENTFORMAT_UINT       , offsetof(Renderer::LineVertex, objectId)},
    {SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(Renderer::LineVertex, color)},
  }
});

  // World-space textured billboards for live particle previews in the editor viewport.
  // Depth-TEST against scene geometry (occlusion) but no depth-WRITE + alpha blend, so
  // overlapping transparent particles don't self-reject — matches the engine's particle pass.
  shaderParticles = std::make_unique<Shader>(ctx.gpu, Shader::Config{
    .name = "particles",
    .vertUboCount = 1,
    .fragUboCount = 1, // alpha-compare threshold (matches rdpq_mode_alphacompare on hardware)
    .vertTexCount = 0,
    .fragTexCount = 1,
    .vertSboCount = 0,
  });
  // One pipeline per RDP blend mode the emitter material can pick (None/Multiply/Additive),
  // all sharing shaders, vertex layout and the depth-test-but-no-write particle behavior.
  Pipeline::Info particleInfo{
    .shader = *shaderParticles,
    .prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
    .depthWrite = false,  // test against geometry, but don't write (alpha-blended particles)
    .drawsObjID = true,   // 2 color targets, matching the viewport's render pass
    .translucent = true,  // alpha blend
    .vertPitch = sizeof(Renderer::ParticleVertex),
    .vertLayout = {
      {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3     , offsetof(Renderer::ParticleVertex, pos)},
      {SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(Renderer::ParticleVertex, color)},
      {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2     , offsetof(Renderer::ParticleVertex, uv)},
    }
  };
  pipelineParticles = std::make_unique<Pipeline>(particleInfo);
  particleInfo.additive = true;
  pipelineParticlesAdd = std::make_unique<Pipeline>(particleInfo);
  particleInfo.additive = false;
  particleInfo.translucent = false;
  particleInfo.noAlphaWrite = true; // keep FB alpha at 1 so ImGui composites the view opaquely
  pipelineParticlesOpaque = std::make_unique<Pipeline>(particleInfo);

  // Wrap-accurate additive blending (ParticleWrapPass): the RDP blender's adder has NO clamp —
  // channel sums past 255 drop the carry and wrap around, while GPU blending saturates. So
  // additive groups are SUMMED into a float color target instead (float attachments don't clamp
  // on blend), and a fullscreen composite folds mod(scene + sum, 256) back into the viewport.
  accumFormat = SDL_GPUTextureSupportsFormat(ctx.gpu, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
      SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
    ? SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT   // integer-exact sums for any particle count
    : SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;  // universal fallback (tiny error in extreme stacks)
  particleInfo.translucent = true;
  particleInfo.additive = true;
  particleInfo.noAlphaWrite = false;
  particleInfo.colorFormat = accumFormat;
  pipelineParticlesAccum = std::make_unique<Pipeline>(particleInfo);

  shaderPtxWrap = std::make_unique<Shader>(ctx.gpu, Shader::Config{
    .name = "ptxWrap",
    .vertUboCount = 0,
    .fragUboCount = 0,
    .vertTexCount = 0,
    .fragTexCount = 2, // scene copy + additive sum
  });
  pipelinePtxWrapComposite = std::make_unique<Pipeline>(Pipeline::Info{
    .shader = *shaderPtxWrap,
    .prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
    .drawsObjID = false,
    .translucent = false,     // straight overwrite; the shader carries the scene through
    .hasDepthTarget = false,  // fullscreen pass, no depth bound
    .vertLayout = {},         // fullscreen triangle from gl_VertexIndex
  });

  // Samplers covering the RDP texture state emitters can author: point/bilinear filtering ×
  // infinite-repeat/mirrored-repeat addressing (mirrored-repeat reproduces the tpx mirror trick).
  for(int i = 0; i < 4; ++i) {
    const bool mirror = (i & 2) != 0, bilinear = (i & 1) != 0;
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = bilinear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    si.mag_filter = si.min_filter;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = mirror ? SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_v = si.address_mode_u;
    si.address_mode_w = si.address_mode_u;
    particleSamplers[i] = SDL_CreateGPUSampler(ctx.gpu, &si);
  }

  // 1x1 white texture so untextured emitters render flat per-particle color, like the
  // engine's COLOR_A_S16 path (texCol becomes 1.0 in the shared particle shader).
  {
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = 1; ti.height = 1;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    whiteTex = SDL_CreateGPUTexture(ctx.gpu, &ti);

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = 4;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(ctx.gpu, &tbi);
    auto* px = (uint8_t*)SDL_MapGPUTransferBuffer(ctx.gpu, tb, false);
    px[0] = px[1] = px[2] = px[3] = 0xFF;
    SDL_UnmapGPUTransferBuffer(ctx.gpu, tb);

    SDL_GPUTextureTransferInfo transInfo{};
    transInfo.transfer_buffer = tb;
    SDL_GPUTextureRegion region{};
    region.texture = whiteTex;
    region.w = 1; region.h = 1; region.d = 1;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx.gpu);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(copyPass, &transInfo, &region, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(ctx.gpu, tb);
  }
}

Renderer::Scene::~Scene() {
  for(auto* &sampler : particleSamplers) {
    if(sampler) { SDL_ReleaseGPUSampler(ctx.gpu, sampler); sampler = nullptr; }
  }
  if(whiteTex) { SDL_ReleaseGPUTexture(ctx.gpu, whiteTex); whiteTex = nullptr; }
}

void Renderer::Scene::update()
{
}

void Renderer::Scene::draw()
{
  const auto drawData = ImGui::GetDrawData();
  const bool isMinimized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);

  SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(ctx.gpu); // Acquire a GPU command buffer

  SDL_GPUTexture* swapTex{};
  SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, ctx.window, &swapTex, nullptr, nullptr); // Acquire a swapchain texture

  if(swapTex == nullptr || isMinimized) {
    SDL_SubmitGPUCommandBuffer(command_buffer);
    return;
  }

  SDL_GPUColorTargetInfo targetInfo2D = {};
  targetInfo2D.texture = swapTex;
  targetInfo2D.clear_color = {0.12f, 0.12f, 0.12f, 1};
  targetInfo2D.load_op = SDL_GPU_LOADOP_CLEAR;
  targetInfo2D.store_op = SDL_GPU_STOREOP_STORE;
  targetInfo2D.mip_level = 0;
  targetInfo2D.layer_or_depth_plane = 0;
  targetInfo2D.cycle = false;

  ImGui_ImplSDLGPU3_PrepareDrawData(drawData, command_buffer);

  auto copyPass = SDL_BeginGPUCopyPass(command_buffer);
  for (const auto &passCb : copyPasses) {
    passCb.second(command_buffer, copyPass);
  }
  for (const auto &passCb : copyPassesOneTime) {
    passCb(command_buffer, copyPass);
  }
  copyPassesOneTime.clear();
  SDL_EndGPUCopyPass(copyPass);

  if (ctx.project)
  {
    for (const auto &passCb : renderPasses) {
      passCb.second(command_buffer, *this);
    }
  }

  // Render ImGui
  SDL_GPURenderPass* renderPass2D = SDL_BeginGPURenderPass(command_buffer, &targetInfo2D, 1, nullptr);
  ImGui_ImplSDLGPU3_RenderDrawData(drawData, command_buffer, renderPass2D);
  SDL_EndGPURenderPass(renderPass2D);

  // Update and Render additional Platform Windows
  if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }

  // Submit the command buffer
  SDL_SubmitGPUCommandBuffer(command_buffer);

  if (ctx.project)
  {
    for (const auto &cb : postRenderCallback) {
      cb.second(*this);
    }
  }
}
