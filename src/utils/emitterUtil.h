/**
* @copyright 2026 - Max Bebök
* @license MIT
*
* Shared helpers so the editor's emitter PREVIEW and the in-scene viewport billboards build
* an identical P64::ParticleSim::Config from a Project::Assets::Emitter — single source of truth.
*/
#pragma once
#include <algorithm>
#include <vector>

#include "../project/assets/emitter.h"
#include "../renderer/vertex.h"
#include "../n64/libdragon.h" // RDPQ_BLENDER_* / FILTER_* (for hardware-faithful preview state)
#include "engine/include/renderer/particles/particleSim.h"

namespace Utils::Emitter
{
  // Scales the emitter's abstract "size" units into world units so particles are visible at the
  // same scale as velocity/gravity (which are in world units). Must match across preview + scene.
  constexpr float SIZE_TO_WORLD = 8.0f;

  inline P64::ParticleSim::Config toConfig(const Project::Assets::Emitter &em)
  {
    P64::ParticleSim::Config c{};
    c.maxParticles = std::max(2u, em.maxParticles.value);
    c.spawnRate = em.spawnRate.value;
    c.fireMode = em.fireMode.value;
    c.burstCount = em.burstCount.value;
    c.lifetime = em.lifetime.value;
    c.lifetimeVariance = em.lifetimeVariance.value;
    c.emitShape = em.emitShape.value;
    c.shapeSize = {em.shapeSize.value.x, em.shapeSize.value.y, em.shapeSize.value.z};
    c.velocity = {em.velocity.value.x, em.velocity.value.y, em.velocity.value.z};
    c.speedMin = em.speedMin.value;
    c.speedMax = em.speedMax.value;
    c.spread = em.spread.value;
    c.gravity = {em.gravity.value.x, em.gravity.value.y, em.gravity.value.z};
    c.drag = em.drag.value;
    c.worldSpace = em.worldSpace.value;
    c.rotationMin = em.rotationMin.value;
    c.rotationMax = em.rotationMax.value;
    c.spinMin = em.spinMin.value;
    c.spinMax = em.spinMax.value;
    for(int i = 0; i < 4; ++i) {
      c.colorStart[i] = em.colorStart.value[i];
      c.colorEnd[i] = em.colorEnd.value[i];
    }
    c.sizeStart = em.sizeStart.value;
    c.sizeEnd = em.sizeEnd.value;
    c.mirrorAnim = em.mirrorAnim.value;
    // hardware frame budget: u8 tex offsets = 8 sections; mirroring doubles the logical frames
    uint32_t maxFrames = c.mirrorAnim
      ? P64::ParticleSim::MAX_FRAME_SECTIONS / 2 : P64::ParticleSim::MAX_FRAME_SECTIONS;
    c.texFrames = std::clamp(em.texFrames.value, 1u, maxFrames);
    c.frameMode = em.frameMode.value;
    c.frameRate = em.frameRate.value;
    c.uvScrollSpeed = em.uvScrollSpeed.value;
    c.swapMode = em.swapMode.value;
    c.swapRate = em.swapRate.value;
    c.swapCount = 1;
    return c;
  }

  // Render state the previews must copy from the material so they match the RDP output:
  // blend mode, filter and the alpha-compare threshold (defaults mirror EmitterInstance).
  struct PreviewRenderInfo
  {
    Renderer::ParticleBlend blend{Renderer::ParticleBlend::Alpha};
    bool bilinear{false};                  // N64 particles default to point sampling
    bool mirrorSampler{false};             // Mirror Frames -> mirrored-repeat addressing
    float alphaThreshold{10.0f / 255.0f};  // rdpq_mode_alphacompare default
  };

  inline PreviewRenderInfo toRenderInfo(const Project::Assets::Emitter &em,
                                        const P64::ParticleSim::Config &cfg)
  {
    PreviewRenderInfo info{};
    const auto &m = em.material;
    if(m.blenderSet.value) {
      if(m.blender.value == 0)                             info.blend = Renderer::ParticleBlend::Opaque;
      else if(m.blender.value == RDPQ_BLENDER_ADDITIVE)    info.blend = Renderer::ParticleBlend::Additive;
      else                                                 info.blend = Renderer::ParticleBlend::Alpha;
    }
    info.bilinear = m.filterSet.value && m.filter.value == (int)FILTER_BILINEAR;
    int alphaComp = m.alphaCompSet.value ? m.alphaComp.value : 10;
    info.alphaThreshold = alphaComp > 0 ? (float)alphaComp / 255.0f : 0.0f;
    info.mirrorSampler = P64::ParticleSim::mirrorActive(cfg);
    return info;
  }

  // Builds hardware-faithful, depth-sorted billboard triangles for one emitter's particles
  // (sorts `particles` back-to-front in place). Matches the engine path exactly: quantized
  // int8 sizes, logical-frame UVs (incl. the mirror trick), UV scroll in frame sections, and
  // NO per-particle rotation (tpx billboards can't rotate, so previews don't either).
  inline void buildBillboards(std::vector<Renderer::ParticleVertex> &out,
    std::vector<P64::ParticleSim::Particle> &particles,
    const P64::ParticleSim::Config &cfg, float simTime, const glm::vec3 &emitterPos,
    const glm::vec3 &camRight, const glm::vec3 &camUp, const glm::vec3 &camFwd)
  {
    namespace PS = P64::ParticleSim;

    // back-to-front along the view axis for correct alpha compositing (depth-write is off)
    std::sort(particles.begin(), particles.end(), [&](const PS::Particle &a, const PS::Particle &b){
      glm::vec3 ca{a.pos.x, a.pos.y, a.pos.z}; if(!cfg.worldSpace) ca += emitterPos;
      glm::vec3 cb{b.pos.x, b.pos.y, b.pos.z}; if(!cfg.worldSpace) cb += emitterPos;
      return glm::dot(ca, camFwd) > glm::dot(cb, camFwd);
    });

    const float scroll = PS::uvScrollSections(cfg, simTime);
    for(auto &p : particles) {
      float t = PS::lifeT(p);
      glm::vec3 center{p.pos.x, p.pos.y, p.pos.z};
      if(!cfg.worldSpace) center += emitterPos; // relative particles follow the emitter

      // quantized hardware size (int8 normalized to the emitter's max size, see particleSim.h)
      // scaled back to world units — quantizes exactly like on-device
      float ws = PS::hwSizeAt(cfg, t) * SIZE_TO_WORLD;

      float col4[4];
      PS::colorAt(cfg, t, col4);
      glm::u8vec4 col{
        (uint8_t)(std::clamp(col4[0], 0.0f, 1.0f) * 255.0f),
        (uint8_t)(std::clamp(col4[1], 0.0f, 1.0f) * 255.0f),
        (uint8_t)(std::clamp(col4[2], 0.0f, 1.0f) * 255.0f),
        (uint8_t)(std::clamp(col4[3], 0.0f, 1.0f) * 255.0f)};

      float u0, v0, u1, v1;
      PS::frameUV(cfg, PS::frameForParticle(cfg, p, simTime), scroll, u0, v0, u1, v1);

      glm::vec3 tl = center - camRight * ws + camUp * ws;
      glm::vec3 tr = center + camRight * ws + camUp * ws;
      glm::vec3 br = center + camRight * ws - camUp * ws;
      glm::vec3 bl = center - camRight * ws - camUp * ws;

      // two triangles (cull disabled): tl,tr,br + tl,br,bl
      out.push_back({tl, col, {u0, v0}});
      out.push_back({tr, col, {u1, v0}});
      out.push_back({br, col, {u1, v1}});
      out.push_back({tl, col, {u0, v0}});
      out.push_back({br, col, {u1, v1}});
      out.push_back({bl, col, {u0, v1}});
    }
  }
}
