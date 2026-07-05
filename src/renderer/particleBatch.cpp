/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "particleBatch.h"

#include <cstring>

#include "scene.h"
#include "texture.h"
#include "uniforms.h"

void Renderer::drawParticleGroups(SDL_GPUCommandBuffer* cmdBuff, SDL_GPURenderPass* pass, Scene& scene,
  const ParticleBatch& batch, const UniformGlobal& uniGlobal,
  const std::vector<ParticleDrawGroup>& groups,
  ParticleGroupFilter filter, const Pipeline* pipelineOverride)
{
  if(groups.empty() || !batch.isReady() || batch.getVertexCount() == 0) return;

  batch.bind(pass);

  const Pipeline* boundPipeline = nullptr;
  for(const auto &g : groups) {
    const bool isAdditive = g.blend == ParticleBlend::Additive;
    if(filter == ParticleGroupFilter::SkipAdditive && isAdditive) continue;
    if(filter == ParticleGroupFilter::OnlyAdditive && !isAdditive) continue;

    auto &pipeline = pipelineOverride ? *pipelineOverride : scene.getParticlePipeline(g.blend);
    if(&pipeline != boundPipeline) {
      pipeline.bind(pass);
      SDL_PushGPUVertexUniformData(cmdBuff, 0, &uniGlobal, sizeof(uniGlobal));
      boundPipeline = &pipeline;
    }

    // alpha-compare threshold (std140 vec4, only .x used) — matches rdpq_mode_alphacompare
    float fragParams[4]{g.alphaThreshold, 0.0f, 0.0f, 0.0f};
    SDL_PushGPUFragmentUniformData(cmdBuff, 0, fragParams, sizeof(fragParams));

    // texture + hardware-matching sampler; untextured emitters sample the 1px white texture
    SDL_GPUTextureSamplerBinding texBind{};
    texBind.texture = (g.tex && g.tex->getGPUTex()) ? g.tex->getGPUTex() : scene.getWhiteTexture();
    texBind.sampler = scene.getParticleSampler(g.mirror, g.bilinear);
    SDL_BindGPUFragmentSamplers(pass, 0, &texBind, 1);

    SDL_DrawGPUPrimitives(pass, g.vertCount, 1, g.firstVert, 0);
  }
}

Renderer::ParticleBatch::~ParticleBatch()
{
  if(buffer) SDL_ReleaseGPUBuffer(gpuDevice, buffer);
  if(bufferTrans) SDL_ReleaseGPUTransferBuffer(gpuDevice, bufferTrans);
}

void Renderer::ParticleBatch::resize(uint32_t requestedSize)
{
  if(requestedSize == 0) return;
  if(buffer) SDL_ReleaseGPUBuffer(gpuDevice, buffer);
  if(bufferTrans) SDL_ReleaseGPUTransferBuffer(gpuDevice, bufferTrans);

  allocVertByteSize = requestedSize;

  SDL_GPUBufferCreateInfo bufferInfo{};
  bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  bufferInfo.size = requestedSize;
  buffer = SDL_CreateGPUBuffer(gpuDevice, &bufferInfo);

  SDL_GPUTransferBufferCreateInfo transferInfo{};
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferInfo.size = requestedSize;
  bufferTrans = SDL_CreateGPUTransferBuffer(gpuDevice, &transferInfo);
}

void Renderer::ParticleBatch::setVertices(const std::vector<ParticleVertex> &verts)
{
  uint32_t neededSize = (uint32_t)(verts.size() * sizeof(ParticleVertex));
  if(neededSize == 0) { currVertByteSize = 0; needsUpload = false; return; }

  if(neededSize > allocVertByteSize) {
    resize((uint32_t)(neededSize * CAPACITY_HEADROOM));
  }
  if(!buffer || !bufferTrans) return;

  currVertByteSize = neededSize;
  void* data = SDL_MapGPUTransferBuffer(gpuDevice, bufferTrans, false);
  if(data) {
    std::memcpy(data, verts.data(), neededSize);
    SDL_UnmapGPUTransferBuffer(gpuDevice, bufferTrans);
    needsUpload = true;
  }
}

void Renderer::ParticleBatch::upload(SDL_GPUCopyPass* pass)
{
  if(!needsUpload || currVertByteSize == 0 || !pass) return;

  SDL_GPUTransferBufferLocation location{};
  location.transfer_buffer = bufferTrans;
  location.offset = 0;

  SDL_GPUBufferRegion region{};
  region.buffer = buffer;
  region.offset = 0;
  region.size = (uint32_t)currVertByteSize;

  SDL_UploadToGPUBuffer(pass, &location, &region, true);
  needsUpload = false;
}
