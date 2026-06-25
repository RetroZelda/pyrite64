/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "particleBatch.h"

#include <cstring>

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
