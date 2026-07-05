/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "particleWrap.h"

#include "framebuffer.h"
#include "particleBatch.h"
#include "scene.h"
#include "../context.h"

void Renderer::ParticleWrapPass::ensureSize(const Scene& scene, uint32_t w, uint32_t h)
{
  if(width == w && height == h && texAccum && texSceneCopy) return;
  if(texAccum)     SDL_ReleaseGPUTexture(ctx.gpu, texAccum);
  if(texSceneCopy) SDL_ReleaseGPUTexture(ctx.gpu, texSceneCopy);
  width = w; height = h;

  SDL_GPUTextureCreateInfo ti{};
  ti.type = SDL_GPU_TEXTURETYPE_2D;
  ti.width = w;
  ti.height = h;
  ti.layer_count_or_depth = 1;
  ti.num_levels = 1;
  ti.sample_count = SDL_GPU_SAMPLECOUNT_1;

  ti.format = scene.getAccumFormat();
  ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  texAccum = SDL_CreateGPUTexture(ctx.gpu, &ti);

  ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; // matches the viewport color target
  ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  texSceneCopy = SDL_CreateGPUTexture(ctx.gpu, &ti);
}

Renderer::ParticleWrapPass::~ParticleWrapPass()
{
  if(texAccum)     SDL_ReleaseGPUTexture(ctx.gpu, texAccum);
  if(texSceneCopy) SDL_ReleaseGPUTexture(ctx.gpu, texSceneCopy);
}

void Renderer::ParticleWrapPass::run(SDL_GPUCommandBuffer* cmdBuff, Scene& scene, Framebuffer& fb,
  const ParticleBatch& batch, const UniformGlobal& uniGlobal,
  const std::vector<ParticleDrawGroup>& groups)
{
  bool anyAdditive = false;
  for(const auto &g : groups) {
    if(g.blend == ParticleBlend::Additive && g.vertCount > 0) { anyAdditive = true; break; }
  }
  if(!anyAdditive || !batch.isReady() || batch.getVertexCount() == 0) return;
  if(!fb.getTexture() || fb.getWidth() == 0 || fb.getHeight() == 0) return;

  ensureSize(scene, fb.getWidth(), fb.getHeight());
  if(!texAccum || !texSceneCopy) return;

  // 1) snapshot the scene: the composite reads it while rewriting the same color target
  {
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuff);
    SDL_GPUTextureLocation src{}, dst{};
    src.texture = fb.getTexture();
    dst.texture = texSceneCopy;
    SDL_CopyGPUTextureToTexture(copyPass, &src, &dst, width, height, 1, true);
    SDL_EndGPUCopyPass(copyPass);
  }

  // 2) sum the additive groups into the float accum target (cleared to 0). Depth is the
  //    viewport's buffer, test-only, so occlusion matches the main pass; the objID target
  //    keeps receiving 0 under particle pixels exactly like the normal particle path.
  {
    SDL_GPUColorTargetInfo targets[2]{fb.getTargetInfo()[0], fb.getTargetInfo()[1]};
    targets[0].texture = texAccum;
    targets[0].load_op = SDL_GPU_LOADOP_CLEAR;
    targets[0].clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    targets[0].cycle = true;
    targets[1].load_op = SDL_GPU_LOADOP_LOAD;

    SDL_GPUDepthStencilTargetInfo depthInfo = fb.getDepthTargetInfo();
    depthInfo.load_op = SDL_GPU_LOADOP_LOAD;
    depthInfo.stencil_load_op = SDL_GPU_LOADOP_LOAD;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdBuff, targets, 2, &depthInfo);
    drawParticleGroups(cmdBuff, pass, scene, batch, uniGlobal, groups,
      ParticleGroupFilter::OnlyAdditive, &scene.getParticleAccumPipeline());
    SDL_EndGPURenderPass(pass);
  }

  // 3) fullscreen composite: scene + sum with the RDP's carry-dropping add (mod 256)
  {
    SDL_GPUColorTargetInfo target = fb.getTargetInfo()[0];
    target.load_op = SDL_GPU_LOADOP_LOAD; // fully overwritten, but partial-size safety first

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdBuff, &target, 1, nullptr);
    scene.getPtxWrapCompositePipeline().bind(pass);

    SDL_GPUTextureSamplerBinding texBinds[2]{};
    texBinds[0].texture = texSceneCopy;
    texBinds[0].sampler = scene.getParticleSampler(false, false); // texelFetch; state irrelevant
    texBinds[1].texture = texAccum;
    texBinds[1].sampler = texBinds[0].sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, texBinds, 2);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
  }
}
