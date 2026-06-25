/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "../overlay.h"
#include "debug/debugDraw.h"
#include "debug/menu.h"
#include "renderer/particles/emitterManager.h"
#include "renderer/particles/emitterDef.h"

#include <cstdio>
#include <cmath>

namespace
{
  constexpr uint32_t POOL = 16; // must match EmitterManager's pool size
  bool drawShape[POOL]{};        // per-slot "draw emission shape" toggle (bound to menu items)
  char slotLabel[POOL][40]{};    // menu-item text, refreshed each frame from live emitter state

  const char* shapeName(uint8_t s)
  {
    switch(s) {
      case P64::PTX::ESHAPE_POINT:  return "point";
      case P64::PTX::ESHAPE_SPHERE: return "sphere";
      case P64::PTX::ESHAPE_BOX:    return "box";
      case P64::PTX::ESHAPE_CONE:   return "cone";
      default:                      return "?";
    }
  }
}

// One BOOL toggle per emitter slot. The menu's built-in UP/DOWN navigates the list and
// LEFT/RIGHT toggles the selected slot's shape-draw flag. Text points at a persistent buffer
// (slotLabel) that ovlParticles() refreshes each frame, so the rows show live emitter state.
void P64::Debug::Overlay::buildParticlesMenu(Menu& m)
{
  for(uint32_t i = 0; i < POOL; ++i) {
    std::snprintf(slotLabel[i], sizeof(slotLabel[i]), "E%lu ---", (unsigned long)i);
    m.add(slotLabel[i], drawShape[i]);
  }
}

void P64::Debug::Overlay::ovlParticles()
{
  // Refresh each slot's menu label from the current emitter state (1-frame lag is fine).
  for(uint32_t i = 0; i < POOL; ++i) {
    auto info = EmitterManager::getSlotDebugInfo(i);
    if(info.used && info.playing && info.def) {
      std::snprintf(slotLabel[i], sizeof(slotLabel[i]), "E%lu %s %lu/%lu",
        (unsigned long)i, shapeName(info.def->emitShape),
        (unsigned long)info.aliveCount, (unsigned long)info.maxParticles);
    } else {
      std::snprintf(slotLabel[i], sizeof(slotLabel[i]), "E%lu ---", (unsigned long)i);
    }
  }
}

void P64::Debug::Overlay::drawParticleShapes()
{
  for(uint32_t i = 0; i < POOL; ++i) {
    if(!drawShape[i]) continue;
    auto info = EmitterManager::getSlotDebugInfo(i);
    if(!info.used || !info.playing || !info.def) continue;

    const auto* d = info.def;
    fm_vec3_t pos = info.pos;
    color_t col = Debug::paletteColor(i);

    switch(d->emitShape) {
      case P64::PTX::ESHAPE_POINT: {
        Debug::drawSphere(pos, 4.0f, col);
        constexpr float ax = 24.0f; // RGB axes = XYZ transform gizmo for a point emitter
        fm_vec3_t px = pos; px.x += ax;  Debug::drawLine(pos, px, color_t{0xFF, 0x22, 0x22, 0xFF});
        fm_vec3_t py = pos; py.y += ax;  Debug::drawLine(pos, py, color_t{0x22, 0xFF, 0x22, 0xFF});
        fm_vec3_t pz = pos; pz.z += ax;  Debug::drawLine(pos, pz, color_t{0x22, 0x22, 0xFF, 0xFF});
      } break;
      case P64::PTX::ESHAPE_SPHERE:
        Debug::drawSphere(pos, d->shapeSize[0], col);
        break;
      case P64::PTX::ESHAPE_BOX: {
        fm_vec3_t he{}; he.x = d->shapeSize[0]; he.y = d->shapeSize[1]; he.z = d->shapeSize[2];
        Debug::drawAABB(pos, he, col);
      } break;
      case P64::PTX::ESHAPE_CONE: {
        fm_quat_t ident; fm_quat_identity(&ident);
        Debug::drawCone(pos, d->shapeSize[0], d->shapeSize[1] * 0.5f, ident, col);
      } break;
      default: break;
    }

    // Emission direction: a yellow line from the emitter along its base velocity.
    float vx = d->velocity[0], vy = d->velocity[1], vz = d->velocity[2];
    float len = std::sqrt(vx*vx + vy*vy + vz*vz);
    if(len > 1e-4f) {
      constexpr float DIR_LEN = 32.0f;
      float s = DIR_LEN / len;
      fm_vec3_t tip = pos; tip.x += vx*s; tip.y += vy*s; tip.z += vz*s;
      Debug::drawLine(pos, tip, color_t{0xFF, 0xFF, 0x22, 0xFF});
    }
  }
}
