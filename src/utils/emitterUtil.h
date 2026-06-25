/**
* @copyright 2026 - Max Bebök
* @license MIT
*
* Shared helpers so the editor's emitter PREVIEW and the in-scene viewport billboards build
* an identical P64::ParticleSim::Config from a Project::Assets::Emitter — single source of truth.
*/
#pragma once
#include <algorithm>

#include "../project/assets/emitter.h"
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
    c.texFrames = std::max(1u, em.texFrames.value);
    c.frameMode = em.frameMode.value;
    c.frameRate = em.frameRate.value;
    c.uvScrollSpeed = em.uvScrollSpeed.value;
    c.swapMode = em.swapMode.value;
    c.swapRate = em.swapRate.value;
    c.swapCount = 1;
    return c;
  }
}
